/**
 * ANM2 Parser -- .anm2 XML parsing and spritesheet loading.
 *
 * Parses The
 * Binding of Isaac: Rebirth's XML-based .anm2 animation format
 * and loads associated
 * spritesheet PNGs.
 */

#include "anm2_parser.h"

#include "gmm_abi_v2.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QXmlStreamReader>
#include <cstdlib>
#include <cstring>

/* --------------------------------------------------------------------------
 * File
 * resolver callback (set by plugin registration)
 *
 * ------------------------------------------------------------------------ */

static GmmResolveFileFn g_resolve_file = nullptr;

void anm2_set_resolve_file(void* resolve_file)
{
  g_resolve_file = reinterpret_cast<GmmResolveFileFn>(resolve_file);
}

/* --------------------------------------------------------------------------
 *
 * Interpolation string parser
 *
 * ------------------------------------------------------------------------ */

static Interpolation parseInterpolation(const QString& str)
{
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
  return Interpolation::LINEAR;  // default
}

/* --------------------------------------------------------------------------
 * Parse a
 * single <Frame> element
 *
 * ------------------------------------------------------------------------ */

static Anm2Frame parseFrame(QXmlStreamReader& xml)
{
  Anm2Frame f;
  auto attrs   = xml.attributes();
  f.x_position = attrs.value("XPosition").toInt();
  f.y_position = attrs.value("YPosition").toInt();
  f.x_pivot    = attrs.value("XPivot").toInt();
  f.y_pivot    = attrs.value("YPivot").toInt();
  f.x_crop     = attrs.value("XCrop").toInt();
  f.y_crop     = attrs.value("YCrop").toInt();
  f.width      = attrs.value("Width").toInt();
  f.height     = attrs.value("Height").toInt();
  f.x_scale    = attrs.value("XScale").toInt();
  f.y_scale    = attrs.value("YScale").toInt();
  if (f.x_scale == 0)
    f.x_scale = 100;
  if (f.y_scale == 0)
    f.y_scale = 100;
  f.delay = attrs.value("Delay").toInt();
  if (f.delay <= 0)
    f.delay = 1;
  f.visible    = attrs.value("Visible").toString() != "false";
  f.rotation   = attrs.value("Rotation").toInt();
  f.red_tint   = attrs.value("RedTint").toInt();
  f.green_tint = attrs.value("GreenTint").toInt();
  f.blue_tint  = attrs.value("BlueTint").toInt();
  f.alpha_tint = attrs.value("AlphaTint").toInt();
  if (!attrs.hasAttribute("RedTint"))
    f.red_tint = 255;
  if (!attrs.hasAttribute("GreenTint"))
    f.green_tint = 255;
  if (!attrs.hasAttribute("BlueTint"))
    f.blue_tint = 255;
  if (!attrs.hasAttribute("AlphaTint"))
    f.alpha_tint = 255;
  f.red_offset    = attrs.value("RedOffset").toInt();
  f.green_offset  = attrs.value("GreenOffset").toInt();
  f.blue_offset   = attrs.value("BlueOffset").toInt();
  f.interpolation = parseInterpolation(attrs.value("Interpolated").toString());
  return f;
}

/* --------------------------------------------------------------------------
 * Parse
 * full .anm2 XML
 *
 * ------------------------------------------------------------------------ */

bool anm2_parse_file(const QString& path, Animation& anim,
                     QList<Spritesheet>& spritesheets, QList<LayerDef>& layer_defs,
                     QList<Animation>* all_anims)
{
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
                  ss.id   = xml.attributes().value("Id").toInt();
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
                  ld.id             = xml.attributes().value("Id").toInt();
                  ld.name           = xml.attributes().value("Name").toString();
                  ld.spritesheet_id = xml.attributes().value("SpritesheetId").toInt();
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
          QString defaultAnim = xml.attributes().value("DefaultAnimation").toString();

          while (xml.readNextStartElement()) {
            if (xml.name() == u"Animation") {
              Animation a;
              a.name      = xml.attributes().value("Name").toString();
              a.frame_num = xml.attributes().value("FrameNum").toInt();
              a.loop      = xml.attributes().value("Loop").toString() != "false";

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
                          xml.attributes().value("Visible").toString() != "false";
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
 * FPS
 * reader
 * ------------------------------------------------------------------------ */

int anm2_read_fps(const QString& path)
{
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

/* --------------------------------------------------------------------------
 *
 * Spritesheet loading
 *
 * ------------------------------------------------------------------------ */

static QStringList buildBaseDirs(const QString& base_dir)
{
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

void anm2_load_spritesheets_from_dir(const QString& base_dir,
                                     QList<Spritesheet>& spritesheets)
{
  QStringList base_dirs = buildBaseDirs(base_dir);

  for (auto& ss : spritesheets) {
    QString rel = ss.path;
    rel.replace('\\', '/');
    bool loaded = false;

    if (g_resolve_file) {
      for (const auto& base : base_dirs) {
        QByteArray root_bytes = base.toUtf8();
        QByteArray rel_bytes  = rel.toUtf8();
        char* resolved =
            g_resolve_file(root_bytes.constData(), rel_bytes.constData(), nullptr);
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
      for (const auto& base : base_dirs) {
        QString full = QDir(base).absoluteFilePath(rel);
        if (ss.pixmap.load(full)) {
          loaded = true;
          break;
        }
      }
    }
  }
}

void anm2_load_spritesheets(const QString& anm2_path, QList<Spritesheet>& spritesheets)
{
  anm2_load_spritesheets_from_dir(QFileInfo(anm2_path).absoluteDir().absolutePath(),
                                  spritesheets);
}
