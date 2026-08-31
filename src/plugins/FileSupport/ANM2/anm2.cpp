/**
 * ANM2 Plugin -- .anm2 animation preview + parser (v2 ABI)
 *
 * Parses The Binding of Isaac: Rebirth's XML-based .anm2 animation format
 * and provides both:
 *   1. A standalone QWidget preview (register_preview)
 *   2. An AnimationParser returning raw RGBA pixels (register_animation_parser)
 *
 * Uses anm2ed's on-demand rendering model for proper interpolation,
 * fixed canvas size, and correct root transforms.
 *
 * Format reference: https://www.moddingofisaac.com/docs/rep/xml/Anm2_files.html
 *
 * Build: shared library (MODULE), links Qt6::Widgets for QWidget creation.
 */

#include "gmm_abi_v2.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

/* --------------------------------------------------------------------------
 * Interpolation types -- matches anm2ed's on-demand model
 * ------------------------------------------------------------------------ */

enum class Interpolation { NONE, LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT };

/* --------------------------------------------------------------------------
 * Data structures matching the .anm2 spec
 * ------------------------------------------------------------------------ */

struct Spritesheet {
  int id = -1;
  QString path;   // relative path from game resources dir
  QPixmap pixmap; // loaded PNG
};

struct LayerDef {
  int id = -1;
  QString name;
  int spritesheet_id = -1;
};

struct Anm2Frame {
  int x_position = 0;
  int y_position = 0;
  int x_pivot = 0;
  int y_pivot = 0;
  int x_crop = 0;
  int y_crop = 0;
  int width = 0;
  int height = 0;
  int x_scale = 100;
  int y_scale = 100;
  int delay = 1; // duration in animation frames
  bool visible = true;
  int rotation = 0; // degrees clockwise
  int red_tint = 255;
  int green_tint = 255;
  int blue_tint = 255;
  int alpha_tint = 255;
  int red_offset = 0;
  int green_offset = 0;
  int blue_offset = 0;
  Interpolation interpolation = Interpolation::LINEAR;
};

struct LayerAnimation {
  int layer_id = -1;
  bool visible = true;
  QList<Anm2Frame> frames;
};

struct Animation {
  QString name;
  int frame_num = 0;
  bool loop = true;
  Anm2Frame root_frame; // single base transform
  QList<LayerAnimation> layer_animations;
};

/* --------------------------------------------------------------------------
 * On-demand rendering helpers (anm2ed model)
 * ------------------------------------------------------------------------ */

/* Easing curve evaluation */
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

/* Sum of all keyframe durations in a track */
static int track_length_get(const QList<Anm2Frame> &keyframes) {
  int length = 0;
  for (const auto &frame : keyframes)
    if (frame.delay > 0)
      length += frame.delay;
  return length;
}

/* On-demand interpolated frame synthesis for a single layer's keyframes.
 * Finds the keyframe at the given time and interpolates between it and the
 * next keyframe when interpolation is enabled. */
static Anm2Frame frame_generate(const QList<Anm2Frame> &keyframes,
                                float time) {
  Anm2Frame frame;
  if (keyframes.isEmpty())
    return frame;

  time = std::max(time, 0.0f);
  const Anm2Frame *frameNext = nullptr;
  int durationCurrent = 0;
  int durationNext = 0;

  for (int i = 0; i < keyframes.size(); ++i) {
    const auto &iFrame = keyframes[i];
    frame = iFrame;
    durationNext += frame.delay;

    if (time >= durationCurrent && time < durationNext) {
      /* Find next FRAME keyframe for interpolation */
      for (int next = i + 1; next < keyframes.size(); ++next) {
        frameNext = &keyframes[next];
        break;
      }

      /* Interpolate between current and next frame */
      if (frame.interpolation != Interpolation::NONE && frameNext &&
          frame.delay > 1) {
        float amount = interpolation_factor(
            frame.interpolation,
            (time - durationCurrent) / (durationNext - durationCurrent));

        frame.x_position =
            qRound(frame.x_position +
                   amount * (frameNext->x_position - frame.x_position));
        frame.y_position =
            qRound(frame.y_position +
                   amount * (frameNext->y_position - frame.y_position));
        frame.x_pivot =
            qRound(frame.x_pivot +
                   amount * (frameNext->x_pivot - frame.x_pivot));
        frame.y_pivot =
            qRound(frame.y_pivot +
                   amount * (frameNext->y_pivot - frame.y_pivot));
        frame.x_crop =
            qRound(frame.x_crop +
                   amount * (frameNext->x_crop - frame.x_crop));
        frame.y_crop =
            qRound(frame.y_crop +
                   amount * (frameNext->y_crop - frame.y_crop));
        frame.width =
            qRound(frame.width + amount * (frameNext->width - frame.width));
        frame.height = qRound(frame.height +
                              amount * (frameNext->height - frame.height));
        frame.x_scale =
            qRound(frame.x_scale +
                   amount * (frameNext->x_scale - frame.x_scale));
        frame.y_scale =
            qRound(frame.y_scale +
                   amount * (frameNext->y_scale - frame.y_scale));
        frame.rotation =
            qRound(frame.rotation +
                   amount * (frameNext->rotation - frame.rotation));
        frame.red_tint =
            qRound(frame.red_tint +
                   amount * (frameNext->red_tint - frame.red_tint));
        frame.green_tint =
            qRound(frame.green_tint +
                   amount * (frameNext->green_tint - frame.green_tint));
        frame.blue_tint =
            qRound(frame.blue_tint +
                   amount * (frameNext->blue_tint - frame.blue_tint));
        frame.alpha_tint =
            qRound(frame.alpha_tint +
                   amount * (frameNext->alpha_tint - frame.alpha_tint));
        frame.red_offset =
            qRound(frame.red_offset +
                   amount * (frameNext->red_offset - frame.red_offset));
        frame.green_offset =
            qRound(frame.green_offset +
                   amount * (frameNext->green_offset - frame.green_offset));
        frame.blue_offset =
            qRound(frame.blue_offset +
                   amount * (frameNext->blue_offset - frame.blue_offset));
      }
      break;
    }

    durationCurrent += frame.delay;
  }

  return frame;
}

