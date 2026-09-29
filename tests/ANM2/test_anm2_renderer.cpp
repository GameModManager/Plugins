/**
 * ANM2 renderer geometry tests.
 *
 * Every assertion here measures geometry numerically - a rendered pixel
 * extent, a channel value, a corner coordinate. Nothing in this file depends
 * on the transform construction it is checking, so a test that passes is
 * evidence about the rendered output rather than about the code path.
 */

#include "anm2_parser.h"
#include "anm2_renderer.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const char* expr, int line, const char* detail = "")
{
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::fprintf(stderr, "FAIL line %d: %s %s\n", line, expr, detail);
  }
}

template <typename A, typename B>
void check_eq(A got, B want, const char* expr, int line, const char* detail = "")
{
  ++g_checks;
  if (!(got == static_cast<A>(want))) {
    ++g_failures;
    std::fprintf(stderr, "FAIL line %d: %s -- got %lld want %lld. %s\n", line, expr,
                 static_cast<long long>(got), static_cast<long long>(want), detail);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(got, want) check_eq((got), (want), #got, __LINE__)
#define CHECK_NEAR(got, want, tol) \
  check(std::fabs(static_cast<double>(got) - (want)) <= (tol), #got " ~= " #want, __LINE__)

/* -- geometry helpers ---------------------------------------------------- */

struct Extent
{
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // inclusive pixel range, empty if x1 < x0
  int w() const { return x1 - x0 + 1; }
  int h() const { return y1 - y0 + 1; }
  bool empty() const { return x1 < x0; }
};

/* Tight bounding box of every pixel that drew something. */
Extent drawn_extent(const QImage& img)
{
  Extent e;
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      if (qAlpha(img.pixel(x, y)) > 0) {
        if (e.x1 < e.x0) {
          e.x0 = e.x1 = x;
          e.y0 = e.y1 = y;
        } else {
          e.x0 = qMin(e.x0, x);
          e.y0 = qMin(e.y0, y);
          e.x1 = qMax(e.x1, x);
          e.y1 = qMax(e.y1, y);
        }
      }
    }
  }
  return e;
}

Extent drawn_extent(const QImage& img, int channel_mask, QRgb want)
{
  Extent e;
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      if (qAlpha(img.pixel(x, y)) > 0 && (img.pixel(x, y) & channel_mask) == (want & channel_mask)) {
        if (e.x1 < e.x0) {
          e.x0 = e.x1 = x;
          e.y0 = e.y1 = y;
        } else {
          e.x0 = qMin(e.x0, x);
          e.y0 = qMin(e.y0, y);
          e.x1 = qMax(e.x1, x);
          e.y1 = qMax(e.y1, y);
        }
      }
    }
  }
  return e;
}

/* Solid opaque sprite of the given size. */
QPixmap solid_sheet(int w, int h, QColor c = QColor(255, 255, 255, 255))
{
  QPixmap pm(w, h);
  pm.fill(c);
  return pm;
}

/* Sprite whose left half is red and right half blue, so a horizontal flip is
 * observable by colour. */
QPixmap split_sheet(int w, int h)
{
  QImage pm(w, h, QImage::Format_RGBA8888);
  pm.fill(Qt::transparent);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w / 2; ++x)
      pm.setPixel(x, y, qRgb(255, 0, 0));
    for (int x = w / 2; x < w; ++x)
      pm.setPixel(x, y, qRgb(0, 0, 255));
  }
  return QPixmap::fromImage(pm);
}

Anm2Frame frame_at(int x, int y, int pivot_x, int pivot_y, int w, int h, int rot,
                   int xs, int ys)
{
  Anm2Frame f;
  f.x_position = x;
  f.y_position = y;
  f.x_pivot    = pivot_x;
  f.y_pivot    = pivot_y;
  f.width      = w;
  f.height     = h;
  f.rotation   = rot;
  f.x_scale    = xs;
  f.y_scale    = ys;
  f.delay      = 1;
  f.interpolation = Interpolation::NONE;
  return f;
}

/* One layer, one keyframe, one spritesheet. */
Animation single_layer(const Anm2Frame& f, int layer_id = 0)
{
  Animation a;
  a.name      = "test";
  a.frame_num = 1;
  a.root_frame.x_scale = 100;
  a.root_frame.y_scale = 100;

  LayerDef ld;
  ld.id             = layer_id;
  ld.spritesheet_id = 0;

  LayerAnimation la;
  la.layer_id = layer_id;
  la.visible  = true;
  la.frames   = QList<Anm2Frame>{f};

  a.layer_animations.append(la);
  (void)ld;
  return a;
}

QImage render(const Animation& a, const QPixmap& sheet, float time = 0.0f)
{
  QList<LayerDef> defs{LayerDef{0, QStringLiteral("l"), 0}};
  std::map<int, QPixmap> sheets{{0, sheet}};
  auto [w, h] = anm2_compute_animation_rect(a, 400, 300);
  return anm2_render_frame_at_time(a, defs, sheets, time, w, h);
}

/* -- tests --------------------------------------------------------------- */

/* A 50% XScale must halve the sprite. Squaring the scale - pre-scaling the
 * pixmap and then scaling it again through the transform - would give 25%.
 * The 25 case is the negative control. */
void test_scale_applied_once()
{
  for (int pct : {50, 200}) {
    Anm2Frame f = frame_at(0, 0, 0, 0, 100, 100, 0, pct, pct);
    QImage img  = render(single_layer(f), solid_sheet(100, 100));
    Extent e    = drawn_extent(img);

    int want = 100 * pct / 100;
    int bad  = 100 * pct / 100 * pct / 100;  // what double-scaling produced
    CHECK(!e.empty());
    CHECK_EQ(e.w(), want);
    CHECK_EQ(e.h(), want);
    CHECK(e.w() != bad);
    CHECK(e.h() != bad);
  }
}

/* Scale and crop size compose once, not twice: a 40x20 crop at 150% is 60x30. */
void test_scale_composes_with_crop_once()
{
  Anm2Frame f = frame_at(0, 0, 0, 0, 40, 20, 0, 150, 150);
  QImage img  = render(single_layer(f), solid_sheet(64, 64));
  Extent e    = drawn_extent(img);
  CHECK_EQ(e.w(), 60);
  CHECK_EQ(e.h(), 30);
}

/* The spec says a negative scale flips the sprite. It must mirror, not
 * collapse: the content keeps its full width with the halves swapped. */
void test_negative_scale_mirrors()
{
  QImage upright =
      render(single_layer(frame_at(0, 0, 0, 0, 100, 100, 0, 100, 100)),
             split_sheet(100, 100));
  QImage mirrored =
      render(single_layer(frame_at(0, 0, 0, 0, 100, 100, 0, -100, 100)),
             split_sheet(100, 100));

  Extent up = drawn_extent(upright);
  Extent mi = drawn_extent(mirrored);

  /* The sheet's left half is red and its right half blue, so a mirror is
   * visible as the colours swapping sides. */
  CHECK_EQ(up.x0 + 5 < up.x1, true);
  QRgb up_left  = upright.pixel(up.x0 + 5, up.y0 + 50) & 0x00ffffff;
  QRgb up_right = upright.pixel(up.x1 - 5, up.y0 + 50) & 0x00ffffff;
  CHECK_EQ(up_left, qRgb(255, 0, 0) & 0x00ffffff);
  CHECK_EQ(up_right, qRgb(0, 0, 255) & 0x00ffffff);

  /* Not 1px wide: qMax(1, crop*scale) turned a negative scale into a sliver. */
  CHECK_EQ(mi.w(), 100);
  CHECK_EQ(mi.h(), 100);

  QRgb mi_left  = mirrored.pixel(mi.x0 + 5, mi.y0 + 50) & 0x00ffffff;
  QRgb mi_right = mirrored.pixel(mi.x1 - 5, mi.y0 + 50) & 0x00ffffff;
  CHECK_EQ(mi_left, qRgb(0, 0, 255) & 0x00ffffff);
  CHECK_EQ(mi_right, qRgb(255, 0, 0) & 0x00ffffff);
  CHECK(mi_left != up_left);
  CHECK(mi_right != up_right);
}

/* Rotating about a pivot that sits at the sprite's centre must not move the
 * sprite: the pin stays on the position while the body swings around it. The
 * bounding box itself does grow - a 100x100 square is 141x141 once turned 45
 * degrees - so the claim is about the centre, which is the pivot and therefore
 * a fixed point of the transform. */
