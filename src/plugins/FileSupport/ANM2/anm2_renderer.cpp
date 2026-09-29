/**
 * ANM2 Renderer -- frame interpolation, canvas computation, QImage rendering.
 *
 *
 * Provides on-demand interpolated frame synthesis and rendering for
 * The Binding of
 * Isaac: Rebirth's .anm2 animation format.
 * Uses anm2ed's on-demand rendering model
 * for proper interpolation,
 * fixed canvas size, and correct root transforms.
 */

#include "anm2_renderer.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

/* --------------------------------------------------------------------------
 *
 * Canvas limits
 *
 * ------------------------------------------------------------------------ */

bool anm2_canvas_within_limits(int w, int h) {
  if (w <= 0 || h <= 0)
    return false;
  return static_cast<qint64>(w) * static_cast<qint64>(h) <= kAnm2MaxCanvasPixels;
}

/* Whether a content box of this span still fits once the canvas padding has
 * been added around it. Same rule as anm2_canvas_within_limits(), written
 * against a double span because that is what the bounds pass accumulates.
 *
 * No overflow is possible here: the span is a product of int32 attributes
 * evaluated in double, so it peaks around 2^62 and the square around 2^124,
 * both far inside the range a double holds. The value that could not survive
 * is the conversion to int in anm2_compute_animation_rect(), and the early
 * exit below is what keeps that conversion from ever running. */
static bool anm2_span_within_limits(double span_x, double span_y) {
  const double w = span_x + 2.0 * kAnm2CanvasPad;
  const double h = span_y + 2.0 * kAnm2CanvasPad;
  return w * h <= static_cast<double>(kAnm2MaxCanvasPixels);
}

/* The canvas a measured box asks for, before the edge cap is applied. */
static void anm2_natural_canvas(const QRectF &bounds, int *w, int *h) {
  *w = std::max(1,
                static_cast<int>(bounds.right() - bounds.left()) + kAnm2CanvasPad * 2);
  *h = std::max(1,
                static_cast<int>(bounds.bottom() - bounds.top()) + kAnm2CanvasPad * 2);
}

/* Scale that brings a canvas inside the edge cap, and the size it produces.
 *
 * One factor on both axes, so the aspect ratio survives: the longest edge
 * lands exactly on the cap and the other is derived from it, which is what
 * tells a scaled-down animation apart from a clipped one. The size is rounded
 * rather than truncated so a canvas that divides evenly does not come back one
 * pixel short, and neither edge is allowed to round down to nothing.
 *
 * Returns 1.0 for a canvas that already fits, so callers that only want the
 * size can ignore it and the draw path needs no branch of its own. */
static double anm2_edge_scale(int w, int h, int *out_w, int *out_h) {
  const int longest = std::max(w, h);
  const double s    = longest > kAnm2MaxCanvasEdge
                          ? static_cast<double>(kAnm2MaxCanvasEdge) / longest
                          : 1.0;
  *out_w            = std::max(1, static_cast<int>(std::lround(w * s)));
  *out_h            = std::max(1, static_cast<int>(std::lround(h * s)));
  return s;
}

/* Fixed size of the refusal image. Constant, not derived from the refused
 * request, so it cannot itself be made to grow. */
inline constexpr int kNoticeW = 480;
inline constexpr int kNoticeH = 160;

QImage anm2_oversize_notice() {
  QImage img(kNoticeW, kNoticeH, QImage::Format_RGBA8888);
  img.fill(Qt::transparent);

  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, true);

  const QRect box(0, 0, kNoticeW, kNoticeH);
  p.setPen(QPen(QColor(150, 60, 60), 2));
  p.drawRect(box.adjusted(1, 1, -2, -2));

  QFont title = p.font();
  title.setBold(true);
  title.setPointSizeF(title.pointSizeF() * 1.25);
  p.setFont(title);
  p.setPen(QColor(220, 120, 120));
  p.drawText(box.adjusted(16, 14, -16, 0), Qt::AlignHCenter | Qt::AlignTop,
             QStringLiteral("Animation not rendered"));

  p.setFont(QFont());
  p.setPen(QColor(200, 200, 200));
  p.drawText(box.adjusted(16, 58, -16, 0),
             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
             QStringLiteral("It asks for more than %1 million pixels. Anything "
                            "above that cannot be drawn; anything above %2 "
                            "pixels is drawn reduced to fit.")
                 .arg(kAnm2MaxCanvasPixels / (1000 * 1000))
                 .arg(kAnm2MaxCanvasEdge));

  p.end();
  return img;
}