/* --------------------------------------------------------------------------
 * .anm2 parser using QXmlStreamReader
 * ------------------------------------------------------------------------ */

static Interpolation parseInterpolation(const QString &str) {
  if (str.isEmpty() || str == "False" || str == "false")
    return Interpolation::NONE;
  if (str == "True" || str == "true" || str == "Linear")
    return Interpolation::LINEAR;
  if (str == "EaseIn")
    return Interpolation::EASE_IN;
  if (str == "EaseOut")
    return Interpolation::EASE_OUT;
  if (str == "EaseInOut")
    return Interpolation::EASE_IN_OUT;
  return Interpolation::LINEAR; // default
}

static Anm2Frame parseFrame(QXmlStreamReader &xml) {
  Anm2Frame f;
  auto attrs = xml.attributes();
  f.x_position = attrs.value("XPosition").toInt();
  f.y_position = attrs.value("YPosition").toInt();
  f.x_pivot = attrs.value("XPivot").toInt();
  f.y_pivot = attrs.value("YPivot").toInt();
  f.x_crop = attrs.value("XCrop").toInt();
  f.y_crop = attrs.value("YCrop").toInt();
  f.width = attrs.value("Width").toInt();
  f.height = attrs.value("Height").toInt();
  f.x_scale = attrs.value("XScale").toInt();
  f.y_scale = attrs.value("YScale").toInt();
  if (f.x_scale == 0)
    f.x_scale = 100;
  if (f.y_scale == 0)
    f.y_scale = 100;
  f.delay = attrs.value("Delay").toInt();
  if (f.delay <= 0)
    f.delay = 1;
  f.visible = attrs.value("Visible").toString() != "false";
  f.rotation = attrs.value("Rotation").toInt();
  f.red_tint = attrs.value("RedTint").toInt();
  f.green_tint = attrs.value("GreenTint").toInt();
  f.blue_tint = attrs.value("BlueTint").toInt();
  f.alpha_tint = attrs.value("AlphaTint").toInt();
  if (f.red_tint == 0)
    f.red_tint = 255;
  if (f.green_tint == 0)
    f.green_tint = 255;
  if (f.blue_tint == 0)
    f.blue_tint = 255;
  if (f.alpha_tint == 0)
    f.alpha_tint = 255;
  f.red_offset = attrs.value("RedOffset").toInt();
  f.green_offset = attrs.value("GreenOffset").toInt();
  f.blue_offset = attrs.value("BlueOffset").toInt();
  f.interpolation = parseInterpolation(attrs.value("Interpolated").toString());
  return f;
}