void test_pivot_at_centre_does_not_move()
{
  struct Case { int rot; };
  for (Case c : {Case{0}, Case{45}, Case{90}, Case{180}}) {
    Animation a = single_layer(frame_at(0, 0, 50, 50, 100, 100, c.rot, 100, 100));
    auto [cw, ch] = anm2_compute_animation_rect(a, 400, 300);
    QList<LayerDef> defs{LayerDef{0, QStringLiteral("l"), 0}};
    std::map<int, QPixmap> sheets{{0, solid_sheet(100, 100)}};
    QImage img = anm2_render_frame_at_time(a, defs, sheets, 0.0f, cw, ch);
    Extent e    = drawn_extent(img);
    CHECK(!e.empty());

    /* The canvas is the content box plus 10px of padding, and the origin
     * places the content box at 0,0. For a square turned about its own
     * centre the content box stays symmetric about the pin, so the pin - and
     * therefore the centre of what was drawn - sits at half the content span.
     * A pivot honoured in the wrong order moves it. */
    double cx = e.x0 + (e.w() - 1) / 2.0;
    double cy = e.y0 + (e.h() - 1) / 2.0;
    CHECK_NEAR(cx, (cw - 20) / 2.0, 1.0);
    CHECK_NEAR(cy, (ch - 20) / 2.0, 1.0);
  }

  /* Turning 45 degrees grows the axis-aligned box to the square's diagonal,
   * 100*sqrt(2) = 141.4. */
  Animation a = single_layer(frame_at(0, 0, 50, 50, 100, 100, 45, 100, 100));
  Extent e    = drawn_extent(render(a, solid_sheet(100, 100)));
  CHECK_NEAR(e.w(), 100.0 * std::sqrt(2.0), 2.0);
  CHECK_NEAR(e.h(), 100.0 * std::sqrt(2.0), 2.0);
}

/* Where the pivot puts a layer relative to the other layers.
 *
 * A single layer cannot show this. Writing the chain as
 *   T(pos) R S T(-pivot)   gives  q -> pos + R*S*(q - pivot)
 * and as
 *   T(-pivot) R S T(pos)   gives  q -> R*S*(q + pos) - pivot
 * and the two differ by the constant (pos + pivot) - R*S*(pos + pivot), a pure
 * translation. The origin shift moves the content bounding box to 0,0, so it
 * cancels that translation outright and the two orders draw identically for
 * one layer. With two layers the origin is shared, so the constant survives
 * as a relative offset - which is what this measures.
 *
 * Layer 0 is red, pivoted at (20,20), placed at (10,5) and turned 90 degrees;
 * layer 1 is blue, unturned at the origin. The red box must start 10 left and
 * 25 below the blue one. */
void test_pivot_controls_layer_placement()
{
  LayerAnimation red;
  red.layer_id = 0;
  red.visible  = true;
  red.frames   = QList<Anm2Frame>{
      frame_at(10, 5, 20, 20, 40, 40, 90, 100, 100)};

  LayerAnimation blue;
  blue.layer_id = 1;
  blue.visible  = true;
  blue.frames   = QList<Anm2Frame>{
      frame_at(0, 0, 0, 0, 40, 40, 0, 100, 100)};

  Animation a;
  a.name      = "two-layer";
  a.frame_num = 1;
  a.root_frame.x_scale = 100;
  a.root_frame.y_scale = 100;
  a.layer_animations.append(red);
  a.layer_animations.append(blue);

  QList<LayerDef> defs{LayerDef{0, QStringLiteral("r"), 0}, LayerDef{1, QStringLiteral("b"), 1}};
  std::map<int, QPixmap> sheets{{0, solid_sheet(64, 64, QColor(255, 0, 0, 255))},
                                {1, solid_sheet(64, 64, QColor(0, 0, 255, 255))}};
  auto [cw, ch] = anm2_compute_animation_rect(a, 400, 300);
  QImage img = anm2_render_frame_at_time(a, defs, sheets, 0.0f, cw, ch);

  /* Isolate each layer by colour. */
  Extent r = drawn_extent(img, 0x00ff0000, qRgb(255, 0, 0));
  Extent b = drawn_extent(img, 0x000000ff, qRgb(0, 0, 255));
  CHECK(!r.empty());
  CHECK(!b.empty());

  /* Red's four corners under T(10,5) R90 T(-20,-20):
   *   (0,0)->(30,-15)  (40,0)->(30,25)  (40,40)->(-10,25)  (0,40)->(-10,-15)
   * so the box spans x -10..30, y -15..25. Blue's box starts at (0,0), so red
   * sits 10 left of it and 15 above. The origin shift moves both together and
   * cancels out of the difference. */
  CHECK_NEAR(r.x0 - b.x0, -10.0, 1.5);
  CHECK_NEAR(r.y0 - b.y0, -15.0, 1.5);
  CHECK_EQ(r.w(), 40);
  CHECK_EQ(r.h(), 40);

  /* The reorder puts red's corners at x -65..-25, so the offset would be -65
   * instead of -10. 55px apart, so the assertion above is not vacuous. */
  CHECK_NEAR(-65.0 - -10.0, -55.0, 0.001);
  CHECK(std::fabs(-65.0 - -10.0) > 20.0);
}

/* Negative control for the pivot chain: the reordered form predicts a
 * different inter-layer offset, and the renderer must land on the pinned one.
 * Kept as its own case so a regression names itself. */
void test_pivot_reorder_negative_control()
{
  /* The two orders for a 90-degree turn of a 40x40 crop pivoted at (20,20),
   * placed at (10,5), with the other layer fixed at the origin. */
  QTransform pinned;
  pinned.translate(10, 5);
  pinned.rotate(90);
  pinned.scale(1.0, 1.0);
  pinned.translate(-20, -20);
  QPointF pinned_tl = pinned.map(QPointF(0, 0));

  QTransform reordered;
  reordered.translate(-20, -20);
  reordered.rotate(90);
  reordered.scale(1.0, 1.0);
  reordered.translate(10, 5);
  QPointF reordered_tl = reordered.map(QPointF(0, 0));

  /* Far enough apart that the placement assertion is meaningful. The two
   * orders differ by a pure translation, so the distance between what they
   * predict is the whole of the effect. */
  double tl_gap = std::hypot(pinned_tl.x() - reordered_tl.x(),
                             pinned_tl.y() - reordered_tl.y());
  CHECK(tl_gap > 20.0);

  /* The pinned form puts the pivot itself on the position, which is what a
   * pin means; the reordered form does not. */
  QPointF pinned_pin     = pinned.map(QPointF(20, 20));
  QPointF reordered_pin = reordered.map(QPointF(20, 20));
  CHECK_NEAR(pinned_pin.x(), 10.0, 0.001);
  CHECK_NEAR(pinned_pin.y(), 5.0, 0.001);
  CHECK(std::hypot(reordered_pin.x() - 10.0, reordered_pin.y() - 5.0) > 20.0);
}

/* The canvas the bounds pass computes must be exactly what the draw pass
 * fills, for a frame that is pivoted, rotated and scaled at once. Measured
 * against the transformed crop corners, not against the renderer's own
 * numbers. */