/* --------------------------------------------------------------------------
 * Easing
 * curve evaluation
 *
 * ------------------------------------------------------------------------ */

static float interpolation_factor(Interpolation interpolation, float value) {
  value = std::clamp(value, 0.0f, 1.0f);
  switch (interpolation) {
  case Interpolation::LINEAR:
    return value;
  case Interpolation::EASE_IN:
    return value * value;
  case Interpolation::EASE_OUT:
    return 1.0f - ((1.0f - value) * (1.0f - value));
  case Interpolation::EASE_IN_OUT:
    return value < 0.5f ? (2.0f * value * value)
                        : (1.0f - std::pow(-2.0f * value + 2.0f, 2.0f) * 0.5f);
  default:
    return 0.0f;
  }
}

/* --------------------------------------------------------------------------
 * Sum of
 * all keyframe durations in a track
 *
 * ------------------------------------------------------------------------ */

int anm2_track_length_get(const QList<Anm2Frame> &keyframes) {
  /* Saturating rather than wrapping: a file whose delays add up past INT_MAX
   * would otherwise wrap to a negative length and land back in the caller as
   * a plausible small animation. */
  int64_t length = 0;
  for (const auto &frame : keyframes)
    if (frame.delay > 0)
      length += frame.delay;
  return static_cast<int>(std::min<int64_t>(length, kAnm2MaxTotalFrames));
}

/* --------------------------------------------------------------------------
 *
 * On-demand interpolated frame synthesis for a single layer's keyframes.
 * Finds the
 * keyframe at the given time and interpolates between it and the
 * next keyframe when
 * interpolation is enabled.
 *
 * ------------------------------------------------------------------------ */

Anm2Frame anm2_frame_generate(const QList<Anm2Frame> &keyframes, float time) {
  Anm2Frame frame;
  if (keyframes.isEmpty())
    return frame;

  time                       = std::max(time, 0.0f);
  const Anm2Frame *frameNext = nullptr;
  int durationCurrent        = 0;
  int durationNext           = 0;

  for (int i = 0; i < keyframes.size(); ++i) {
    const auto &iFrame = keyframes[i];
    frame              = iFrame;
    durationNext += frame.delay;

    if (time >= durationCurrent && time < durationNext) {
      /* Find next FRAME keyframe for interpolation */
      for (int next = i + 1; next < keyframes.size(); ++next) {
        frameNext = &keyframes[next];
        break;
      }

      /* Interpolate between current and next frame */
      if (frame.interpolation != Interpolation::NONE && frameNext && frame.delay > 1) {
        float amount = interpolation_factor(frame.interpolation,
                                            (time - durationCurrent) /
                                                (durationNext - durationCurrent));

        frame.x_position = qRound(frame.x_position +
                                  amount * (frameNext->x_position - frame.x_position));
        frame.y_position = qRound(frame.y_position +
                                  amount * (frameNext->y_position - frame.y_position));
        frame.x_pivot =
            qRound(frame.x_pivot + amount * (frameNext->x_pivot - frame.x_pivot));
        frame.y_pivot =
            qRound(frame.y_pivot + amount * (frameNext->y_pivot - frame.y_pivot));
        frame.x_scale =
            qRound(frame.x_scale + amount * (frameNext->x_scale - frame.x_scale));
        frame.y_scale =
            qRound(frame.y_scale + amount * (frameNext->y_scale - frame.y_scale));
        frame.rotation =
            qRound(frame.rotation + amount * (frameNext->rotation - frame.rotation));
        frame.red_tint =
            qRound(frame.red_tint + amount * (frameNext->red_tint - frame.red_tint));
        frame.green_tint = qRound(frame.green_tint +
                                  amount * (frameNext->green_tint - frame.green_tint));
        frame.blue_tint =
            qRound(frame.blue_tint + amount * (frameNext->blue_tint - frame.blue_tint));
        frame.alpha_tint = qRound(frame.alpha_tint +
                                  amount * (frameNext->alpha_tint - frame.alpha_tint));
        frame.red_offset = qRound(frame.red_offset +
                                  amount * (frameNext->red_offset - frame.red_offset));
        frame.green_offset =
            qRound(frame.green_offset +
                   amount * (frameNext->green_offset - frame.green_offset));
        frame.blue_offset = qRound(
            frame.blue_offset + amount * (frameNext->blue_offset - frame.blue_offset));
      }
      break;
    }

    durationCurrent += frame.delay;
  }

  return frame;
}