static bool parseAnm2(const QString &path, Animation &anim,
                      QList<Spritesheet> &spritesheets,
                      QList<LayerDef> &layer_defs,
                      QList<Animation> *all_anims = nullptr) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;

  QXmlStreamReader xml(&file);

  while (xml.readNextStartElement()) {
    if (xml.name() == u"AnimatedActor") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"Content") {
          while (xml.readNextStartElement()) {
            if (xml.name() == u"Spritesheets") {
              while (xml.readNextStartElement()) {
                if (xml.name() == u"Spritesheet") {
                  Spritesheet ss;
                  ss.id = xml.attributes().value("Id").toInt();
                  ss.path = xml.attributes().value("Path").toString();
                  spritesheets.append(ss);
                  xml.skipCurrentElement();
                } else {
                  xml.skipCurrentElement();
                }
              }
            } else if (xml.name() == u"Layers") {
              while (xml.readNextStartElement()) {
                if (xml.name() == u"Layer") {
                  LayerDef ld;
                  ld.id = xml.attributes().value("Id").toInt();
                  ld.name = xml.attributes().value("Name").toString();
                  ld.spritesheet_id =
                      xml.attributes().value("SpritesheetId").toInt();
                  layer_defs.append(ld);
                  xml.skipCurrentElement();
                } else {
                  xml.skipCurrentElement();
                }
              }
            } else {
              xml.skipCurrentElement();
            }
          }
        } else if (xml.name() == u"Animations") {
          QString defaultAnim =
              xml.attributes().value("DefaultAnimation").toString();

          while (xml.readNextStartElement()) {
            if (xml.name() == u"Animation") {
              Animation a;
              a.name = xml.attributes().value("Name").toString();
              a.frame_num = xml.attributes().value("FrameNum").toInt();
              a.loop = xml.attributes().value("Loop").toString() != "false";

              while (xml.readNextStartElement()) {
                if (xml.name() == u"RootAnimation") {
                  while (xml.readNextStartElement()) {
                    if (xml.name() == u"Frame") {
                      a.root_frame = parseFrame(xml);
                      xml.skipCurrentElement();
                    } else {
                      xml.skipCurrentElement();
                    }
                  }
                } else if (xml.name() == u"LayerAnimations") {
                  while (xml.readNextStartElement()) {
                    if (xml.name() == u"LayerAnimation") {
                      LayerAnimation la;
                      la.layer_id =
                          xml.attributes().value("LayerId").toInt();
                      la.visible =
                          xml.attributes().value("Visible").toString() !=
                          "false";
                      while (xml.readNextStartElement()) {
                        if (xml.name() == u"Frame") {
                          la.frames.append(parseFrame(xml));
                          xml.skipCurrentElement();
                        } else {
                          xml.skipCurrentElement();
                        }
                      }
                      a.layer_animations.append(la);
                    } else {
                      xml.skipCurrentElement();
                    }
                  }
                } else {
                  xml.skipCurrentElement();
                }
              }

              if (all_anims)
                all_anims->append(a);

              if (anim.name.isEmpty() || a.name == defaultAnim)
                anim = std::move(a);
            } else {
              xml.skipCurrentElement();
            }
          }
        } else {
          xml.skipCurrentElement();
        }
      }
      break;
    } else {
      xml.skipCurrentElement();
    }
  }

  return anim.frame_num > 0;
}

/* --------------------------------------------------------------------------
 * Load spritesheet PNGs from a given base directory (or walk up for gfx/)
 * ------------------------------------------------------------------------ */

static GmmResolveFileFn g_resolve_file = nullptr;

static QStringList buildBaseDirs(const QString &base_dir) {
  QStringList dirs;
  dirs << base_dir;
  QDir walk(base_dir);
  for (int i = 0; i < 5; ++i) {
    QDir gfx_candidate(walk.absoluteFilePath("gfx"));
    if (gfx_candidate.exists() && !dirs.contains(gfx_candidate.absolutePath()))
      dirs << gfx_candidate.absolutePath();
    if (walk.dirName().compare("gfx", Qt::CaseInsensitive) == 0) {
      if (!dirs.contains(walk.absolutePath()))
        dirs << walk.absolutePath();
    }
    if (!walk.cdUp())
      break;
  }
  return dirs;
}

static void loadSpritesheetsFromDir(const QString &base_dir,
                                    QList<Spritesheet> &spritesheets) {
  QStringList base_dirs = buildBaseDirs(base_dir);

  for (auto &ss : spritesheets) {
    QString rel = ss.path;
    rel.replace('\\', '/');
    bool loaded = false;

    if (g_resolve_file) {
      for (const auto &base : base_dirs) {
        QByteArray root_bytes = base.toUtf8();
        QByteArray rel_bytes = rel.toUtf8();
        char *resolved = g_resolve_file(root_bytes.constData(),
                                        rel_bytes.constData(), nullptr);
        if (resolved) {
          QString path = QString::fromUtf8(resolved);
          free(resolved);
          if (ss.pixmap.load(path)) {
            loaded = true;
            break;
          }
        }
      }
    }

    if (!loaded) {
      for (const auto &base : base_dirs) {
        QString full = QDir(base).absoluteFilePath(rel);
        if (ss.pixmap.load(full)) {
          loaded = true;
          break;
        }
      }
    }
  }
}

