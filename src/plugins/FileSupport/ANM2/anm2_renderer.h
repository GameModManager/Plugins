/**
 * ANM2 Renderer -- frame interpolation, canvas computation, QImage rendering.
 *
 *
 * Provides on-demand interpolated frame synthesis and rendering for
 * The Binding of
 * Isaac: Rebirth's .anm2 animation format.
 */

#ifndef ANM2_RENDERER_H
#define ANM2_RENDERER_H

#include "anm2_types.h"

#include <QImage>
#include <QList>
#include <QMap>
#include <QPixmap>
#include <cstdint>
#include <map>
#include <utility>

/* Runtime data container for on-demand rendering. */
struct Anm2RawData
{
  Animation anim;
  QList<Spritesheet> spritesheets;
  QList<LayerDef> layer_defs;
  std::map<int, QPixmap> sheet_by_id;
  int fps           = 18;
  int total_frames  = 0;
  int canvas_width  = 0;
  int canvas_height = 0;
};

/* Compute the total frame count for an animation (sum of keyframe durations
 * or the
 * declared frame_num, whichever is larger). */
int anm2_compute_total_frames(const Animation& a);

/* Compute the fixed canvas size across all frames using interpolated
 * keyframes.
 * Returns {width, height}. */
std::pair<int, int> anm2_compute_animation_rect(const Animation& a, int default_w,
                                                int default_h);

/* Sum of all keyframe durations in a track. */
int anm2_track_length_get(const QList<Anm2Frame>& keyframes);

/* Generate an interpolated frame at the given time from a list of keyframes. */
Anm2Frame anm2_frame_generate(const QList<Anm2Frame>& keyframes, float time);

/* Render a single animation frame to QImage at the given time. */
QImage anm2_render_frame_at_time(const Animation& a, const QList<LayerDef>& layer_defs,
                                 std::map<int, QPixmap>& sheet_by_id, float time,
                                 int cw, int ch);

/* On-demand render callback for the ABI.
 * Returns malloc'd RGBA pixels (caller
 * frees). */
uint8_t* anm2_render_frame_cb(void* raw_animation, float time_ms, int32_t* out_width,
                              int32_t* out_height);

#endif  // ANM2_RENDERER_H
