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

#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QTransform>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

/* --------------------------------------------------------------------------
 * Easing
 * curve evaluation
 *
 * ------------------------------------------------------------------------ */

static float interpolation_factor(Interpolation interpolation, float value)
{
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

int anm2_track_length_get(const QList<Anm2Frame>& keyframes)
{
  int length = 0;
  for (const auto& frame : keyframes)
    if (frame.delay > 0)
      length += frame.delay;
  return length;
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

Anm2Frame anm2_frame_generate(const QList<Anm2Frame>& keyframes, float time)
{
  Anm2Frame frame;
  if (keyframes.isEmpty())
    return frame;

  time                       = std::max(time, 0.0f);
  const Anm2Frame* frameNext = nullptr;
  int durationCurrent        = 0;
  int durationNext           = 0;

  for (int i = 0; i < keyframes.size(); ++i) {
    const auto& iFrame = keyframes[i];
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
 * Compute
 * animation total frame count (sum of keyframe durations or frame_num)
 *
 * ------------------------------------------------------------------------ */

int anm2_compute_total_frames(const Animation& a)
{
  int max_layer_frames = 0;
  for (const auto& la : a.layer_animations) {
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

std::pair<int, int> anm2_compute_animation_rect(const Animation& a, int default_w,
                                                int default_h)
{
  constexpr int CORNERS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  int total                   = anm2_compute_total_frames(a);
  if (total <= 0)
    return {default_w, default_h};

  float minX = 1e30f, minY = 1e30f;
  float maxX = -1e30f, maxY = -1e30f;
  bool isAny = false;

  for (float t = 0; t < static_cast<float>(total); t += 1.0f) {
    /* Root transform at this time */
    Anm2Frame rootFrame = anm2_frame_generate({a.root_frame}, t);
    QTransform rootTransform;
    rootTransform.translate(rootFrame.x_position, rootFrame.y_position);
    rootTransform.rotate(rootFrame.rotation);
    rootTransform.scale(rootFrame.x_scale / 100.0, rootFrame.y_scale / 100.0);

    for (const auto& la : a.layer_animations) {
      if (!la.visible)
        continue;
      if (la.frames.isEmpty())
        continue;
      Anm2Frame frame = anm2_frame_generate(la.frames, t);
      if (!frame.visible)
        continue;

      int crop_w = frame.width > 0 ? frame.width : 64;
      int crop_h = frame.height > 0 ? frame.height : 64;

      QTransform layerTransform;
      layerTransform.translate(frame.x_position, frame.y_position);
      layerTransform.rotate(frame.rotation);
      layerTransform.scale(frame.x_scale / 100.0, frame.y_scale / 100.0);
      layerTransform.translate(-frame.x_pivot, -frame.y_pivot);

      QTransform fullTransform = rootTransform * layerTransform;

      for (const auto& corner : CORNERS) {
        QPointF world =
            fullTransform.map(QPointF(corner[0] * crop_w, corner[1] * crop_h));
        minX  = std::min(minX, static_cast<float>(world.x()));
        minY  = std::min(minY, static_cast<float>(world.y()));
        maxX  = std::max(maxX, static_cast<float>(world.x()));
        maxY  = std::max(maxY, static_cast<float>(world.y()));
        isAny = true;
      }
    }
  }

  if (!isAny)
    return {default_w, default_h};

  int pad = 10;
  int w   = std::max(1, static_cast<int>(maxX - minX) + pad * 2);
  int h   = std::max(1, static_cast<int>(maxY - minY) + pad * 2);
  return {w, h};
}

/* --------------------------------------------------------------------------
 * Render
 * a single animation frame to QImage using on-demand interpolation.
 * Uses QTransform
 * for correct root transforms and per-layer transforms.
 *
 * ------------------------------------------------------------------------ */

QImage anm2_render_frame_at_time(const Animation& a, const QList<LayerDef>& layer_defs,
                                 std::map<int, QPixmap>& sheet_by_id, float time,
                                 int cw, int ch)
{
  QImage canvas(cw, ch, QImage::Format_RGBA8888);
  canvas.fill(Qt::transparent);
  QPainter p(&canvas);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::Antialiasing, false);

  /* Compute origin so that all content fits in the canvas */
  int gmin_x = INT_MAX, gmin_y = INT_MAX;
  {
    int total = anm2_compute_total_frames(a);
    for (float t = 0; t < static_cast<float>(total); t += 1.0f) {
      Anm2Frame rootFrame = anm2_frame_generate({a.root_frame}, t);
      QTransform rootTransform;
      rootTransform.translate(rootFrame.x_position, rootFrame.y_position);
      rootTransform.rotate(rootFrame.rotation);
      rootTransform.scale(rootFrame.x_scale / 100.0, rootFrame.y_scale / 100.0);

      for (const auto& la : a.layer_animations) {
        if (!la.visible)
          continue;
        if (la.frames.isEmpty())
          continue;
        Anm2Frame frame = anm2_frame_generate(la.frames, t);
        if (!frame.visible)
          continue;
        int crop_w = frame.width > 0 ? frame.width : 64;
        int crop_h = frame.height > 0 ? frame.height : 64;
        QTransform layerTransform;
        layerTransform.translate(frame.x_position, frame.y_position);
        layerTransform.rotate(frame.rotation);
        layerTransform.scale(frame.x_scale / 100.0, frame.y_scale / 100.0);
        layerTransform.translate(-frame.x_pivot, -frame.y_pivot);
        QTransform fullTransform    = rootTransform * layerTransform;
        constexpr int CORNERS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (const auto& corner : CORNERS) {
          QPointF world =
              fullTransform.map(QPointF(corner[0] * crop_w, corner[1] * crop_h));
          gmin_x = qMin(gmin_x, (int)world.x());
          gmin_y = qMin(gmin_y, (int)world.y());
        }
      }
    }
  }
  int origin_x = -gmin_x;
  int origin_y = -gmin_y;

  /* Root transform at this time */
  Anm2Frame rootFrame = anm2_frame_generate({a.root_frame}, time);
  QTransform rootTransform;
  rootTransform.translate(origin_x, origin_y);
  rootTransform.translate(rootFrame.x_position, rootFrame.y_position);
  rootTransform.rotate(rootFrame.rotation);
  rootTransform.scale(rootFrame.x_scale / 100.0, rootFrame.y_scale / 100.0);

  /* Render each layer using on-demand interpolated frames */
  for (const auto& la : a.layer_animations) {
    if (!la.visible)
      continue;
    if (la.frames.isEmpty())
      continue;

    /* Generate interpolated frame for this layer at this time */
    Anm2Frame fr = anm2_frame_generate(la.frames, time);
    if (!fr.visible)
      continue;

    /* Find spritesheet for this layer */
    QPixmap* sheet = nullptr;
    for (const auto& ld : layer_defs) {
      if (ld.id == la.layer_id) {
        auto it = sheet_by_id.find(ld.spritesheet_id);
        if (it != sheet_by_id.end() && !it->second.isNull())
          sheet = &it->second;
        break;
      }
    }

    double sx  = fr.x_scale / 100.0;
    double sy  = fr.y_scale / 100.0;
    int crop_w = fr.width;
    int crop_h = fr.height;

    if (sheet && crop_w <= 0)
      crop_w = sheet->width();
    if (sheet && crop_h <= 0)
      crop_h = sheet->height();
    if (crop_w <= 0)
      crop_w = 64;
    if (crop_h <= 0)
      crop_h = 64;

    if (sheet && !sheet->isNull()) {
      QPixmap cropped = sheet->copy(fr.x_crop, fr.y_crop, crop_w, crop_h);
      if (!cropped.isNull()) {
        int scaled_w   = qMax(1, (int)(crop_w * sx));
        int scaled_h   = qMax(1, (int)(crop_h * sy));
        QPixmap scaled = cropped.scaled(scaled_w, scaled_h, Qt::IgnoreAspectRatio,
                                        Qt::FastTransformation);

        /* Apply tint */
        if (fr.red_tint != 255 || fr.green_tint != 255 || fr.blue_tint != 255 ||
            fr.alpha_tint != 255 || fr.red_offset != 0 || fr.green_offset != 0 ||
            fr.blue_offset != 0) {
          QPainter sp(&scaled);
          sp.setCompositionMode(QPainter::CompositionMode_SourceIn);
          QColor tint(qBound(0, fr.red_tint + fr.red_offset, 255),
                      qBound(0, fr.green_tint + fr.green_offset, 255),
                      qBound(0, fr.blue_tint + fr.blue_offset, 255),
                      qBound(0, fr.alpha_tint, 255));
          sp.fillRect(scaled.rect(), tint);
          sp.end();
        }

        QTransform layerTransform;
        layerTransform.translate(fr.x_position, fr.y_position);
        layerTransform.rotate(fr.rotation);
        layerTransform.scale(sx, sy);
        layerTransform.translate(-fr.x_pivot, -fr.y_pivot);

        QTransform fullTransform = rootTransform * layerTransform;

        /* Apply the full transform via drawPixmap with QTransform */
        p.setTransform(fullTransform, false);
        p.drawPixmap(0, 0, scaled);
        p.resetTransform();
      }
    } else {
      /* No spritesheet -- draw colored rectangle placeholder */
      static const QColor kPalette[] = {
          QColor(100, 120, 180), QColor(140, 100, 160), QColor(100, 160, 140),
          QColor(180, 120, 100), QColor(120, 140, 160), QColor(160, 130, 130),
      };
      int ci = qAbs(la.layer_id) % 6;
      int sw = qMax(1, (int)(crop_w * sx));
      int sh = qMax(1, (int)(crop_h * sy));

      QTransform layerTransform;
      layerTransform.translate(fr.x_position, fr.y_position);
      layerTransform.rotate(fr.rotation);
      layerTransform.scale(sx, sy);
      layerTransform.translate(-fr.x_pivot, -fr.y_pivot);

      QTransform fullTransform = rootTransform * layerTransform;
      p.setTransform(fullTransform, false);
      p.fillRect(0, 0, sw, sh, kPalette[ci]);
      p.setPen(kPalette[ci].darker(130));
      p.drawRect(0, 0, sw, sh);
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

uint8_t* anm2_render_frame_cb(void* raw_animation, float time_ms, int32_t* out_width,
                              int32_t* out_height)
{
  if (!raw_animation || !out_width || !out_height)
    return nullptr;

  auto* data = static_cast<Anm2RawData*>(raw_animation);

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
  uint8_t* pixels    = static_cast<uint8_t*>(malloc(pixel_count));
  if (pixels)
    memcpy(pixels, rgba.constBits(), pixel_count);
  return pixels;
}
