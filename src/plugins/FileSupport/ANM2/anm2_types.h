/**
 * ANM2 Types -- shared data structures for .anm2 animation format.
 *
 * Parsed
 * from The Binding of Isaac: Rebirth's XML-based .anm2 files.
 * Format reference:
 * https://www.moddingofisaac.com/docs/rep/xml/Anm2_files.html
 */

#ifndef ANM2_TYPES_H
#define ANM2_TYPES_H

#include <QList>
#include <QPixmap>
#include <QString>

/* --------------------------------------------------------------------------
 *
 * Interpolation types -- matches anm2ed's on-demand model
 *
 * ------------------------------------------------------------------------ */

enum class Interpolation
{
  NONE,
  LINEAR,
  EASE_IN,
  EASE_OUT,
  EASE_IN_OUT
};

/* --------------------------------------------------------------------------
 * Data
 * structures matching the .anm2 spec
 *
 * ------------------------------------------------------------------------ */

struct Spritesheet
{
  int id = -1;
  QString path;    // relative path from game resources dir
  QPixmap pixmap;  // loaded PNG
};

struct LayerDef
{
  int id = -1;
  QString name;
  int spritesheet_id = -1;
};

struct Anm2Frame
{
  int x_position              = 0;
  int y_position              = 0;
  int x_pivot                 = 0;
  int y_pivot                 = 0;
  int x_crop                  = 0;
  int y_crop                  = 0;
  int width                   = 0;
  int height                  = 0;
  int x_scale                 = 100;
  int y_scale                 = 100;
  int delay                   = 1;  // duration in animation frames
  bool visible                = true;
  int rotation                = 0;  // degrees clockwise
  int red_tint                = 255;
  int green_tint              = 255;
  int blue_tint               = 255;
  int alpha_tint              = 255;
  int red_offset              = 0;
  int green_offset            = 0;
  int blue_offset             = 0;
  Interpolation interpolation = Interpolation::LINEAR;
};

struct LayerAnimation
{
  int layer_id = -1;
  bool visible = true;
  QList<Anm2Frame> frames;
};

struct Animation
{
  QString name;
  int frame_num = 0;
  bool loop     = true;
  Anm2Frame root_frame;  // single base transform
  QList<LayerAnimation> layer_animations;
};

#endif  // ANM2_TYPES_H