static void loadSpritesheets(const QString &anm2_path,
                             QList<Spritesheet> &spritesheets) {
  loadSpritesheetsFromDir(QFileInfo(anm2_path).absoluteDir().absolutePath(),
                          spritesheets);
}

/* --------------------------------------------------------------------------
 * Animation parser -- returns raw RGBA pixel data for the Core's
 * AnimationParserFeature / PreviewWidget::try_load_anm2().
 * ------------------------------------------------------------------------ */

static int readAnm2Fps(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return 18;
  QXmlStreamReader xr(&f);
  while (xr.readNextStartElement()) {
    if (xr.name() == u"Info") {
      int fps = xr.attributes().value("Fps").toInt();
      return fps > 0 ? fps : 18;
    }
    xr.skipCurrentElement();
  }
  return 18;
}

static QImage cropToRgba(const QPixmap &sheet, int cx, int cy, int cw, int ch) {
  if (sheet.isNull() || cw <= 0 || ch <= 0)
    return QImage();
  QPixmap cropped = sheet.copy(cx, cy, cw, ch);
  if (cropped.isNull())
    return QImage();
  return cropped.toImage().convertToFormat(QImage::Format_RGBA8888);
}

/* --------------------------------------------------------------------------
 * Compute animation total frame count (sum of keyframe durations or frame_num)
 * ------------------------------------------------------------------------ */

static int computeTotalFrames(const Animation &a) {
  int max_layer_frames = 0;
  for (const auto &la : a.layer_animations) {
    int layer_len = track_length_get(la.frames);
    if (layer_len > max_layer_frames)
      max_layer_frames = layer_len;
  }
  int total = a.frame_num;
  if (total <= 0)
    total = max_layer_frames;
  return total;
}

/* --------------------------------------------------------------------------
 * Compute fixed canvas size across all frames using interpolated keyframes.
 * Iterates every time step, generates interpolated frames for all layers,
 * and takes the global bounding box. This ensures the canvas never changes
 * size between frames (fixes canvas bouncing).
 * ------------------------------------------------------------------------ */

static std::pair<int, int> computeAnimationRect(const Animation &a,
                                                int default_w, int default_h) {
  constexpr int CORNERS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  int total = computeTotalFrames(a);
  if (total <= 0)
    return {default_w, default_h};

  float minX = 1e30f, minY = 1e30f;
  float maxX = -1e30f, maxY = -1e30f;
  bool isAny = false;

  for (float t = 0; t < static_cast<float>(total); t += 1.0f) {
    /* Root transform at this time */
    Anm2Frame rootFrame = frame_generate({a.root_frame}, t);
    QTransform rootTransform;
    rootTransform.translate(rootFrame.x_position, rootFrame.y_position);
    rootTransform.scale(rootFrame.x_scale / 100.0, rootFrame.y_scale / 100.0);
    rootTransform.rotate(rootFrame.rotation);

    for (const auto &la : a.layer_animations) {
      if (!la.visible)
        continue;
      Anm2Frame frame = frame_generate(la.frames, t);
      if (!frame.visible)
        continue;

      int crop_w = frame.width > 0 ? frame.width : 64;
      int crop_h = frame.height > 0 ? frame.height : 64;

      /* Per-layer transform: translate(position - pivot), scale, rotate */
      QTransform layerTransform;
      layerTransform.translate(frame.x_position - frame.x_pivot,
                               frame.y_position - frame.y_pivot);
      layerTransform.scale(frame.x_scale / 100.0, frame.y_scale / 100.0);
      layerTransform.rotate(frame.rotation);

      QTransform fullTransform = rootTransform * layerTransform;

      for (const auto &corner : CORNERS) {
        QPointF world =
            fullTransform.map(QPointF(corner[0] * crop_w, corner[1] * crop_h));
        minX = std::min(minX, static_cast<float>(world.x()));
        minY = std::min(minY, static_cast<float>(world.y()));
        maxX = std::max(maxX, static_cast<float>(world.x()));
        maxY = std::max(maxY, static_cast<float>(world.y()));
        isAny = true;
      }
    }
  }

  if (!isAny)
    return {default_w, default_h};

  int pad = 10;
  int w = std::max(1, static_cast<int>(maxX - minX) + pad * 2);
  int h = std::max(1, static_cast<int>(maxY - minY) + pad * 2);
  return {w, h};
}

/* --------------------------------------------------------------------------
 * Render a single animation frame to QImage using on-demand interpolation.
 * Uses QTransform for correct root transforms and per-layer transforms.
 * ------------------------------------------------------------------------ */