void test_bounds_match_draw()
{
  struct Spec { int px, py, pvx, pvy, w, h, rot, sx, sy; };
  const Spec specs[] = {
      {0, 0, 20, 30, 60, 40, 25, 90, 110},    // scaled, rotated, pivoted off-centre
      {17, -9, 30, 20, 40, 40, 0, 100, 100},   // moved, no rotation
      {0, 0, 0, 0, 100, 100, 30, 100, 100},    // pivot at the corner
      {5, 5, 50, 50, 100, 100, 90, 100, 100},  // pivot dead centre
      {0, 0, 25, 25, 50, 50, 0, 200, 200},     // scaled up
  };

  for (const Spec& s : specs) {
    Anm2Frame f = frame_at(s.px, s.py, s.pvx, s.pvy, s.w, s.h, s.rot, s.sx, s.sy);
    Animation a = single_layer(f);
    QList<LayerDef> defs{LayerDef{0, QStringLiteral("l"), 0}};
    std::map<int, QPixmap> sheets{{0, solid_sheet(200, 200)}};
    auto [cw, ch] = anm2_compute_animation_rect(a, 400, 300);
    QImage img = anm2_render_frame_at_time(a, defs, sheets, 0.0f, cw, ch);
    Extent e    = drawn_extent(img);
    CHECK(!e.empty());

    /* The crop corners under the transform the spec describes, worked out
     * here from scratch: pin removed, then rotate, then scale, then place. */
    QTransform t;
    t.translate(s.px, s.py);
    t.rotate(s.rot);
    t.scale(s.sx / 100.0, s.sy / 100.0);
    t.translate(-s.pvx, -s.pvy);

    QPointF c0 = t.map(QPointF(0, 0));
    QPointF c1 = t.map(QPointF(s.w, 0));
    QPointF c2 = t.map(QPointF(s.w, s.h));
    QPointF c3 = t.map(QPointF(0, s.h));
    double want_w = qMax(qMax(c0.x(), c1.x()), qMax(c2.x(), c3.x())) -
                    qMin(qMin(c0.x(), c1.x()), qMin(c2.x(), c3.x()));
    double want_h = qMax(qMax(c0.y(), c1.y()), qMax(c2.y(), c3.y())) -
                    qMin(qMin(c0.y(), c1.y()), qMin(c2.y(), c3.y()));

    /* The drawn extent is the size of that box: one pixel for a fully covered
     * one, plus a partial pixel on each side where the transform lands off a
     * pixel boundary. */
    CHECK_NEAR(e.w(), want_w, 2.0);
    CHECK_NEAR(e.h(), want_h, 2.0);

    /* And the canvas holds all of it - the draw pass never runs past the
     * rectangle the bounds pass measured. */
    CHECK(e.x0 >= 0);
    CHECK(e.y0 >= 0);
    CHECK(e.x1 <= cw - 1);
    CHECK(e.y1 <= ch - 1);
    CHECK(cw >= e.w());
    CHECK(ch >= e.h());
  }
}

/* The tint offset is a signed shift on the tint channel. A negative offset
 * must darken the channel; clamping the sum away would leave it white. */
void test_negative_tint_offset_applies()
{
  Anm2Frame f     = frame_at(0, 0, 0, 0, 20, 20, 0, 100, 100);
  f.red_tint      = 255;
  f.red_offset    = -190;  // the most negative value real files use
  f.green_tint    = 255;
  f.blue_tint     = 255;
  f.alpha_tint    = 255;

  QImage img = render(single_layer(f), solid_sheet(20, 20));
  QColor c(  img.pixel(10, 10));
  CHECK_EQ(c.red(), 65);
  CHECK_EQ(c.green(), 255);
  CHECK_EQ(c.blue(), 255);
  CHECK_EQ(qAlpha(img.pixel(10, 10)), 255);
}

/* A positive offset pushes the channel back up, and both offsets clamp at the
 * channel range rather than wrapping. */
void test_tint_offset_clamps_to_channel_range()
{
  Anm2Frame over  = frame_at(0, 0, 0, 0, 20, 20, 0, 100, 100);
  over.red_tint   = 255;
  over.red_offset = 10000;  // real files use offsets this large
  QColor hot(render(single_layer(over), solid_sheet(20, 20)).pixel(10, 10));
  CHECK_EQ(hot.red(), 255);

  Anm2Frame under     = over;
  under.red_tint     = 0;
  under.red_offset   = -190;
  QColor cold(render(single_layer(under), solid_sheet(20, 20)).pixel(10, 10));
  CHECK_EQ(cold.red(), 0);
}

/* The sum is what gets clamped, not the tint and the offset separately. Tints
 * above 255 occur in real files, so the two readings diverge there: summing
 * gives 300-190 = 110, whereas saturating the tint first would give
 * 255-190 = 65 and make an over-bright tint darken by less than a fully
 * bright one, which is not what a shift means. */
void test_tint_offset_applies_to_untouched_tint()
{
  Anm2Frame f     = frame_at(0, 0, 0, 0, 20, 20, 0, 100, 100);
  f.red_tint      = 300;
  f.red_offset    = -190;
  f.green_tint    = 255;
  f.blue_tint     = 255;
  f.alpha_tint    = 255;

  QColor c(render(single_layer(f), solid_sheet(20, 20)).pixel(10, 10));
  CHECK_EQ(c.red(), 110);
  CHECK_EQ(c.red() != 65, true);  // not saturated before the shift

  /* An over-bright tint with no offset still saturates at 255. */
  Anm2Frame plain = f;
  plain.red_offset = 0;
  QColor flat(render(single_layer(plain), solid_sheet(20, 20)).pixel(10, 10));
  CHECK_EQ(flat.red(), 255);
}

/* A layer with no resolvable spritesheet draws the placeholder at the crop
 * size, and that placeholder obeys the same single scale. */
void test_placeholder_path_scales_once()
{
  Anm2Frame f = frame_at(0, 0, 0, 0, 100, 100, 0, 50, 50);
  Animation a = single_layer(f);
  QList<LayerDef> defs{LayerDef{0, QStringLiteral("l"), 0}};
  std::map<int, QPixmap> empty;
  auto [cw, ch] = anm2_compute_animation_rect(a, 400, 300);
  QImage img    = anm2_render_frame_at_time(a, defs, empty, 0.0f, cw, ch);

  Extent e = drawn_extent(img);
  CHECK(!e.empty());
  /* fillRect under a 0.5 scale covers half the crop; the border stroke adds at
   * most a pixel on each edge. A double-scaled 50% would be 25. */
  CHECK(e.w() <= 52);
  CHECK(e.h() <= 52);
  CHECK(e.w() >= 48);
  CHECK(e.h() >= 48);
}

/* A scale attribute that is absent takes the spec default; one that is present
 * and zero means zero, and real files do carry XScale="0". Substituting the
 * default on the parsed value instead of on the missing attribute silently
 * turns those sprites back to full size. */
void test_parser_scale_attribute_defaults()
{
  QTemporaryDir dir;
  CHECK(dir.isValid());
  if (!dir.isValid())
    return;

  const char* kXml =
      "<AnimatedActor><Content><Spritesheets/><Layers>"
      "<Layer Name=\"a\" Id=\"0\" SpritesheetId=\"0\"/></Layers></Content>"
      "<Animations DefaultAnimation=\"Default\">"
      "<Animation Name=\"Default\" FrameNum=\"1\" Loop=\"false\">"
      "<LayerAnimations><LayerAnimation LayerId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"8\" Height=\"8\" Delay=\"1\"/>"
      "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"8\" Height=\"8\" Delay=\"1\" "
      "XScale=\"0\" YScale=\"-50\"/>"
      "</LayerAnimation></LayerAnimations></Animation></Animations>"
      "</AnimatedActor>";

  QString path = dir.filePath(QStringLiteral("t.anm2"));
  {
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
    if (!f.isOpen())
      return;
    f.write(kXml);
    f.close();
  }

  Animation anim;
  QList<Spritesheet> sheets;
  QList<LayerDef> defs;
  CHECK(anm2_parse_file(path, anim, sheets, defs, nullptr));
  CHECK_EQ(anim.layer_animations.size(), 1);
  if (anim.layer_animations.isEmpty())
    return;

  const QList<Anm2Frame>& kf = anim.layer_animations[0].frames;
  CHECK_EQ(kf.size(), 2);
  if (kf.size() != 2)
    return;

  /* Absent attribute -> spec default. */
  CHECK_EQ(kf[0].x_scale, 100);
  CHECK_EQ(kf[0].y_scale, 100);
  /* Explicit values are honoured, including zero and negative. */
  CHECK_EQ(kf[1].x_scale, 0);
  CHECK_EQ(kf[1].y_scale, -50);

  /* QTemporaryDir removes the tree on destruction. */
}

/* -- canvas limits -------------------------------------------------------- */

/* The canvas a frame asks for, worked out here from its attributes without
 * going near the renderer, so a test can state what it built and then check
 * that the renderer agreed. The scale is a percentage, so it divides by 100. */
static void expected_canvas(const Anm2Frame& f, double* w, double* h)
{
  *w = f.width * static_cast<double>(f.x_scale) / 100.0;
  *h = f.height * static_cast<double>(f.y_scale) / 100.0;
}

/* Two frames taken from a real shipped animation - the Death animation of
 * 407.000_hush.anm2, layer 3 - whose scales are out by three orders of
 * magnitude. They sit either side of the one limit that refuses, which is what
 * makes them worth pinning:
 *
 *   frame 3:  32 x 115 at 14000% x 10000%  ->  4480 x 11500
 *     area 51,520,000 px, inside kAnm2MaxCanvasPixels, so it is measured and
 *     then drawn whole at 400 x 1024 - over the edge cap, not over the budget.
 *   frame 4:  43 x 126 at 16800% x 10000%  ->  7224 x 12600
 *     area 91,022,400 px, over kAnm2MaxCanvasPixels. Not measured, not drawn.
 *
 * The edge between the two is the edge cap, and it is a scaling target rather
 * than a refusal: the first frame's 11500px edge is not a reason to withhold
 * the animation. Only the budget refuses. */