/* --------------------------------------------------------------------------
 * Shared geometry
 *
 * The canvas bounds pass and the draw pass measure and place the same
 * sprites, so both must build their transform and their crop size the same
 * way or the canvas ends up sized by one rule and drawn by another.
 * ------------------------------------------------------------------------ */

/* Per-layer transform.
 *
 * QTransform applies the most recent call to the point first, so this chain
 * reads T(position) * R(rotation) * S(scale) * T(-pivot). The pivot is
 * therefore stripped from the sprite before it is scaled and rotated, and the
 * pivot point itself lands exactly on the position - that is what "rotate
 * around a pin" means. Do not move the T(-pivot) to the front: that rotates
 * around a point that has itself already been scaled and rotated. */
static QTransform anm2_layer_transform(const Anm2Frame &f) {
  QTransform t;
  t.translate(f.x_position, f.y_position);
  t.rotate(f.rotation);
  t.scale(f.x_scale / 100.0, f.y_scale / 100.0);
  t.translate(-f.x_pivot, -f.y_pivot);
  return t;
}

/* Root transform, optionally displaced by a canvas origin.
 *
 * Root frames carry no pivot - XPivot/YPivot are LayerAnimation-only
 * attributes - so the root is a plain position/rotate/scale with the origin
 * offset pushed outside it. */
static QTransform anm2_root_transform(const Anm2Frame &f, double origin_x,
                                      double origin_y) {
  QTransform t;
  t.translate(origin_x, origin_y);
  t.translate(f.x_position, f.y_position);
  t.rotate(f.rotation);
  t.scale(f.x_scale / 100.0, f.y_scale / 100.0);
  return t;
}

/* Size of the crop rectangle a frame draws.
 *
 * One rule for both passes: a frame carrying Width/Height uses them, anything
 * else falls back to the 64x64 default. Falling back to the spritesheet size
 * would make the drawn size depend on the sheet while the bounds pass used
 * the default, so the two would disagree on layers whose frame omits a size. */
static void anm2_crop_size(const Anm2Frame &f, int *w, int *h) {
  *w = f.width > 0 ? f.width : 64;
  *h = f.height > 0 ? f.height : 64;
}

/* What measuring the animation's content came to. */
enum class Anm2Bounds {
  Ok,      /* a content box was measured */
  Empty,   /* nothing in the file is drawable */
  TooLarge /* the file asks for more canvas than the limits allow */
};

/* Union of every visible layer's transformed sprite rectangle over the whole
 * animation. Both the canvas size and the draw origin come from this, so they
 * cannot drift apart.
 *
 * Only layer animations are measured. A <NullAnimation> is deliberately not
 * folded in, and that is a decision rather than an omission: the frames under
 * one move an object belonging to the running game, not a sprite in this file,
 * so they have no crop rectangle and no dimensions to place. See the note on
 * Animation in anm2_types.h.
 *
 * Stops as soon as the box it is accumulating outgrows the limits, so a file
 * asking for a canvas of billions of pixels costs the same as one asking for
 * a few thousand: the loop runs until the box is too big, not to the end of
 * the animation. Nothing is allocated from the result on that path, and the
 * rect is left untouched. */