static QImage renderFrameAtTime(const Animation &a,
                                const QList<LayerDef> &layer_defs,
                                std::map<int, QPixmap> &sheet_by_id,
                                float time, int cw, int ch) {
  QImage canvas(cw, ch, QImage::Format_RGBA8888);
  canvas.fill(Qt::transparent);
  QPainter p(&canvas);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setRenderHint(QPainter::Antialiasing, false);

  /* Compute origin so that all content fits in the canvas */
  int gmin_x = INT_MAX, gmin_y = INT_MAX;
  {
    int total = computeTotalFrames(a);
    for (float t = 0; t < static_cast<float>(total); t += 1.0f) {
      Anm2Frame rootFrame = frame_generate({a.root_frame}, t);
      QTransform rootTransform;
      rootTransform.translate(rootFrame.x_position, rootFrame.y_position);
      rootTransform.scale(rootFrame.x_scale / 100.0, rootFrame.y_scale / 100.0);
      rootTransform.rotate(rootFrame.rotation);

      for (const auto &la : a.layer_animations) {
        if (!la.visible)
          continue;
        Anm2Frame frame = frame_generate(la.frames, t);
        if (!frame.visible)
          continue;
        int crop_w = frame.width > 0 ? frame.width : 64;
        int crop_h = frame.height > 0 ? frame.height : 64;
        QTransform layerTransform;
        layerTransform.translate(frame.x_position - frame.x_pivot,
                                 frame.y_position - frame.y_pivot);
        layerTransform.scale(frame.x_scale / 100.0, frame.y_scale / 100.0);
        layerTransform.rotate(frame.rotation);
        QTransform fullTransform = rootTransform * layerTransform;
        constexpr int CORNERS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (const auto &corner : CORNERS) {
          QPointF world = fullTransform.map(
              QPointF(corner[0] * crop_w, corner[1] * crop_h));
          gmin_x = qMin(gmin_x, (int)world.x());
          gmin_y = qMin(gmin_y, (int)world.y());
        }
      }
    }
  }
  int origin_x = -gmin_x;
  int origin_y = -gmin_y;

  /* Root transform at this time */
  Anm2Frame rootFrame = frame_generate({a.root_frame}, time);
  QTransform rootTransform;
  rootTransform.translate(rootFrame.x_position, rootFrame.y_position);
  rootTransform.scale(rootFrame.x_scale / 100.0, rootFrame.y_scale / 100.0);
  rootTransform.rotate(rootFrame.rotation);

  /* Render each layer using on-demand interpolated frames */
  for (const auto &la : a.layer_animations) {
    if (!la.visible)
      continue;

    /* Generate interpolated frame for this layer at this time */
    Anm2Frame fr = frame_generate(la.frames, time);
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

    double sx = fr.x_scale / 100.0;
    double sy = fr.y_scale / 100.0;
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
        int scaled_w = qMax(1, (int)(crop_w * sx));
        int scaled_h = qMax(1, (int)(crop_h * sy));
        QPixmap scaled =
            cropped.scaled(scaled_w, scaled_h, Qt::IgnoreAspectRatio,
                           Qt::FastTransformation);

        /* Apply tint */
        if (fr.red_tint != 255 || fr.green_tint != 255 ||
            fr.blue_tint != 255 || fr.alpha_tint != 255 ||
            fr.red_offset != 0 || fr.green_offset != 0 ||
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

        /* Per-layer transform using QTransform (correct transforms) */
        QTransform layerTransform;
        layerTransform.translate(fr.x_position - fr.x_pivot,
                                 fr.y_position - fr.y_pivot);
        layerTransform.scale(sx, sy);
        layerTransform.rotate(fr.rotation);

        QTransform fullTransform = rootTransform * layerTransform;

        /* Apply the full transform via drawPixmap with QTransform */
        p.setTransform(fullTransform, false);
        p.drawPixmap(origin_x, origin_y, scaled);
        p.resetTransform();
      }
    } else {
      /* No spritesheet -- draw colored rectangle placeholder */
      static const QColor kPalette[] = {
          QColor(100, 120, 180), QColor(140, 100, 160),
          QColor(100, 160, 140), QColor(180, 120, 100),
          QColor(120, 140, 160), QColor(160, 130, 130),
      };
      int ci = qAbs(la.layer_id) % 6;
      int sw = qMax(1, (int)(crop_w * sx));
      int sh = qMax(1, (int)(crop_h * sy));

      QTransform layerTransform;
      layerTransform.translate(fr.x_position - fr.x_pivot,
                               fr.y_position - fr.y_pivot);
      layerTransform.scale(sx, sy);
      layerTransform.rotate(fr.rotation);

      QTransform fullTransform = rootTransform * layerTransform;
      p.setTransform(fullTransform, false);
      p.fillRect(origin_x, origin_y, sw, sh, kPalette[ci]);
      p.setPen(kPalette[ci].darker(130));
      p.drawRect(origin_x, origin_y, sw, sh);
      p.resetTransform();
    }
  }

  p.end();
  return canvas;
}