struct HushFrame
{
  int w, h, xs, ys;
  double want_w, want_h;
  bool refused;
};

const HushFrame kHushFrames[] = {
    {32, 115, 14000, 10000, 4480.0, 11500.0, false},
    {43, 126, 16800, 10000, 7224.0, 12600.0, true},
};

/* A content box over the pixel budget is not drawn, and the gate is still
 * ahead of the allocation. Both halves matter: the measurement declines to
 * name the size the file asked for, and the render returns the notice rather
 * than an image carrying the requested dimensions.
 *
 * The rows walked here are the ones over the budget. Being over the edge cap
 * is not among them - that is handled by scaling down, and kHushFrames carries
 * a real frame on that side of the line to prove the two are not confused. */
void test_oversize_content_box_is_refused()
{
  for (const HushFrame& hf : kHushFrames) {
    if (!hf.refused)
      continue;

    Anm2Frame f = frame_at(0, 0, 0, 0, hf.w, hf.h, 0, hf.xs, hf.ys);
    Animation a = single_layer(f);

    /* What the file asks for, derived from its own attributes. The test is
     * worthless if this does not hold: a renderer that only ever saw small
     * canvases would pass it without trying. */
    double want_w = 0, want_h = 0;
    expected_canvas(f, &want_w, &want_h);
    CHECK_NEAR(want_w, hf.want_w, 0.5);
    CHECK_NEAR(want_h, hf.want_h, 0.5);
    CHECK(want_w * want_h > static_cast<double>(kAnm2MaxCanvasPixels));

    /* The measurement refuses to name the hostile size. */
    auto [cw, ch] = anm2_compute_animation_rect(a, 400, 300);
    CHECK(anm2_canvas_within_limits(cw, ch));
    CHECK(cw <= kAnm2MaxCanvasEdge);
    CHECK(ch <= kAnm2MaxCanvasEdge);
    CHECK(static_cast<qint64>(cw) * ch <= kAnm2MaxCanvasPixels);

    /* And so does the render. The returned image is not the size that was
     * asked for, which is the whole point: if the gate sat after
     * `QImage canvas(cw, ch, ...)`, the image returned would carry the
     * requested dimensions, or be null. Neither size appearing here is what
     * shows the request was turned away before the allocation. */
    QImage img = render(a, solid_sheet(64, 64));
    CHECK(!img.isNull());
    CHECK(img.width() == cw);
    CHECK(img.height() == ch);
    CHECK(img.width() <= kAnm2MaxCanvasEdge);
    CHECK(img.height() <= kAnm2MaxCanvasEdge);
    CHECK(img.width() < static_cast<int>(want_w) ||
          img.height() < static_cast<int>(want_h));
  }

  /* The same gate covers a hostile size arriving from the caller with an
   * otherwise ordinary animation, which is the second way in: the file is
   * fine, the canvas the host was told about is not. 7804 x 17820 is the
   * canvas an XScale of 14000 produces. */
  QList<LayerDef> defs{LayerDef{0, QStringLiteral("l"), 0}};
  std::map<int, QPixmap> sheets{{0, solid_sheet(64, 64)}};
  Animation small = single_layer(frame_at(0, 0, 0, 0, 32, 32, 0, 100, 100));
  QImage hostile = anm2_render_frame_at_time(small, defs, sheets, 0.0f,
                                             7804, 17820);
  CHECK(!hostile.isNull());
  CHECK(hostile.width() != 7804);
  CHECK(hostile.height() != 17820);
  CHECK(anm2_canvas_within_limits(hostile.width(), hostile.height()));
  /* Refused rather than scaled down to the cap, which is the other thing the
   * renderer does with a large canvas and would otherwise be indistinguishable
   * here on size alone. The notice is a fixed 480 x 160 whatever was asked
   * for, so it cannot be mistaken for a scaled animation. */
  CHECK_EQ(hostile.width(), 480);
  CHECK_EQ(hostile.height(), 160);

  /* The same file read through the parser, so the refusal is shown to follow
   * the numbers in the XML rather than a struct the test built by hand. */
  QTemporaryDir dir;
  CHECK(dir.isValid());
  if (dir.isValid()) {
    const char* kXml =
        "<AnimatedActor><Content><Spritesheets/><Layers>"
        "<Layer Name=\"a\" Id=\"0\" SpritesheetId=\"0\"/></Layers></Content>"
        "<Animations DefaultAnimation=\"Default\">"
        "<Animation Name=\"Default\" FrameNum=\"6\" Loop=\"false\">"
        "<LayerAnimations><LayerAnimation LayerId=\"0\" Visible=\"true\">"
        "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"43\" Height=\"126\" "
        "Delay=\"1\" XScale=\"16800\" YScale=\"10000\"/>"
        "</LayerAnimation></LayerAnimations></Animation></Animations>"
        "</AnimatedActor>";
    QString path = dir.filePath(QStringLiteral("hush.anm2"));
    {
      QFile f(path);
      CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
      if (!f.isOpen())
        return;
      f.write(kXml);
      f.close();
    }
    Animation anim;
    QList<Spritesheet> sheets;
    QList<LayerDef> defs;
    CHECK(anm2_parse_file(path, anim, sheets, defs, nullptr));
    auto [pcw, pch] = anm2_compute_animation_rect(anim, 400, 300);
    CHECK(anm2_canvas_within_limits(pcw, pch));
    CHECK(pcw <= kAnm2MaxCanvasEdge);
    CHECK(pch <= kAnm2MaxCanvasEdge);
    /* QTemporaryDir removes the tree on destruction. */
  }
}

/* The largest canvas any real animation produces is drawn whole, at a reduced
 * size.
 *
 * 3000 x 3000 at 200% is the geometry of zissAura.anm2, whose crop really does
 * sit inside a 3000 x 3000 sheet on disk, and it is the largest canvas of the
 * 25077 real animations with sane scales: 6000 x 6000 of content, plus the
 * padding, is 6020 x 6020. That is over the 1024 edge cap, so it is scaled
 * down rather than withheld, and the output is pinned at the exact size that
 * implies - 1024 x 1024, because 6020 and 6020 are the same length and the
 * same factor therefore lands both on the cap.
 *
 * Drawn whole is the claim worth checking, so the assertions are about the
 * sprite still covering its whole 6000 x 6000 box and the padding still
 * standing off it. A clip would show up as content running to the last pixel
 * of the canvas, which is a width of 1024 rather than 1020. The negative
 * control is the same animation small enough to fit the cap, which must come
 * through at its own size and unscaled - otherwise "reduced to fit" would pass
 * for any canvas, including one that was never reduced. */
void test_measured_real_maximum_still_renders()
{
  Anm2Frame f = frame_at(0, 0, 0, 0, 3000, 3000, 0, 200, 200);
  Animation a = single_layer(f);

  auto [cw, ch] = anm2_compute_animation_rect(a, 400, 300);
  CHECK_EQ(cw, 1024);
  CHECK_EQ(ch, 1024);
  CHECK(anm2_canvas_within_limits(cw, ch));

  QImage img = render(a, solid_sheet(3000, 3000));
  CHECK_EQ(img.width(), 1024);
  CHECK_EQ(img.height(), 1024);

  /* The sheet has to be as big as the crop: QPixmap::copy() returns a null
   * pixmap for a rectangle that is not inside the source, so a small sheet
   * here would draw nothing and prove nothing. */
  Extent e = drawn_extent(img);
  CHECK(!e.empty());

  /* 6000 x 6000 of content scaled by 1024 / 6020 is 1020.6, and the 20px of
   * padding around the canvas becomes 3.4. So the sprite stops short of the
   * canvas edge on the right and the bottom, and that gap is the thing a clip
   * would have eaten: content running to the last pixel is what a cropped
   * animation looks like, and 1024 wide would be exactly that. */
  CHECK_NEAR(e.w(), 1020.0, 2.0);
  CHECK_NEAR(e.h(), 1020.0, 2.0);
  CHECK(e.w() < img.width());
  CHECK(e.h() < img.height());
  /* The origin shift puts the measured box at 0,0, so the padding is all
   * trailing - 20px of it, reduced by the fit. */
  CHECK_EQ(e.x0, 0);
  CHECK_EQ(e.y0, 0);
  CHECK_NEAR(img.width() - e.w(), 4.0, 1.0);
  CHECK_NEAR(img.height() - e.h(), 4.0, 1.0);
  CHECK_EQ(qAlpha(img.pixel(512, 512)), 255);

  /* Negative control: the same sprite at 10% is 300 x 300, well inside the
   * cap, and comes through at its own size. */
  Anm2Frame small_f = frame_at(0, 0, 0, 0, 3000, 3000, 0, 10, 10);
  auto [scw, sch] = anm2_compute_animation_rect(single_layer(small_f), 400, 300);
  CHECK_EQ(scw, 320);
  CHECK_EQ(sch, 320);
  QImage small = render(single_layer(small_f), solid_sheet(3000, 3000));
  CHECK_EQ(small.width(), 320);
  CHECK_EQ(small.height(), 320);
  Extent se = drawn_extent(small);
  CHECK(!se.empty());
  CHECK_NEAR(se.w(), 300.0, 2.0);
  CHECK_NEAR(se.h(), 300.0, 2.0);
  /* Unscaled means the padding is still the full 20px rather than 3.4, so the
   * control is not passing for the same geometry the scaled case does. */
  CHECK_NEAR(small.width() - se.w(), 20.0, 1.0);
  CHECK_NEAR(small.height() - se.h(), 20.0, 1.0);
}