static Anm2Bounds anm2_content_bounds(const Animation &a, QRectF *out) {
  constexpr int CORNERS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

  int total = anm2_compute_total_frames(a);
  if (total <= 0)
    return Anm2Bounds::Empty;

  double min_x = 1e30, min_y = 1e30, max_x = -1e30, max_y = -1e30;
  bool any = false;

  for (float t = 0; t < static_cast<float>(total); t += 1.0f) {
    QTransform root = anm2_root_transform(anm2_frame_generate({a.root_frame}, t), 0, 0);

    for (const auto &la : a.layer_animations) {
      if (!la.visible || la.frames.isEmpty())
        continue;
      Anm2Frame frame = anm2_frame_generate(la.frames, t);
      if (!frame.visible)
        continue;

      int crop_w, crop_h;
      anm2_crop_size(frame, &crop_w, &crop_h);

      /* QTransform's operator* applies the left-hand transform to the point
       * first, so the layer goes on the left: the root transform, and with it
       * the canvas origin, wraps the finished layer rather than being folded
       * into it. Written the other way round, a layer scale or a mirror
       * would scale the origin shift along with the sprite and throw the
       * content off the canvas. */
      QTransform full = anm2_layer_transform(frame) * root;
      for (const auto &corner : CORNERS) {
        QPointF world = full.map(QPointF(corner[0] * crop_w, corner[1] * crop_h));
        min_x         = qMin(min_x, world.x());
        min_y         = qMin(min_y, world.y());
        max_x         = qMax(max_x, world.x());
        max_y         = qMax(max_y, world.y());
        any           = true;
      }
    }

    /* The box only ever grows, so one check per time step is enough to catch
     * it, and a hostile file is out of the loop on its first step. */
    if (any && !anm2_span_within_limits(max_x - min_x, max_y - min_y))
      return Anm2Bounds::TooLarge;
  }

  if (!any)
    return Anm2Bounds::Empty;
  *out = QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
  return Anm2Bounds::Ok;
}

/* --------------------------------------------------------------------------
 * Compute
 * animation total frame count (sum of keyframe durations or frame_num)
 *
 * ------------------------------------------------------------------------ */

int anm2_compute_total_frames(const Animation &a) {
  int max_layer_frames = 0;
  for (const auto &la : a.layer_animations) {
    int layer_len = anm2_track_length_get(la.frames);
    if (layer_len > max_layer_frames)
      max_layer_frames = layer_len;
  }
  int total = a.frame_num;
  if (total <= 0)
    total = max_layer_frames;
  return total;
}

/* --------------------------------------------------------------------------
 * Compute
 * fixed canvas size across all frames using interpolated keyframes.
 * Iterates every
 * time step, generates interpolated frames for all layers,
 * and takes the global
 * bounding box. This ensures the canvas never changes
 * size between frames (fixes
 * canvas bouncing).
 *
 * ------------------------------------------------------------------------ */

std::pair<int, int> anm2_compute_animation_rect(const Animation &a, int default_w,
                                                int default_h) {
  QRectF bounds;
  Anm2Bounds measured = anm2_content_bounds(a, &bounds);
  if (measured == Anm2Bounds::TooLarge)
    return {kNoticeW, kNoticeH};
  if (measured == Anm2Bounds::Empty)
    return {default_w, default_h};

  /* The span is known to be inside the limits, so both of these fit in an int
   * and the conversion is exact. */
  int w = 0, h = 0;
  anm2_natural_canvas(bounds, &w, &h);

  /* Reported already scaled down, so a caller that allocates this size gets an
   * image the renderer can actually fill. */
  int fit_w = 0, fit_h = 0;
  anm2_edge_scale(w, h, &fit_w, &fit_h);
  return {fit_w, fit_h};
}

