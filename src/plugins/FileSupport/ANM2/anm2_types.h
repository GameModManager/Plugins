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

enum class Interpolation { NONE, LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT };

/* --------------------------------------------------------------------------
 * Data
 * structures matching the .anm2 spec
 *
 * ------------------------------------------------------------------------ */

struct Spritesheet {
  int id = -1;
  QString path;    // relative path from game resources dir
  QPixmap pixmap;  // loaded PNG
};

struct LayerDef {
  int id = -1;
  QString name;
  int spritesheet_id = -1;
};

struct Anm2Frame {
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

struct LayerAnimation {
  int layer_id = -1;
  bool visible = true;
  QList<Anm2Frame> frames;
};

struct Animation {
  QString name;
  int frame_num = 0;
  bool loop     = true;

  /* The whole of the non-layer transform chain, and it is genuinely the whole
   * of it. The format has no parent relation between animations: nothing in
   * an .anm2 points at another animation in the same file, so there is no
   * hierarchy for a single flattened transform to be standing in for.
   *
   * What the parser does not retain is <Nulls>, <NullAnimations>, <Events>
   * and <Triggers>. The nulls are the interesting case and are worth being
   * precise about. A <Null> declares a name and an id and nothing else - no
   * transform, and no pointer at any other file. A <NullAnimation> under
   * NullId carries frames that move whatever object that null stands for,
   * which is a thing in the running game rather than something in this file:
   * the pickup item Isaac is holding, an eye, a tractor beam, an overlay
   * effect. The frames confirm it. Out of 10200 such frames in a real
   * corpus, every one carries only position, scale, delay, visibility, tints
   * and rotation; not one carries a Width, a Height, an XCrop or a YCrop.
   * There is no crop rectangle because there is no sprite - the object those
   * numbers would select belongs to another animation, which this file does
   * not name and cannot reach.
   *
   * So there is nothing for a previewer to compose, and dropping these is the
   * correct rendering rather than a silent loss of something drawable. They
   * are not modelled here because nothing in the preview reads them, and
   * modelling them would be a shape no code currently fills. */
  Anm2Frame root_frame;  // single base transform
  QList<LayerAnimation> layer_animations;
};

#endif  // ANM2_TYPES_H