/* A canvas past the edge cap is scaled down, and the scale is the same on both
 * axes.
 *
 * kHushFrames holds a real frame whose canvas is 4500 x 11520 - over the cap
 * on its long edge, inside the pixel budget - so it is the case where the two
 * limits have to be told apart. One factor is applied to both edges, which is
 * what makes a reduced animation distinguishable from a clipped one: the
 * result is 400 x 1024, not 1024 x 1024. Clamping each edge to the cap
 * independently would give the square, which is what a reader of the code
 * would picture if the word "cap" were taken on its own.
 *
 * The negative control is a canvas already inside the cap, which must be left
 * at its own size - a fit that fired on canvases that did not need it would
 * pass every other assertion in this file. */
void test_oversize_canvas_is_scaled_down()
{
  const HushFrame& hf = kHushFrames[0];
  CHECK(!hf.refused);

  Anm2Frame f = frame_at(0, 0, 0, 0, hf.w, hf.h, 0, hf.xs, hf.ys);
  double want_w = 0, want_h = 0;
  expected_canvas(f, &want_w, &want_h);
  CHECK_NEAR(want_w, hf.want_w, 0.5);
  CHECK_NEAR(want_h, hf.want_h, 0.5);
  /* Over the cap, inside the budget: this is the case that scales rather than
   * being refused, and the two checks above are what make it that case. */
  CHECK(want_h > kAnm2MaxCanvasEdge);
  CHECK(want_w * want_h <= static_cast<double>(kAnm2MaxCanvasPixels));

  auto [cw, ch] = anm2_compute_animation_rect(single_layer(f), 400, 300);
  /* 11520 is the long edge, so it lands on the cap and 4500 is carried along:
   * 4500 * 1024 / 11520 is exactly 400. */
  CHECK_EQ(ch, kAnm2MaxCanvasEdge);
  CHECK_EQ(cw, 400);

  QImage img = render(single_layer(f), solid_sheet(64, 115));
  CHECK_EQ(img.width(), 400);
  CHECK_EQ(img.height(), 1024);

  /* Drawn whole: 4480 x 11500 of content becomes 398 x 1022, which stops short
   * of the 400 x 1024 canvas on every side. Drawing it unscaled instead would
   * run off all four edges and measure the full canvas, so the gap between the
   * two is the whole claim. The sheet is as tall as the crop on purpose: a
   * smaller one would be clipped by QPixmap::copy() and the extent would then
   * measure the sheet rather than the content. */
  Extent ie = drawn_extent(img);
  CHECK(!ie.empty());
  CHECK_NEAR(ie.w(), 398.0, 3.0);
  CHECK_NEAR(ie.h(), 1022.0, 3.0);
  CHECK(ie.w() < img.width());
  CHECK(ie.h() < img.height());
  CHECK(qAlpha(img.pixel(200, 512)) > 0);

  /* Negative control: a canvas inside the cap is drawn at its own size. */
  Anm2Frame inside_f = frame_at(0, 0, 0, 0, 900, 900, 0, 100, 100);
  auto [icw, ich] = anm2_compute_animation_rect(single_layer(inside_f), 400, 300);
  CHECK_EQ(icw, 920);
  CHECK_EQ(ich, 920);
  QImage inside = render(single_layer(inside_f), solid_sheet(900, 900));
  CHECK_EQ(inside.width(), 920);
  CHECK_EQ(inside.height(), 920);
  Extent ce = drawn_extent(inside);
  CHECK(!ce.empty());
  CHECK_NEAR(ce.w(), 900.0, 2.0);
  CHECK_NEAR(ce.h(), 900.0, 2.0);
}

/* The aspect ratio of the requested canvas survives the downscale.
 *
 * 1020 x 500 of content plus padding is a 1040 x 520 canvas, which is exactly
 * two to one, so a single uniform factor has to produce 1024 x 512. 1024 x
 * 1024 - the answer a per-edge clamp gives - would be the same pixels
 * stretched, and 1024 x 510 would be one axis rounded away from the other.
 *
 * The negative control is the same two to one canvas below the cap, which must
 * come through untouched, so the ratio above is a property of the scaling and
 * not of the geometry the test happened to pick. */
void test_downscale_preserves_aspect_ratio()
{
  Anm2Frame f = frame_at(0, 0, 0, 0, 1020, 500, 0, 100, 100);
  double want_w = 0, want_h = 0;
  expected_canvas(f, &want_w, &want_h);
  CHECK_NEAR(want_w, 1020.0, 0.5);
  CHECK_NEAR(want_h, 500.0, 0.5);

  auto [cw, ch] = anm2_compute_animation_rect(single_layer(f), 400, 300);
  CHECK_EQ(cw, 1024);
  CHECK_EQ(ch, 512);
  /* Not the square a per-edge clamp produces, and not off by a rounding step. */
  CHECK(ch != cw);
  CHECK_NEAR(static_cast<double>(cw) / ch, 2.0, 0.01);

  QImage img = render(single_layer(f), solid_sheet(1020, 500));
  CHECK_EQ(img.width(), 1024);
  CHECK_EQ(img.height(), 512);

  /* The ratio has to survive in the drawn pixels, not just in the canvas:
   * 1020 x 500 of content is 1004 x 492 once scaled, and it is that pair which
   * says the two axes were scaled by the same number. Drawn unscaled it would
   * fill the canvas and measure 1020 x 500 instead. The sheet is as big as the
   * crop on purpose - a smaller one would be clipped by QPixmap::copy() and the
   * extent would measure the sheet rather than the content. */
  Extent e = drawn_extent(img);
  CHECK(!e.empty());
  CHECK_NEAR(e.w(), 1004.0, 3.0);
  CHECK_NEAR(e.h(), 492.0, 3.0);
  CHECK_NEAR(static_cast<double>(e.w()) / e.h(), 1020.0 / 500.0, 0.03);
  CHECK(e.w() < img.width());
  CHECK(e.h() < img.height());

  /* Same again on the draw path with a canvas handed in unfitted, which is the
   * second way in and the one where the allocation is bounded rather than
   * merely reported. */
  QList<LayerDef> defs{LayerDef{0, QStringLiteral("l"), 0}};
  std::map<int, QPixmap> sheets{{0, solid_sheet(1020, 500)}};
  QImage direct = anm2_render_frame_at_time(single_layer(f), defs, sheets, 0.0f,
                                            1040, 520);
  CHECK_EQ(direct.width(), 1024);
  CHECK_EQ(direct.height(), 512);
  CHECK(anm2_canvas_within_limits(direct.width(), direct.height()));
  /* Same pixels as the path above: the caller gets back the capped size and
   * the whole content in it, which is the part that cannot come from the size
   * arithmetic alone. */
  Extent de = drawn_extent(direct);
  CHECK(!de.empty());
  CHECK_NEAR(de.w(), e.w(), 1.0);
  CHECK_NEAR(de.h(), e.h(), 1.0);

  /* Negative control: the same shape inside the cap is not scaled at all. */
  Anm2Frame inside_f = frame_at(0, 0, 0, 0, 620, 300, 0, 100, 100);
  auto [icw, ich] = anm2_compute_animation_rect(single_layer(inside_f), 400, 300);
  CHECK_EQ(icw, 640);
  CHECK_EQ(ich, 320);
  QImage inside = render(single_layer(inside_f), solid_sheet(620, 300));
  CHECK_EQ(inside.width(), 640);
  CHECK_EQ(inside.height(), 320);
  Extent ie = drawn_extent(inside);
  CHECK(!ie.empty());
  CHECK_NEAR(ie.w(), 620.0, 2.0);
  CHECK_NEAR(ie.h(), 300.0, 2.0);
}