/* --------------------------------------------------------------------------
 * On-demand render callback for the ABI.
 * Called by the host's preview window to render a single frame.
 * Returns malloc'd RGBA pixels (caller frees).
 * ------------------------------------------------------------------------ */

struct Anm2RawData {
  Animation anim;
  QList<Spritesheet> spritesheets;
  QList<LayerDef> layer_defs;
  std::map<int, QPixmap> sheet_by_id;
  int fps = 18;
  int total_frames = 0;
  int canvas_width = 0;
  int canvas_height = 0;
};

static uint8_t *anm2_render_frame_cb(void *raw_animation, float time_ms,
                                     int32_t *out_width, int32_t *out_height) {
  if (!raw_animation || !out_width || !out_height)
    return nullptr;

  auto *data = static_cast<Anm2RawData *>(raw_animation);

  /* Clamp time to valid range */
  float total_time = static_cast<float>(data->total_frames);
  float t = std::clamp(time_ms, 0.0f, total_time > 0 ? total_time - 1.0f : 0.0f);

  QImage frame =
      renderFrameAtTime(data->anim, data->layer_defs, data->sheet_by_id, t,
                        data->canvas_width, data->canvas_height);

  if (frame.isNull())
    return nullptr;

  QImage rgba = frame.convertToFormat(QImage::Format_RGBA8888);
  *out_width = rgba.width();
  *out_height = rgba.height();

  size_t pixel_count = static_cast<size_t>(rgba.sizeInBytes());
  uint8_t *pixels = static_cast<uint8_t *>(malloc(pixel_count));
  if (pixels)
    memcpy(pixels, rgba.constBits(), pixel_count);
  return pixels;
}

/* --------------------------------------------------------------------------
 * Main animation parser entry point
 * ------------------------------------------------------------------------ */

