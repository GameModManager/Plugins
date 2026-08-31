/**
 * ANM2 Plugin — .anm2 animation preview + parser (v2 ABI)
 *
 * Parses The Binding of Isaac: Rebirth's XML-based .anm2 animation format
 * and provides both:
 *   1. A standalone QWidget preview (register_preview)
 *   2. An AnimationParser returning raw RGBA pixels (register_animation_parser)
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
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QXmlStreamReader>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

/* --------------------------------------------------------------------------
 * Data structures matching the .anm2 spec
 * ------------------------------------------------------------------------ */

struct Spritesheet {
  int id = -1;
  QString path;   // relative path from game resources dir (backslash-separated)
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
  bool interpolated = false;
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
 * .anm2 parser using QXmlStreamReader
 * ------------------------------------------------------------------------ */

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
  f.interpolated = attrs.value("Interpolated").toString() == "true";
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

  /* -- Spritesheets and Layers are in <Content> before <Animations> -- */
  /* Two-level traversal: first find the root <AnimatedActor>,
     then iterate its direct children (Content, Animations). */
  while (xml.readNextStartElement()) {
    if (xml.name() == u"AnimatedActor") {
      /* Now iterate children of AnimatedActor */
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
          /* Default animation name */
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
                      la.layer_id = xml.attributes().value("LayerId").toInt();
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

              /* Collect all animations if requested */
              if (all_anims)
                all_anims->append(a);

              /* Pick the default animation, or the first one */
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
      break; /* done with root element */
    } else {
      xml.skipCurrentElement();
    }
  }

  return anim.frame_num > 0;
}

/* --------------------------------------------------------------------------
 * Load spritesheet PNGs from a given base directory (or walk up for gfx/)
 * ------------------------------------------------------------------------ */

/* Host-resolved file path -- set by gmm_register_v2, used by loadSpritesheets.
   The host's PathResolver handles case-insensitive lookup on Linux. */
static GmmResolveFileFn g_resolve_file = nullptr;

/* Build candidate base directories for spritesheet resolution.
   Takes an explicit base_dir (from the animation parser) and walks up
   looking for gfx/ siblings. Also includes the base_dir itself. */
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

    /* Use host resolver for case-insensitive lookup */
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

    /* Fallback: try exact path (fast, no CI) */
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

/* Legacy loader: derives base_dir from the .anm2 file path (for preview widget)
 */
static void loadSpritesheets(const QString &anm2_path,
                             QList<Spritesheet> &spritesheets) {
  loadSpritesheetsFromDir(QFileInfo(anm2_path).absoluteDir().absolutePath(),
                          spritesheets);
}

/* --------------------------------------------------------------------------
 * Animation parser -- returns raw RGBA pixel data for the Core's
 * AnimationParserFeature / PreviewWidget::try_load_anm2().
 * ------------------------------------------------------------------------ */

/* Read FPS from the .anm2 <Info> tag (re-parses the file minimally). */
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

/* Crop a QPixmap to a sub-region and return it as a QImage of Format_RGBA8888.
 */