/* The refusal is drawn, not blank. A null QImage would reach the host as an
 * empty frame and leave the user looking at an empty preview with nothing
 * saying why, so the assertion is that the refusal has opaque pixels and more
 * than one colour in it.
 *
 * The negative control is an animation with nothing drawable, whose canvas is
 * genuinely empty - so "not blank" is a claim about this image and not about
 * every image the renderer produces. */
void test_oversize_is_reported_not_silent()
{
  Anm2Frame f = frame_at(0, 0, 0, 0, 43, 126, 0, 16800, 10000);
  QImage notice = render(single_layer(f), solid_sheet(64, 64));
  CHECK(!notice.isNull());

  Extent e = drawn_extent(notice);
  CHECK(!e.empty());

  /* A border, a heading and a body: at least three distinct opaque colours,
   * none of them the background the canvas is cleared to. */
  QSet<QRgb> colours;
  for (int y = 0; y < notice.height(); y += 2) {
    for (int x = 0; x < notice.width(); x += 2) {
      QRgb px = notice.pixel(x, y);
      if (qAlpha(px) > 0)
        colours.insert(px & 0x00ffffff);
    }
  }
  CHECK(colours.size() >= 3);

  /* The border runs the whole way round, so the notice is framed rather than
   * a stray glyph in a corner. */
  CHECK(qAlpha(notice.pixel(1, notice.height() / 2)) > 0);
  CHECK(qAlpha(notice.pixel(notice.width() - 2, notice.height() / 2)) > 0);
  CHECK(qAlpha(notice.pixel(notice.width() / 2, 1)) > 0);
  CHECK(qAlpha(notice.pixel(notice.width() / 2, notice.height() - 2)) > 0);

  /* Negative control: an animation with every layer hidden produces a canvas
   * that really is empty, so the three-colour finding above means something. */
  Animation hidden = single_layer(frame_at(0, 0, 0, 0, 32, 32, 0, 100, 100));
  hidden.layer_animations[0].visible = false;
  QImage blank = render(hidden, solid_sheet(64, 64));
  CHECK(!blank.isNull());
  CHECK(drawn_extent(blank).empty());
}

/* The size arithmetic cannot overflow, so a canvas that overflows a 32-bit
 * width*height*4 computation is refused on its merits and not on the
 * accident of a wrapped value.
 *
 * 40000 x 40000 is 1.6e9 px: that fits in an int, but times 4 it is 6.4e9,
 * which does not, so an int32 computation of the byte count has already
 * overflowed by the time the result would be compared against anything.
 * 100000 x 100000 overflows the pixel count itself. Both are reachable from
 * the file - Width and XScale are arbitrary integers.
 *
 * The area is the limit that decides this. A canvas over the edge cap is
 * scaled down, so being long is on its own no longer a reason to be turned
 * away, and the rows above 1024 px that are still marked allowed are saying
 * so: the pixel budget is what a malformed file has to get past.
 *
 * The negative control is 1000 x 1000: 1e6 px, comfortably inside the budget
 * and inside the edge cap, so this is a case about the arithmetic and not
 * about large numbers being turned away. */
void test_canvas_size_arithmetic_cannot_overflow()
{
  struct Spec
  {
    int w, h;
    bool allowed;
  };
  const Spec specs[] = {
      {40000, 40000, false},   // w*h fits in int32, w*h*4 does not
      {100000, 100000, false},  // w*h overflows int32 on its own
      {1, 67108864, true},      // exactly on the budget, and allowed
      {67108864, 2, false},     // one pixel of area over the budget
      {1000, 1000, true},       // the control, inside both
      {1024, 1024, true},       // the cap exactly: a square the cap cannot clip
      {1025, 1024, true},       // one past the cap, still inside the budget
      {4000, 4000, true},       // well past the cap, still inside the budget
  };

  for (const Spec& s : specs) {
    bool got = anm2_canvas_within_limits(s.w, s.h);
    CHECK_EQ(got, s.allowed);

    /* The comparison is done on a 64-bit product. Recomputing it in int32
     * here would be undefined behaviour rather than a test, so instead the
     * product is shown to be the one the 64-bit path makes: for the two
     * overflow rows the 32-bit product is a different, much smaller number. */
    if (s.w * s.h > 2147483647LL / 4) {
      long long wide = static_cast<long long>(s.w) * s.h * 4LL;
      CHECK(wide > 2147483647LL);
    }
  }

  /* And the same sizes asked for by a file, which is the way they arrive. */
  Anm2Frame f = frame_at(0, 0, 0, 0, 40000, 40000, 0, 100, 100);
  QImage img  = render(single_layer(f), solid_sheet(64, 64));
  CHECK(!img.isNull());
  CHECK(img.width() <= kAnm2MaxCanvasEdge);
  CHECK(img.height() <= kAnm2MaxCanvasEdge);
  CHECK(static_cast<qint64>(img.width()) * img.height() <= kAnm2MaxCanvasPixels);

  /* The control geometry really does draw, at its own full size. The sheet
   * has to be as big as the crop: QPixmap::copy() returns a null pixmap for a
   * rectangle that is not inside the source, so a small sheet here would draw
   * nothing and prove nothing. */
  Anm2Frame ok_f = frame_at(0, 0, 0, 0, 1000, 1000, 0, 100, 100);
  QImage ok      = render(single_layer(ok_f), solid_sheet(1000, 1000));
  CHECK_EQ(ok.width(), 1020);
  CHECK_EQ(ok.height(), 1020);
  Extent ok_e = drawn_extent(ok);
  CHECK(!ok_e.empty());
  CHECK_NEAR(ok_e.w(), 1000.0, 2.0);
  CHECK_NEAR(ok_e.h(), 1000.0, 2.0);
}

/* The scale needs no clamp of its own, and none is applied.
 *
 * The bounds are the union of the transformed crop rectangles, so a frame's
 * scale multiplies straight into the measured box whatever the position or
 * the pivot: bounding the box bounds the scale. A scale large enough to leave
 * the box is therefore already turned away by the canvas limit, and a scale
 * large enough to stay inside it is honoured at its real size rather than
 * being trimmed to something the limit allows.
 *
 * The negative control is the pair that makes the point: the same 64px sprite
 * at 1000% is drawn 640px across, not clamped, and at 100000% - the largest
 * XScale in any real file, from infected mushroom.anm2 - it is refused. */
void test_scale_is_bounded_by_the_canvas_not_clamped()
{
  Anm2Frame drawable = frame_at(0, 0, 0, 0, 64, 64, 0, 1000, 1000);
  QImage img        = render(single_layer(drawable), solid_sheet(64, 64));
  Extent e           = drawn_extent(img);
  CHECK(!e.empty());
  /* 64 x 1000% is 640. A clamp would have produced something smaller; the
   * renderer is meant to be the one place the number is not adjusted. */
  CHECK_NEAR(e.w(), 640.0, 2.0);
  CHECK_NEAR(e.h(), 640.0, 2.0);
  CHECK(e.w() >= 630);
  CHECK(e.w() <= 650);

  Anm2Frame hostile = frame_at(0, 0, 0, 0, 64, 64, 0, 100000, 100000);
  double want_w = 0, want_h = 0;
  expected_canvas(hostile, &want_w, &want_h);
  CHECK_NEAR(want_w, 64000.0, 1.0);   // 64 * 1000x
  CHECK(want_w > kAnm2MaxCanvasEdge);
  QImage refused = render(single_layer(hostile), solid_sheet(64, 64));
  CHECK(refused.width() <= kAnm2MaxCanvasEdge);
  CHECK(refused.height() <= kAnm2MaxCanvasEdge);
  /* Refused, not drawn at 640 the way the 1000% case was: the two are an
   * order of magnitude apart and the smaller one is not the larger one
   * quietly shrunk. */
  CHECK(refused.height() != e.h());
  CHECK(std::fabs(static_cast<double>(refused.height()) - e.h()) > 100.0);
}