static int anm2_parse(const char *file_path_c, const char *base_dir_c,
                      GmmAnimationDataV2 *out, void *) {
  if (!file_path_c || !out)
    return 0;

  QString file_path = QString::fromUtf8(file_path_c);
  QString base_dir = base_dir_c
                         ? QString::fromUtf8(base_dir_c)
                         : QFileInfo(file_path).absoluteDir().absolutePath();

  /* Parse the .anm2 XML -- collect ALL animations for named states */
  Animation anim;
  QList<Animation> all_anims;
  QList<Spritesheet> spritesheets;
  QList<LayerDef> layer_defs;
  if (!parseAnm2(file_path, anim, spritesheets, layer_defs, &all_anims))
    return 0;

  /* Load spritesheet PNGs relative to base_dir */
  loadSpritesheetsFromDir(base_dir, spritesheets);

  /* Build spritesheet lookup by id */
  std::map<int, QPixmap> sheet_by_id;
  for (const auto &ss : spritesheets)
    sheet_by_id[ss.id] = ss.pixmap;

  /* Read FPS */
  int fps = readAnm2Fps(file_path);

  /* Compute fixed canvas size for the default animation */
  auto [def_cw, def_ch] = computeAnimationRect(anim, 400, 300);

  /* Pre-bake the first frame as fallback for backward compat */
  int total = computeTotalFrames(anim);
  GmmAnimationFrameV2 *def_frames = nullptr;
  size_t def_count = 0;

  if (total > 0) {
    def_frames = static_cast<GmmAnimationFrameV2 *>(
        calloc(static_cast<size_t>(total), sizeof(GmmAnimationFrameV2)));
    if (def_frames) {
      def_count = static_cast<size_t>(total);
      for (int step = 0; step < total; ++step) {
        GmmAnimationFrameV2 &cf = def_frames[step];
        cf.delay_ms = 1000.0f / static_cast<float>(fps);

        QImage canvas = renderFrameAtTime(anim, layer_defs, sheet_by_id,
                                          static_cast<float>(step), def_cw, def_ch);
        QImage rgba = canvas.convertToFormat(QImage::Format_RGBA8888);

        cf.layer_count = 1;
        cf.layers = static_cast<GmmAnimationLayerV2 *>(
            calloc(1, sizeof(GmmAnimationLayerV2)));
        if (!cf.layers) {
          for (size_t j = 0; j < static_cast<size_t>(step); ++j) {
            for (size_t k = 0; k < def_frames[j].layer_count; ++k)
              free(def_frames[j].layers[k].rgba_pixels);
            free(def_frames[j].layers);
          }
          free(def_frames);
          def_frames = nullptr;
          def_count = 0;
          break;
        }

        cf.layers[0].x = 0;
        cf.layers[0].y = 0;
        cf.layers[0].width = rgba.width();
        cf.layers[0].height = rgba.height();
        cf.layers[0].pixel_count = static_cast<size_t>(rgba.sizeInBytes());
        cf.layers[0].rgba_pixels =
            static_cast<uint8_t *>(malloc(cf.layers[0].pixel_count));
        if (cf.layers[0].rgba_pixels)
          memcpy(cf.layers[0].rgba_pixels, rgba.constBits(),
                 cf.layers[0].pixel_count);
      }
    }
  }

  if (!def_frames || def_count == 0)
    return 0;

  out->fps = static_cast<float>(fps);
  out->canvas_width = def_cw;
  out->canvas_height = def_ch;
  out->frames = def_frames;
  out->frame_count = def_count;
  out->render_frame = anm2_render_frame_cb;

  /* ---------------------------------------------------------------
   * Allocate raw animation data for on-demand rendering
   * --------------------------------------------------------------- */
  auto *raw = new Anm2RawData();
  raw->anim = anim;
  raw->spritesheets = spritesheets;
  raw->layer_defs = layer_defs;
  raw->sheet_by_id = sheet_by_id;
  raw->fps = fps;
  raw->total_frames = total;
  raw->canvas_width = def_cw;
  raw->canvas_height = def_ch;
  out->raw_animation = raw;

  /* ---------------------------------------------------------------
   * Named states: render each animation from all_anims as a state
   * --------------------------------------------------------------- */
  if (all_anims.size() > 1) {
    out->state_count = static_cast<size_t>(all_anims.size());
    out->states = static_cast<GmmAnimationStateV2 *>(
        calloc(out->state_count, sizeof(GmmAnimationStateV2)));
    if (out->states) {
      for (int si = 0; si < all_anims.size(); ++si) {
        GmmAnimationStateV2 &st = out->states[si];
        const Animation &a = all_anims[si];

        QByteArray name_bytes = a.name.toUtf8();
        st.name = static_cast<char *>(malloc(name_bytes.size() + 1));
        if (st.name) {
          memcpy(st.name, name_bytes.constData(), name_bytes.size());
          st.name[name_bytes.size()] = '\0';
        }

        auto [cw, ch] = computeAnimationRect(a, 400, 300);
        st.canvas_width = cw;
        st.canvas_height = ch;

        int state_total = computeTotalFrames(a);
        st.frame_count = static_cast<size_t>(state_total);
        st.frames = static_cast<GmmAnimationFrameV2 *>(
            calloc(st.frame_count, sizeof(GmmAnimationFrameV2)));
        if (st.frames) {
          for (int step = 0; step < state_total; ++step) {
            GmmAnimationFrameV2 &cf = st.frames[step];
            cf.delay_ms = 1000.0f / static_cast<float>(fps);

            QImage canvas = renderFrameAtTime(a, layer_defs, sheet_by_id,
                                              static_cast<float>(step), cw, ch);
            QImage rgba = canvas.convertToFormat(QImage::Format_RGBA8888);

            cf.layer_count = 1;
            cf.layers = static_cast<GmmAnimationLayerV2 *>(
                calloc(1, sizeof(GmmAnimationLayerV2)));
            if (cf.layers) {
              cf.layers[0].x = 0;
              cf.layers[0].y = 0;
              cf.layers[0].width = rgba.width();
              cf.layers[0].height = rgba.height();
              cf.layers[0].pixel_count =
                  static_cast<size_t>(rgba.sizeInBytes());
              cf.layers[0].rgba_pixels =
                  static_cast<uint8_t *>(malloc(cf.layers[0].pixel_count));
              if (cf.layers[0].rgba_pixels)
                memcpy(cf.layers[0].rgba_pixels, rgba.constBits(),
                       cf.layers[0].pixel_count);
            }
          }
        }

        /* Per-state raw data for on-demand rendering */
        auto *state_raw = new Anm2RawData();
        state_raw->anim = a;
        state_raw->spritesheets = spritesheets;
        state_raw->layer_defs = layer_defs;
        state_raw->sheet_by_id = sheet_by_id;
        state_raw->fps = fps;
        state_raw->total_frames = state_total;
        state_raw->canvas_width = cw;
        state_raw->canvas_height = ch;
        st.raw_animation = state_raw;
        st.render_frame = anm2_render_frame_cb;
      }
    }
  } else {
    out->states = nullptr;
    out->state_count = 0;
  }

  return 1;
}