static QImage cropToRgba(const QPixmap &sheet, int cx, int cy, int cw, int ch) {
  if (sheet.isNull() || cw <= 0 || ch <= 0)
    return QImage();
  QPixmap cropped = sheet.copy(cx, cy, cw, ch);
  if (cropped.isNull())
    return QImage();
  return cropped.toImage().convertToFormat(QImage::Format_RGBA8888);
}

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

  /* Helper: compute total steps for an animation */
  auto computeSteps = [](const Animation &a) -> int {
    int max_layer_frames = 0;
    for (const auto &la : a.layer_animations)
      if (la.frames.size() > max_layer_frames)
        max_layer_frames = la.frames.size();
    int steps = a.frame_num;
    if (steps <= 0)
      steps = max_layer_frames;
    return steps;
  };

  /* Helper: compute fixed canvas size for an animation across ALL frames.
   * This ensures the canvas doesn't change size per frame (Issue 2). */
  auto computeFixedCanvas = [&](const Animation &a, int default_w,
                                int default_h) -> std::pair<int, int> {
    int gmin_x = INT_MAX, gmin_y = INT_MAX;
    int gmax_x = INT_MIN, gmax_y = INT_MIN;
    int total = computeSteps(a);
    if (total <= 0)
      return {default_w, default_h};

    for (int step = 0; step < total; ++step) {
      int min_x = INT_MAX, min_y = INT_MAX;
      int max_x = INT_MIN, max_y = INT_MIN;
      auto ub = [&](int x, int y, int w, int h) {
        if (x < min_x)
          min_x = x;
        if (y < min_y)
          min_y = y;
        if (x + w > max_x)
          max_x = x + w;
        if (y + h > max_y)
          max_y = y + h;
      };
      ub(a.root_frame.x_position, a.root_frame.y_position, default_w,
         default_h);
      for (const auto &la : a.layer_animations) {
        if (!la.visible)
          continue;
        int fi = qMin(step, la.frames.size() - 1);
        if (fi < 0)
          continue;
        const Anm2Frame &fr = la.frames[fi];
        if (!fr.visible)
          continue;
        int rx = fr.x_position - fr.x_pivot + fr.x_crop;
        int ry = fr.y_position - fr.y_pivot + fr.y_crop;
        int rw = fr.width > 0 ? fr.width : 64;
        int rh = fr.height > 0 ? fr.height : 64;
        ub(rx, ry, rw, rh);
      }
      if (min_x < gmin_x)
        gmin_x = min_x;
      if (min_y < gmin_y)
        gmin_y = min_y;
      if (max_x > gmax_x)
        gmax_x = max_x;
      if (max_y > gmax_y)
        gmax_y = max_y;
    }
    if (gmax_x <= gmin_x)
      gmax_x = gmin_x + default_w;
    if (gmax_y <= gmin_y)
      gmax_y = gmin_y + default_h;
    int pad = 10;
    int w = (gmax_x - gmin_x) + pad * 2;
    int h = (gmax_y - gmin_y) + pad * 2;
    if (w <= 0)
      w = default_w;
    if (h <= 0)
      h = default_h;
    return {w, h};
  };

  /* Helper: render a single animation into an array of GmmAnimationFrameV2 */
  auto renderAnimation =
      [&](const Animation &a, int cw,
          int ch) -> std::pair<GmmAnimationFrameV2 *, size_t> {
    int total = computeSteps(a);
    if (total <= 0)
      return {nullptr, 0};

    double rsx = a.root_frame.x_scale / 100.0;
    double rsy = a.root_frame.y_scale / 100.0;

    GmmAnimationFrameV2 *frames = static_cast<GmmAnimationFrameV2 *>(
        calloc(static_cast<size_t>(total), sizeof(GmmAnimationFrameV2)));
    if (!frames)
      return {nullptr, 0};

    /* Compute fixed bounding origin for this animation */
    int gmin_x = INT_MAX, gmin_y = INT_MAX;
    for (int step = 0; step < total; ++step) {
      int min_x = INT_MAX, min_y = INT_MAX;
      int max_x = INT_MIN, max_y = INT_MIN;
      auto ub = [&](int x, int y, int w, int h) {
        if (x < min_x)
          min_x = x;
        if (y < min_y)
          min_y = y;
        if (x + w > max_x)
          max_x = x + w;
        if (y + h > max_y)
          max_y = y + h;
      };
      ub(a.root_frame.x_position, a.root_frame.y_position, cw, ch);
      for (const auto &la : a.layer_animations) {
        if (!la.visible)
          continue;
        int fi = qMin(step, la.frames.size() - 1);
        if (fi < 0)
          continue;
        const Anm2Frame &fr = la.frames[fi];
        if (!fr.visible)
          continue;
        int rx = fr.x_position - fr.x_pivot + fr.x_crop;
        int ry = fr.y_position - fr.y_pivot + fr.y_crop;
        int rw = fr.width > 0 ? fr.width : 64;
        int rh = fr.height > 0 ? fr.height : 64;
        ub(rx, ry, rw, rh);
      }
      if (min_x < gmin_x)
        gmin_x = min_x;
      if (min_y < gmin_y)
        gmin_y = min_y;
    }
    int origin_x = -gmin_x;
    int origin_y = -gmin_y;

    for (int step = 0; step < total; ++step) {
      GmmAnimationFrameV2 &cf = frames[step];
      cf.delay_ms = 1000.0f / static_cast<float>(fps);

      QImage canvas(cw, ch, QImage::Format_RGBA8888);
      canvas.fill(Qt::transparent);
      QPainter p(&canvas);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      p.setRenderHint(QPainter::Antialiasing, false);

      for (const auto &la : a.layer_animations) {
        if (!la.visible)
          continue;
        int fi = qMin(step, la.frames.size() - 1);
        if (fi < 0)
          continue;
        const Anm2Frame &fr = la.frames[fi];
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

        double sx = fr.x_scale / 100.0 * rsx;
        double sy = fr.y_scale / 100.0 * rsy;
        int draw_x = origin_x + fr.x_position - fr.x_pivot;
        int draw_y = origin_y + fr.y_position - fr.y_pivot;
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

            if (fr.rotation != 0) {
              QTransform t;
              t.rotate(fr.rotation);
              scaled = scaled.transformed(t, Qt::SmoothTransformation);
            }

            p.drawPixmap(draw_x, draw_y, scaled);
          }
        }
      }
      p.end();

      QImage rgba = canvas.convertToFormat(QImage::Format_RGBA8888);

      cf.layer_count = 1;
      cf.layers = static_cast<GmmAnimationLayerV2 *>(
          calloc(1, sizeof(GmmAnimationLayerV2)));
      if (!cf.layers) {
        /* Clean up on allocation failure */
        for (size_t j = 0; j < static_cast<size_t>(step); ++j) {
          for (size_t k = 0; k < frames[j].layer_count; ++k)
            free(frames[j].layers[k].rgba_pixels);
          free(frames[j].layers);
        }
        free(frames);
        return {nullptr, 0};
      }

      cf.layers[0].x = 0;
      cf.layers[0].y = 0;
      cf.layers[0].width = rgba.width();
      cf.layers[0].height = rgba.height();
      cf.layers[0].pixel_count = static_cast<size_t>(rgba.sizeInBytes());
      cf.layers[0].rgba_pixels =
          static_cast<uint8_t *>(malloc(cf.layers[0].pixel_count));
      if (cf.layers[0].rgba_pixels) {
        memcpy(cf.layers[0].rgba_pixels, rgba.constBits(),
               cf.layers[0].pixel_count);
      }
    }

    return {frames, static_cast<size_t>(total)};
  };

  /* ---------------------------------------------------------------
   * Default animation: render into top-level frames (backward compat)
   * --------------------------------------------------------------- */
  auto [def_frames, def_count] = renderAnimation(anim, 400, 300);
  if (!def_frames || def_count == 0)
    return 0;

  auto [def_cw, def_ch] = computeFixedCanvas(anim, 400, 300);

  out->fps = static_cast<float>(fps);
  out->canvas_width = def_cw;
  out->canvas_height = def_ch;
  out->frames = def_frames;
  out->frame_count = def_count;

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

        /* Name: malloc'd, caller frees */
        QByteArray name_bytes = a.name.toUtf8();
        st.name = static_cast<char *>(malloc(name_bytes.size() + 1));
        if (st.name) {
          memcpy(st.name, name_bytes.constData(), name_bytes.size());
          st.name[name_bytes.size()] = '\0';
        }

        auto [cw, ch] = computeFixedCanvas(a, 400, 300);
        st.canvas_width = cw;
        st.canvas_height = ch;

        auto [frames, count] = renderAnimation(a, cw, ch);
        st.frames = frames;
        st.frame_count = count;
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

    /* Store layer defs for spritesheet lookup */
    layer_defs_ = layer_defs;

    /* Compute total animation steps.
       Each LayerAnimation[i].frames has some number of entries.
       The animation steps through them -- when a layer runs out of
       frames, it holds the last frame. Total steps = max across
       all layer frame counts, capped by frame_num. */
    int max_layer_frames = 0;
    for (const auto &la : anim.layer_animations)
      if (la.frames.size() > max_layer_frames)
        max_layer_frames = la.frames.size();

    total_steps_ = anim.frame_num;
    if (total_steps_ <= 0)
      total_steps_ = max_layer_frames;
    if (total_steps_ <= 0) {
      info_label_->setText("No animation frames found");
      return;
    }

    anim_ = std::move(anim);
    has_data_ = true;

    /* Default canvas: 400x300 -- will be adjusted per frame */
    canvas_w_ = 400;
    canvas_h_ = 300;

    /* FPS from file -- default 18 */
    int fps = readAnm2Fps(path);
    frame_interval_ms_ = 1000 / fps;

    info_label_->setText(QString("%1  [%2 frames, %3 fps]")
                             .arg(anim_.name)
                             .arg(total_steps_)
                             .arg(fps));

    renderStep(0);
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &Anm2PreviewWidget::nextStep);
    timer_->start(frame_interval_ms_);
  }