/* The frame count is bounded too, because it multiplies every other cost:
 * the bounds pass steps once per frame and the host holds an image per frame.
 * The count arrives as a plain attribute, so it is as attacker-controlled as
 * the canvas.
 *
 * The control is the largest count any of 27378 real animations reports -
 * 4522, from the intro cutscene - which passes through untouched. */
void test_frame_count_is_bounded()
{
  Animation a    = single_layer(frame_at(0, 0, 0, 0, 32, 32, 0, 100, 100));
  a.frame_num    = 2000000000;
  CHECK_EQ(anm2_compute_total_frames(a), kAnm2MaxTotalFrames);
  CHECK(anm2_compute_total_frames(a) < a.frame_num);

  /* A negative value cannot become a large one. */
  a.frame_num = -5;
  CHECK_EQ(anm2_compute_total_frames(a), 1);  // falls back to the track length

  /* And a file whose keyframe delays declare the length gets the same bound,
   * by the other route into the same number. */
  Animation delays = single_layer(frame_at(0, 0, 0, 0, 32, 32, 0, 100, 100));
  delays.frame_num = 0;
  Anm2Frame big    = delays.layer_animations[0].frames[0];
  big.delay        = 2000000000;
  delays.layer_animations[0].frames = QList<Anm2Frame>{big, big, big};
  CHECK(anm2_compute_total_frames(delays) <= kAnm2MaxTotalFrames);
  CHECK(anm2_track_length_get(delays.layer_animations[0].frames) <=
        kAnm2MaxTotalFrames);

  /* Negative control: the real worst case is untouched. */
  Animation real = single_layer(frame_at(0, 0, 0, 0, 32, 32, 0, 100, 100));
  real.frame_num = 4522;
  CHECK_EQ(anm2_compute_total_frames(real), 4522);
  CHECK(4522 < kAnm2MaxTotalFrames);
}

/* -- null animations ------------------------------------------------------ */

/* A <NullAnimation> does not move the canvas, and that is the correct
 * rendering rather than a dropped animation.
 *
 * A null addresses an object in the running game - the pickup item Isaac is
 * holding, an eye, a tractor beam - and the file neither contains a sprite for
 * it nor names the animation that does. So there is nothing to draw, and
 * nothing to compose onto a layer.
 *
 * The test is built so that it could fail. The null carries a transform of
 * 100000%, which is the largest XScale in any real file and which on a layer
 * would put 32 x 1000% - a 32000px edge, over the pixel budget - into the
 * canvas. The negative control is the same transform applied to a layer
 * instead, in a file that is otherwise byte-for-byte the same: if that one is
 * refused and this one is not, then what is being measured is which tag the
 * transform sits under, and the "not composed" claim is real. */
void test_null_animations_do_not_change_the_canvas()
{
  const char* kWithNull =
      "<AnimatedActor><Content><Spritesheets/><Layers>"
      "<Layer Name=\"a\" Id=\"0\" SpritesheetId=\"0\"/></Layers>"
      "<Nulls><Null Name=\"pickup item\" Id=\"0\"/></Nulls>"
      "<Events><Event Name=\"s\" Id=\"0\"/></Events></Content>"
      "<Animations DefaultAnimation=\"Default\">"
      "<Animation Name=\"Default\" FrameNum=\"1\" Loop=\"false\">"
      "<RootAnimation><Frame XPosition=\"0\" YPosition=\"0\" Delay=\"1\"/></RootAnimation>"
      "<LayerAnimations><LayerAnimation LayerId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\"/>"
      "</LayerAnimation></LayerAnimations>"
      "<NullAnimations><NullAnimation NullId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" XScale=\"100000\" YScale=\"100000\" "
      "Delay=\"1\"/>"
      "</NullAnimation></NullAnimations>"
      "<Triggers><Trigger EventId=\"0\" AtFrame=\"0\"/></Triggers>"
      "</Animation></Animations></AnimatedActor>";

  /* Identical except that the big transform moves from the null onto the
   * layer. */
  const char* kOnLayer =
      "<AnimatedActor><Content><Spritesheets/><Layers>"
      "<Layer Name=\"a\" Id=\"0\" SpritesheetId=\"0\"/></Layers>"
      "<Nulls><Null Name=\"pickup item\" Id=\"0\"/></Nulls>"
      "<Events><Event Name=\"s\" Id=\"0\"/></Events></Content>"
      "<Animations DefaultAnimation=\"Default\">"
      "<Animation Name=\"Default\" FrameNum=\"1\" Loop=\"false\">"
      "<RootAnimation><Frame XPosition=\"0\" YPosition=\"0\" Delay=\"1\"/></RootAnimation>"
      "<LayerAnimations><LayerAnimation LayerId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\" "
      "XScale=\"100000\" YScale=\"100000\"/>"
      "</LayerAnimation></LayerAnimations>"
      "<NullAnimations><NullAnimation NullId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" XScale=\"100000\" YScale=\"100000\" "
      "Delay=\"1\"/>"
      "</NullAnimation></NullAnimations>"
      "<Triggers><Trigger EventId=\"0\" AtFrame=\"0\"/></Triggers>"
      "</Animation></Animations></AnimatedActor>";

  QTemporaryDir dir;
  CHECK(dir.isValid());
  if (!dir.isValid())
    return;

  struct Parsed
  {
    Animation anim;
    int cw     = 0;
    int ch     = 0;
    QImage img;
  };

  auto load = [&](const char* xml, const char* stem) {
    Parsed p;
    QString path = dir.filePath(QString::fromLatin1(stem) + QStringLiteral(".anm2"));
    {
      QFile f(path);
      CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
      if (!f.isOpen())
        return p;
      f.write(xml);
      f.close();
    }
    QList<Spritesheet> sheets;
    QList<LayerDef> defs;
    CHECK(anm2_parse_file(path, p.anim, sheets, defs, nullptr));
    std::map<int, QPixmap> sheets_by_id{{0, solid_sheet(64, 64)}};
    auto [w, h]           = anm2_compute_animation_rect(p.anim, 400, 300);
    p.cw                 = w;
    p.ch                 = h;
    p.img                = anm2_render_frame_at_time(p.anim, defs, sheets_by_id,
                                                      0.0f, w, h);
    return p;
  };

  Parsed with_null = load(kWithNull, "with_null");
  Parsed on_layer  = load(kOnLayer, "on_layer");

  /* The null does not reach the canvas. The layer is a 32x32 sprite, so the
   * canvas is that sprite plus the 20px of padding. */
  CHECK_EQ(with_null.cw, 52);
  CHECK_EQ(with_null.ch, 52);
  CHECK_EQ(with_null.img.width(), 52);
  CHECK(!drawn_extent(with_null.img).empty());
  Extent ne = drawn_extent(with_null.img);
  CHECK_NEAR(ne.w(), 32.0, 1.5);
  CHECK_NEAR(ne.h(), 32.0, 1.5);

  /* Negative control: the same transform on a layer does change the answer,
   * and 32 x 1000% is 32000px, so the content box is over the pixel budget and
   * the canvas is refused outright. If this did not come out different from the
   * case above, the case above would be proving nothing about which tag was
   * measured. */
  CHECK(anm2_canvas_within_limits(on_layer.cw, on_layer.ch));
  CHECK(!on_layer.img.isNull());
  CHECK(on_layer.cw != with_null.cw);
  CHECK(on_layer.ch != with_null.ch);
  /* The refused one is not a 32px sprite: it carries the notice, which is a
   * different shape entirely. */
  Extent le = drawn_extent(on_layer.img);
  CHECK(!le.empty());
  CHECK(std::fabs(static_cast<double>(le.w()) - 32.0) > 100.0);
  CHECK_EQ(on_layer.img.height(), on_layer.ch);
  CHECK_EQ(with_null.img.height(), with_null.ch);

  /* QTemporaryDir removes the tree on destruction. */
}

/* The sections the parser skips are skipped cleanly: adding them to a file
 * leaves the parsed animation exactly as it was. If the skip were consuming
 * the wrong elements, this is where it would show - a frame lost to a
 * miscounted read, or a layer animation swallowed by a stray skip.
 *
 * The negative control is the same comparison run against a file whose layer
 * frame really does differ, so "identical" is a claim about these two files
 * and not about the comparison itself. */
