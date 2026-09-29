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
#include <QtGlobal>
#include <cstdint>
#include <map>
#include <utility>

/* --------------------------------------------------------------------------
 *
 * Canvas limits
 *
 * The canvas is sized from the file's own attributes, so a malformed or
 * hostile .anm2 can ask for one that no machine can allocate. The bounds were
 * measured over 25077 real animations whose scale attributes are all within
 * +/-200%: the largest canvas any of them produces is 6020 x 6020 px, and the
 * next largest is 520 x 3020 - a 23x gap, so 6020 is a lone outlier rather
 * than the top of a continuum. 8192 and 2^26 sit above both with room to
 * spare while turning away the 7804 x 17820 canvas that an XScale of 14000
 * produces, and anything the attribute space can express beyond that.
 *
 * ------------------------------------------------------------------------ */

/* Padding the canvas reserves around the measured content box. */
inline constexpr int kAnm2CanvasPad = 10;

/* Largest single canvas edge, in pixels. The pixel budget alone does not bound
 * this: 67108864 x 1 is inside the budget and still past what QPixmap accepts
 * and past any sane display, so the edge is capped in its own right. */
inline constexpr int kAnm2MaxCanvasEdge = 8192;

/* Largest canvas area, in pixels: 2^26 px, which is 256 MiB as RGBA8888. One
 * frame is briefly three of that - the QImage, the convertToFormat copy, and
 * the QPixmap the host builds from it. */
inline constexpr qint64 kAnm2MaxCanvasPixels = 1LL << 26;

/* Longest animation the renderer will work through, in frames.
 *
 * This is the multiplier on every other allocation: the bounds pass steps once
 * per frame, and the host holds one image per frame. The count is a plain
 * attribute in the file, so without a bound a single FrameNum drives all three.
 * The largest count any of 27378 real animations reports is 4522, from a
 * cutscene, and the 99.9th percentile is 441 - so this is 14x clear of the
 * worst real file and still small enough that a hostile one cannot ask for
 * billions of steps. */
inline constexpr int kAnm2MaxTotalFrames = 65536;

/* Whether a canvas of this size may be allocated.
 *
 * False for a non-positive size, so a measurement that was refused can be
 * told apart from a usable one. The product is taken in qint64 because both
 * operands are int: the largest product of two int32 values is 2^62, which
 * qint64 holds and int does not. */
bool anm2_canvas_within_limits(int w, int h);

/* Runtime data container for on-demand rendering. */
struct Anm2RawData {
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
int anm2_compute_total_frames(const Animation &a);

/* Compute the fixed canvas size across all frames using interpolated
 * keyframes. Returns {width, height}.
 *
 * A file whose sprites ask for more than kAnm2MaxCanvasEdge or
 * kAnm2MaxCanvasPixels is not measured, sized, or rendered: every entry point
 * falls back to the size of anm2_oversize_notice() instead, so the size a
 * caller gets back always describes an image it can actually draw. */
std::pair<int, int> anm2_compute_animation_rect(const Animation &a, int default_w,
                                                int default_h);

/* The image returned in place of an animation that asks for a canvas beyond
 * the limits. Drawn rather than returned as a null QImage because a null
 * image reaches the host as an empty frame and the user is left looking at a
 * blank preview with nothing explaining it. */
QImage anm2_oversize_notice();

/* Sum of all keyframe durations in a track, saturating at
 * kAnm2MaxTotalFrames rather than wrapping past it. */
int anm2_track_length_get(const QList<Anm2Frame> &keyframes);

/* Generate an interpolated frame at the given time from a list of keyframes. */
Anm2Frame anm2_frame_generate(const QList<Anm2Frame> &keyframes, float time);

/* Render a single animation frame to QImage at the given time.
 *
 * Returns anm2_oversize_notice() rather than an empty image when the
 * requested canvas, or the animation's own content, exceeds the limits. */
QImage anm2_render_frame_at_time(const Animation &a, const QList<LayerDef> &layer_defs,
                                 std::map<int, QPixmap> &sheet_by_id, float time,
                                 int cw, int ch);

/* On-demand render callback for the ABI.
 * Returns malloc'd RGBA pixels (caller
 * frees). */
uint8_t *anm2_render_frame_cb(void *raw_animation, float time_ms, int32_t *out_width,
                              int32_t *out_height);

#endif  // ANM2_RENDERER_H