private:
  /* Find the spritesheet pixmap for a given layer id */
  QPixmap *sheetForLayer(int layer_id) {
    for (const auto &ld : layer_defs_) {
      if (ld.id == layer_id) {
        auto it = sheet_by_id_.find(ld.spritesheet_id);
        if (it != sheet_by_id_.end() && !it->second.isNull())
          return &it->second;
      }
    }
    return nullptr;
  }

  void renderStep(int step) {
    if (!has_data_ || step < 0 || step >= total_steps_)
      return;

    /* -- Pass 1: compute bounding box -- */
    int min_x = INT_MAX, min_y = INT_MAX;
    int max_x = INT_MIN, max_y = INT_MIN;

    auto updateBounds = [&](int x, int y, int w, int h) {
      if (x < min_x)
        min_x = x;
      if (y < min_y)
        min_y = y;
      if (x + w > max_x)
        max_x = x + w;
      if (y + h > max_y)
        max_y = y + h;
    };

    /* Root frame covers entire canvas */
    updateBounds(anim_.root_frame.x_position, anim_.root_frame.y_position,
                 canvas_w_, canvas_h_);

    for (const auto &la : anim_.layer_animations) {
      if (!la.visible)
        continue;
      int fi = qMin(step, la.frames.size() - 1);
      if (fi < 0)
        continue;
      const Anm2Frame &fr = la.frames[fi];
      if (!fr.visible)
        continue;

      int rx = fr.x_position - fr.x_pivot + fr.x_crop;
      int ry = fr.y_position - fr.y_pivot + fr.y_crop;
      int rw = fr.width > 0 ? fr.width : 64;
      int rh = fr.height > 0 ? fr.height : 64;
      updateBounds(rx, ry, rw, rh);
    }

    if (max_x <= min_x)
      max_x = min_x + canvas_w_;
    if (max_y <= min_y)
      max_y = min_y + canvas_h_;

    /* Add padding */
    int pad = 10;
    min_x -= pad;
    min_y -= pad;
    max_x += pad;
    max_y += pad;

    int w = max_x - min_x;
    int h = max_y - min_y;
    if (w <= 0 || h <= 0) {
      w = 400;
      h = 300;
    }

    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, false);

    /* -- Pass 2: render layers in order -- */
    int origin_x = -min_x;
    int origin_y = -min_y;

    /* Apply root transform */
    double root_sx = anim_.root_frame.x_scale / 100.0;
    double root_sy = anim_.root_frame.y_scale / 100.0;

    for (const auto &la : anim_.layer_animations) {
      if (!la.visible)
        continue;
      int fi = qMin(step, la.frames.size() - 1);
      if (fi < 0)
        continue;
      const Anm2Frame &fr = la.frames[fi];
      if (!fr.visible)
        continue;

      /* Find spritesheet for this layer */
      QPixmap *sheet = sheetForLayer(la.layer_id);

      /* Compute draw position */
      double sx = fr.x_scale / 100.0 * root_sx;
      double sy = fr.y_scale / 100.0 * root_sy;
      int draw_x = origin_x + fr.x_position - fr.x_pivot;
      int draw_y = origin_y + fr.y_position - fr.y_pivot;
      int crop_w = fr.width;
      int crop_h = fr.height;

      /* If no crop specified, use full spritesheet or default size */
      if (sheet && crop_w <= 0)
        crop_w = sheet->width();
      if (sheet && crop_h <= 0)
        crop_h = sheet->height();
      if (crop_w <= 0)
        crop_w = 64;
      if (crop_h <= 0)
        crop_h = 64;

      /* Draw the sprite or a colored placeholder */
      if (sheet && !sheet->isNull()) {
        QPixmap cropped = sheet->copy(fr.x_crop, fr.y_crop, crop_w, crop_h);
        if (!cropped.isNull()) {
          /* Apply scale */
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

          /* Apply rotation */
          if (fr.rotation != 0) {
            QTransform t;
            t.rotate(fr.rotation);
            scaled = scaled.transformed(t, Qt::SmoothTransformation);
          }

          p.drawPixmap(draw_x, draw_y, scaled);
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
        p.fillRect(draw_x, draw_y, sw, sh, kPalette[ci]);
        p.setPen(kPalette[ci].darker(130));
        p.drawRect(draw_x, draw_y, sw, sh);
      }
    }

    p.end();
    label_->setPixmap(pm);
  }

  void nextStep() {
    step_ = (step_ + 1) % total_steps_;
    renderStep(step_);
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
  int frame_interval_ms_ = 55; /* ~18 fps default */
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

  /* Capture host services */
  g_resolve_file = ctx->resolve_file;

  /* Register standalone preview widget */
  if (ctx->register_preview) {
    ctx->register_preview(ctx, ".anm2", nullptr, anm2_preview, nullptr);
  }

  /* Register animation parser for the Core's AnimationParserFeature.
   * NULL game_id = non-game-specific (applies to every game).
   * This populates rgba_pixels so PreviewWidget::try_load_anm2() works. */
  if (ctx->register_animation_parser) {
    ctx->register_animation_parser(ctx, nullptr, ".anm2", anm2_parse, 10,
                                   nullptr);
  }
}

} /* extern "C" */