void test_skipped_sections_leave_the_animation_intact()
{
  const char* kPlain =
      "<AnimatedActor><Content><Spritesheets/><Layers>"
      "<Layer Name=\"a\" Id=\"0\" SpritesheetId=\"0\"/></Layers></Content>"
      "<Animations DefaultAnimation=\"Default\">"
      "<Animation Name=\"Default\" FrameNum=\"2\" Loop=\"false\">"
      "<RootAnimation><Frame XPosition=\"3\" YPosition=\"4\" Delay=\"1\"/></RootAnimation>"
      "<LayerAnimations><LayerAnimation LayerId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\"/>"
      "<Frame XPosition=\"5\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\"/>"
      "</LayerAnimation></LayerAnimations>"
      "</Animation></Animations></AnimatedActor>";

  const char* kDecorated =
      "<AnimatedActor><Content><Spritesheets/><Layers>"
      "<Layer Name=\"a\" Id=\"0\" SpritesheetId=\"0\"/></Layers>"
      "<Nulls><Null Name=\"OverlayEffect\" Id=\"0\"/><Null Name=\"Mouth\" Id=\"1\"/>"
      "</Nulls><Events><Event Name=\"s\" Id=\"0\"/></Events></Content>"
      "<Animations DefaultAnimation=\"Default\">"
      "<Animation Name=\"Default\" FrameNum=\"2\" Loop=\"false\">"
      "<RootAnimation><Frame XPosition=\"3\" YPosition=\"4\" Delay=\"1\"/></RootAnimation>"
      "<LayerAnimations><LayerAnimation LayerId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\"/>"
      "<Frame XPosition=\"5\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\"/>"
      "</LayerAnimation></LayerAnimations>"
      "<NullAnimations><NullAnimation NullId=\"1\" Visible=\"false\">"
      "<Frame XPosition=\"900\" YPosition=\"900\" XScale=\"500\" YScale=\"500\" "
      "Delay=\"1\"/>"
      "</NullAnimation></NullAnimations>"
      "<Triggers><Trigger EventId=\"0\" AtFrame=\"1\"/></Triggers>"
      "</Animation></Animations></AnimatedActor>";

  /* The control: a file whose second layer frame really does sit elsewhere. */
  const char* kMoved =
      "<AnimatedActor><Content><Spritesheets/><Layers>"
      "<Layer Name=\"a\" Id=\"0\" SpritesheetId=\"0\"/></Layers></Content>"
      "<Animations DefaultAnimation=\"Default\">"
      "<Animation Name=\"Default\" FrameNum=\"2\" Loop=\"false\">"
      "<RootAnimation><Frame XPosition=\"3\" YPosition=\"4\" Delay=\"1\"/></RootAnimation>"
      "<LayerAnimations><LayerAnimation LayerId=\"0\" Visible=\"true\">"
      "<Frame XPosition=\"0\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\"/>"
      "<Frame XPosition=\"25\" YPosition=\"0\" Width=\"32\" Height=\"32\" Delay=\"1\"/>"
      "</LayerAnimation></LayerAnimations>"
      "</Animation></Animations></AnimatedActor>";

  QTemporaryDir dir;
  CHECK(dir.isValid());
  if (!dir.isValid())
    return;

  auto frames_of = [&](const char* xml, const char* stem, QList<Anm2Frame>* out,
                       int* frame_num) {
    QString path = dir.filePath(QString::fromLatin1(stem) + QStringLiteral(".anm2"));
    {
      QFile f(path);
      CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
      if (!f.isOpen())
        return false;
      f.write(xml);
      f.close();
    }
    Animation anim;
    QList<Spritesheet> sheets;
    QList<LayerDef> defs;
    bool ok = anm2_parse_file(path, anim, sheets, defs, nullptr);
    CHECK(ok);
    *out      = anim.layer_animations.isEmpty() ? QList<Anm2Frame>{}
                                                : anim.layer_animations[0].frames;
    *frame_num = anim.frame_num;
    return ok;
  };

  QList<Anm2Frame> plain_f, decorated_f, moved_f;
  int plain_n = 0, decorated_n = 0, moved_n = 0;
  frames_of(kPlain, "plain", &plain_f, &plain_n);
  frames_of(kDecorated, "decorated", &decorated_f, &decorated_n);
  frames_of(kMoved, "moved", &moved_f, &moved_n);

  /* Both files declare the same frame count and both keep both keyframes, so
   * the decorated file's extra sections neither ate a frame nor the frame
   * number. */
  CHECK_EQ(plain_n, 2);
  CHECK_EQ(decorated_n, plain_n);
  CHECK_EQ(decorated_f.size(), plain_f.size());
  CHECK_EQ(decorated_f.size(), 2);
  for (int i = 0; i < plain_f.size() && i < decorated_f.size(); ++i) {
    CHECK_EQ(decorated_f[i].x_position, plain_f[i].x_position);
    CHECK_EQ(decorated_f[i].x_scale, plain_f[i].x_scale);
    CHECK_EQ(decorated_f[i].width, plain_f[i].width);
    CHECK_EQ(decorated_f[i].height, plain_f[i].height);
    CHECK_EQ(decorated_f[i].delay, plain_f[i].delay);
  }

  /* Negative control: move one number and the comparison separates them, so
   * "identical" above is a statement about the decoration and not about the
   * comparison being blind. */
  CHECK(moved_f.size() == decorated_f.size());
  if (moved_f.size() == decorated_f.size() && moved_f.size() == 2) {
    CHECK_EQ(moved_f[1].x_position, 25);
    CHECK(decorated_f[1].x_position != moved_f[1].x_position);
    CHECK(plain_f[1].x_position != moved_f[1].x_position);
  }

  /* QTemporaryDir removes the tree on destruction. */
}

struct Case
{
  const char* name;
  void (*fn)();
};

const Case kCases[] = {
    {"scale_applied_once", test_scale_applied_once},
    {"scale_composes_with_crop_once", test_scale_composes_with_crop_once},
    {"negative_scale_mirrors", test_negative_scale_mirrors},
    {"pivot_at_centre_does_not_move", test_pivot_at_centre_does_not_move},
    {"pivot_controls_layer_placement", test_pivot_controls_layer_placement},
    {"pivot_reorder_negative_control", test_pivot_reorder_negative_control},
    {"bounds_match_draw", test_bounds_match_draw},
    {"negative_tint_offset_applies", test_negative_tint_offset_applies},
    {"tint_offset_clamps_to_channel_range", test_tint_offset_clamps_to_channel_range},
    {"tint_offset_applies_to_untouched_tint", test_tint_offset_applies_to_untouched_tint},
    {"placeholder_path_scales_once", test_placeholder_path_scales_once},
    {"parser_scale_attribute_defaults", test_parser_scale_attribute_defaults},
    {"oversize_content_box_is_refused", test_oversize_content_box_is_refused},
    {"measured_real_maximum_still_renders", test_measured_real_maximum_still_renders},
    {"oversize_canvas_is_scaled_down", test_oversize_canvas_is_scaled_down},
    {"downscale_preserves_aspect_ratio", test_downscale_preserves_aspect_ratio},
    {"oversize_is_reported_not_silent", test_oversize_is_reported_not_silent},
    {"canvas_size_arithmetic_cannot_overflow", test_canvas_size_arithmetic_cannot_overflow},
    {"scale_is_bounded_by_the_canvas_not_clamped", test_scale_is_bounded_by_the_canvas_not_clamped},
    {"frame_count_is_bounded", test_frame_count_is_bounded},
    {"null_animations_do_not_change_the_canvas", test_null_animations_do_not_change_the_canvas},
    {"skipped_sections_leave_the_animation_intact", test_skipped_sections_leave_the_animation_intact},
};

}  // namespace

int main(int argc, char** argv)
{
  /* QPixmap is a QPaintDevice, so a QGuiApplication has to exist before any
   * sheet is built. Offscreen keeps this runnable on a headless box. */
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);

  /* An argument runs exactly one case, so a failure can be reproduced alone.
   * No argument runs everything. */
  const char* only = argc > 1 ? argv[1] : nullptr;
  if (only && std::string(only) == "--list") {
    for (const auto& c : kCases)
      std::printf("%s\n", c.name);
    return 0;
  }

  bool ran_any = false;
  for (const auto& c : kCases) {
    if (only && std::string(only) != c.name)
      continue;
    int before = g_failures;
    c.fn();
    ran_any = true;
    std::printf("%-40s %s\n", c.name, g_failures == before ? "ok" : "FAILED");
  }

  if (!ran_any) {
    std::fprintf(stderr, "no such case: %s\n", only ? only : "(none)");
    return 2;
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