/* --------------------------------------------------------------------------
 * Render
 * a single animation frame to QImage using on-demand interpolation.
 * Uses QTransform
 * for correct root transforms and per-layer transforms.
 *
 * ------------------------------------------------------------------------ */

QImage anm2_render_frame_at_time(const Animation &a, const QList<LayerDef> &layer_defs,
                                 std::map<int, QPixmap> &sheet_by_id, float time,
                                 int cw, int ch) {
  /* The gate sits ahead of the QImage constructor, so a canvas the file asks
   * for but the limits refuse is never allocated. Two ways in: the animation
   * measures past the limits, or the caller handed over a size that does. The
   * first is what a hostile .anm2 looks like; the second catches a stale size
   * arriving from the host. Either way the answer is the same refusal image,
   * and its size is what anm2_compute_animation_rect() reported, so the canvas
   * the host allocated around these pixels fits them exactly. */
  QRectF bounds;
  Anm2Bounds measured = anm2_content_bounds(a, &bounds);
  if (measured == Anm2Bounds::TooLarge || !anm2_canvas_within_limits(cw, ch))
    return anm2_oversize_notice();

  /* A size inside the area limit but past the edge cap is drawn whole at a
   * reduced size rather than cut off, so the canvas allocated below is the one
   * inside the cap and the content is scaled into it. The factor comes from
   * the measured box rather than from cw/ch, because a caller that sized its
   * buffer through anm2_compute_animation_rect() has already been given the
   * capped size and scaling by that against itself would leave the content
   * twice the size of the canvas. Taking the smaller of the two per-axis
   * ratios puts the content inside the canvas whichever axis the caller's size
   * is short on, and for a caller that used the compute entry point the two
   * ratios are equal. */
  int fit_w = 0, fit_h = 0;
  double fit = anm2_edge_scale(cw, ch, &fit_w, &fit_h);
  if (measured == Anm2Bounds::Ok) {
    int natural_w = 0, natural_h = 0;
    anm2_natural_canvas(bounds, &natural_w, &natural_h);
    fit = std::min(static_cast<double>(fit_w) / natural_w,
                   static_cast<double>(fit_h) / natural_h);
  }

  QImage canvas(fit_w, fit_h, QImage::Format_RGBA8888);
  canvas.fill(Qt::transparent);
  QPainter p(&canvas);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::Antialiasing, false);

  /* Same bounds the canvas was sized from, so the origin that shifts the
   * content to 0,0 and the padding that reserves room around it describe the
   * same rectangle. */
  bool has_bounds = (measured == Anm2Bounds::Ok);
  int origin_x    = has_bounds ? -qFloor(bounds.left()) : 0;
  int origin_y    = has_bounds ? -qFloor(bounds.top()) : 0;

  /* Root transform at this time */
  Anm2Frame rootFrame = anm2_frame_generate({a.root_frame}, time);
  /* The fit goes on the right so it is applied after the origin shift, and
   * shrinks the padded content into the canvas rather than the canvas into the
   * content. The padding therefore stays kAnm2CanvasPad wide until the very
   * last step, and what survives of it is what the fit leaves behind. */
  QTransform rootTransform = anm2_root_transform(rootFrame, origin_x, origin_y) *
                             QTransform::fromScale(fit, fit);

  /* Render each layer using on-demand interpolated frames */
  for (const auto &la : a.layer_animations) {
    if (!la.visible)
      continue;
    if (la.frames.isEmpty())
      continue;

    /* Generate interpolated frame for this layer at this time */
    Anm2Frame fr = anm2_frame_generate(la.frames, time);
    if (!fr.visible)
      continue;

    /* Find spritesheet for this layer */
    QPixmap *sheet = nullptr;
    for (const auto &ld : layer_defs) {
      if (ld.id == la.layer_id) {
        auto it = sheet_by_id.find(ld.spritesheet_id);
        if (it != sheet_by_id.end() && !it->second.isNull())
          sheet = &it->second;
        break;
      }
    }

    int crop_w, crop_h;
    anm2_crop_size(fr, &crop_w, &crop_h);

    QTransform fullTransform = anm2_layer_transform(fr) * rootTransform;

    if (sheet && !sheet->isNull()) {
      QPixmap cropped = sheet->copy(fr.x_crop, fr.y_crop, crop_w, crop_h);
      if (!cropped.isNull()) {
        /* Tint: a flat colour keeping the source alpha, so it is independent
         * of scale and is applied before the transform rescales the sprite.
         * The offset is a signed shift on top of the tint - negative values
         * darken a channel - so the sum is what gets clamped to a channel
         * range. Clamping the two separately would discard every negative
         * offset, which is how a lit sprite is darkened. */
        if (fr.red_tint != 255 || fr.green_tint != 255 || fr.blue_tint != 255 ||
            fr.alpha_tint != 255 || fr.red_offset != 0 || fr.green_offset != 0 ||
            fr.blue_offset != 0) {
          QPainter sp(&cropped);
          sp.setCompositionMode(QPainter::CompositionMode_SourceIn);
          QColor tint(qBound(0, fr.red_tint + fr.red_offset, 255),
                      qBound(0, fr.green_tint + fr.green_offset, 255),
                      qBound(0, fr.blue_tint + fr.blue_offset, 255),
                      qBound(0, fr.alpha_tint, 255));
          sp.fillRect(cropped.rect(), tint);
          sp.end();
        }

        /* The transform carries the only scale. Pre-scaling the pixmap as
         * well would square it, and a negative scale would have to be faked
         * with an absolute value and a mirror. */
        p.setTransform(fullTransform, false);
        p.drawPixmap(0, 0, cropped);
        p.resetTransform();
      }
    } else {
      /* No spritesheet -- draw colored rectangle placeholder */
      static const QColor kPalette[] = {
          QColor(100, 120, 180), QColor(140, 100, 160), QColor(100, 160, 140),
          QColor(180, 120, 100), QColor(120, 140, 160), QColor(160, 130, 130),
      };
      int ci = qAbs(la.layer_id) % 6;

      p.setTransform(fullTransform, false);
      p.fillRect(0, 0, crop_w, crop_h, kPalette[ci]);
      p.setPen(kPalette[ci].darker(130));
      p.drawRect(0, 0, crop_w, crop_h);
      p.resetTransform();
    }
  }

  p.end();
  return canvas;
}
/* --------------------------------------------------------------------------
 *
 * On-demand render callback for the ABI.
 * Called by the host's preview window to
 * render a single frame.
 * Returns malloc'd RGBA pixels (caller frees).
 *
 * ------------------------------------------------------------------------ */

uint8_t *anm2_render_frame_cb(void *raw_animation, float time_ms, int32_t *out_width,
                              int32_t *out_height) {
  if (!raw_animation || !out_width || !out_height)
    return nullptr;

  auto *data = static_cast<Anm2RawData *>(raw_animation);

  /* Clamp time to valid range */
  float total_time = static_cast<float>(data->total_frames);
  float t = std::clamp(time_ms, 0.0f, total_time > 0 ? total_time - 1.0f : 0.0f);

  QImage frame =
      anm2_render_frame_at_time(data->anim, data->layer_defs, data->sheet_by_id, t,
                                data->canvas_width, data->canvas_height);

  if (frame.isNull())
    return nullptr;

  QImage rgba = frame.convertToFormat(QImage::Format_RGBA8888);
  *out_width  = rgba.width();
  *out_height = rgba.height();

  size_t pixel_count = static_cast<size_t>(rgba.sizeInBytes());
  uint8_t *pixels    = static_cast<uint8_t *>(malloc(pixel_count));
  if (pixels)
    memcpy(pixels, rgba.constBits(), pixel_count);
  return pixels;
}