/* --------------------------------------------------------------------------
 * Preview widget -- renders animation frames (standalone QWidget)
 * Uses on-demand interpolation for smooth playback.
 * ------------------------------------------------------------------------ */

class Anm2PreviewWidget : public QWidget {
public:
  explicit Anm2PreviewWidget(const QString &path, QWidget *parent = nullptr)
      : QWidget(parent), step_(0), total_steps_(0) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    info_label_ = new QLabel(this);
    info_label_->setAlignment(Qt::AlignCenter);
    info_label_->setStyleSheet("color: gray;");
    lay->addWidget(info_label_);

    label_ = new QLabel(this);
    label_->setAlignment(Qt::AlignCenter);
    lay->addWidget(label_);

    /* Parse the .anm2 file */
    Animation anim;
    QList<Spritesheet> spritesheets;
    QList<LayerDef> layer_defs;
    if (!parseAnm2(path, anim, spritesheets, layer_defs)) {
      info_label_->setText("Failed to parse .anm2 file");
      return;
    }

    /* Load spritesheet PNGs */
    loadSpritesheets(path, spritesheets);

    /* Build spritesheet lookup by id */
    for (const auto &ss : spritesheets)
      sheet_by_id_[ss.id] = ss.pixmap;

    layer_defs_ = layer_defs;

    /* Compute total animation steps using track_length_get */
    total_steps_ = computeTotalFrames(anim);
    if (total_steps_ <= 0) {
      info_label_->setText("No animation frames found");
      return;
    }

    anim_ = std::move(anim);
    has_data_ = true;

    /* Compute fixed canvas size once (no canvas bouncing) */
    auto [cw, ch] = computeAnimationRect(anim_, 400, 300);
    canvas_w_ = cw;
    canvas_h_ = ch;

    int fps = readAnm2Fps(path);
    frame_interval_ms_ = 1000 / fps;

    info_label_->setText(QString("%1  [%2 frames, %3 fps]")
                             .arg(anim_.name)
                             .arg(total_steps_)
                             .arg(fps));

    /* Render first frame using on-demand rendering */
    renderTime(0.0f);
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &Anm2PreviewWidget::nextFrame);
    timer_->start(frame_interval_ms_);
  }

private:
  /* Render a frame at the given time using on-demand interpolation */
  void renderTime(float time) {
    if (!has_data_)
      return;

    QImage canvas =
        renderFrameAtTime(anim_, layer_defs_, sheet_by_id_, time,
                          canvas_w_, canvas_h_);
    label_->setPixmap(QPixmap::fromImage(canvas));
  }

  void nextFrame() {
    step_ = (step_ + 1) % total_steps_;
    renderTime(static_cast<float>(step_));
    timer_->start(frame_interval_ms_);
  }

  /* UI */
  QLabel *info_label_ = nullptr;
  QLabel *label_ = nullptr;
  QTimer *timer_ = nullptr;

  /* Animation data */
  bool has_data_ = false;
  Animation anim_;
  QList<LayerDef> layer_defs_;
  std::map<int, QPixmap> sheet_by_id_;
  int canvas_w_ = 400;
  int canvas_h_ = 300;

  /* Playback state */
  int step_ = 0;
  int total_steps_ = 0;
  int frame_interval_ms_ = 55;
};

/* --------------------------------------------------------------------------
 * Preview callback
 * ------------------------------------------------------------------------ */
static void *anm2_preview(const char *path, void *, void *) {
  if (!path)
    return nullptr;
  return new Anm2PreviewWidget(QString::fromUtf8(path));
}

/* --------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------ */
extern "C" {

uint32_t gmm_abi_version() { return GMM_ABI_VERSION; }

void gmm_register_v2(GmmRegistrationCtxV2 *ctx) {
  if (!ctx)
    return;

  ctx->register_plugin(ctx,
                       {.name = "ANM2",
                        .author = "GameModManager Team",
                        .version = "2.0.0",
                        .description = "ANM2 animation file preview and parser "
                                       "(The Binding of Isaac: Rebirth)"});

  if (ctx->register_category)
    ctx->register_category(ctx, "File Support");

  g_resolve_file = ctx->resolve_file;

  if (ctx->register_preview) {
    ctx->register_preview(ctx, ".anm2", nullptr, anm2_preview, nullptr);
  }

  if (ctx->register_animation_parser) {
    ctx->register_animation_parser(ctx, nullptr, ".anm2", anm2_parse, 10,
                                   nullptr);
  }
}

} /* extern "C" */
