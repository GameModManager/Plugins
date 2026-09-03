/**
 * ANM2 Parser -- .anm2 XML parsing and spritesheet loading.
 *
 * Parses The
 * Binding of Isaac: Rebirth's XML-based .anm2 animation format
 * and loads associated
 * spritesheet PNGs.
 */

#ifndef ANM2_PARSER_H
#define ANM2_PARSER_H

#include "anm2_types.h"

#include <QImage>
#include <QList>
#include <QMap>
#include <QString>

/* Parse a single .anm2 file, populating anim, spritesheets, and layer_defs.
 * If
 * all_anims is non-null, all animations from the file are appended.
 * Returns true if
 * a valid animation was found. */
bool anm2_parse_file(const QString& path, Animation& anim,
                     QList<Spritesheet>& spritesheets, QList<LayerDef>& layer_defs,
                     QList<Animation>* all_anims = nullptr);

/* Read the FPS value from a .anm2 file's <Info> element.
 * Returns 18 if not found or
 * invalid. */
int anm2_read_fps(const QString& path);

/* Load spritesheet PNGs from directories relative to the .anm2 file path.
 * Uses
 * g_resolve_file callback when available. */
void anm2_load_spritesheets(const QString& anm2_path, QList<Spritesheet>& spritesheets);

/* Load spritesheet PNGs from an explicit base directory.
 * Uses g_resolve_file
 * callback when available. */
void anm2_load_spritesheets_from_dir(const QString& base_dir,
                                     QList<Spritesheet>& spritesheets);

/* Set the file resolver callback (from GmmRegistrationCtxV2). */
void anm2_set_resolve_file(void* resolve_file);

#endif  // ANM2_PARSER_H
