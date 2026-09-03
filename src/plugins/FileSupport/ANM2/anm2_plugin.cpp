/**
 * ANM2 Plugin -- GMM registration entry point (v2 ABI).
 *
 * Registers the ANM2
 * animation preview and parser with the host.
 * This file contains the ABI entry
 * points (gmm_abi_version, gmm_register_v2)
 * and the standalone QWidget preview
 * widget.
 */

#include "anm2_parser.h"
#include "anm2_renderer.h"
#include "anm2_types.h"
#include "gmm_abi_v2.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <utility>

/* --------------------------------------------------------------------------
 * Preview
 * widget -- renders animation frames (standalone QWidget)
 * Uses on-demand
 * interpolation for smooth playback.
 *
 * ------------------------------------------------------------------------ */

class Anm2PreviewWidget : public QWidget
{
public:
  explicit Anm2PreviewWidget(const QString& path, QWidget* parent = nullptr)
      : QWidget(parent), step_(0), total_steps_(0)
  {
    auto* lay = new QVBoxLayout(this);
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
    if (!anm2_parse_file(path, anim, spritesheets, layer_defs)) {
      info_label_->setText("Failed to parse .anm2 file");
      return;
    }

    /* Load spritesheet PNGs */
    anm2_load_spritesheets(path, spritesheets);

    /* Build spritesheet lookup by id */
    for (const auto& ss : spritesheets)
      sheet_by_id_[ss.id] = ss.pixmap;

    layer_defs_ = layer_defs;

    /* Compute total animation steps */
    total_steps_ = anm2_compute_total_frames(anim);
    if (total_steps_ <= 0) {
      info_label_->setText("No animation frames found");
      return;
    }

    anim_     = std::move(anim);
    has_data_ = true;

    /* Compute fixed canvas size once (no canvas bouncing) */
    auto [cw, ch] = anm2_compute_animation_rect(anim_, 400, 300);
    canvas_w_     = cw;
    canvas_h_     = ch;

    int fps            = anm2_read_fps(path);
    frame_interval_ms_ = 1000 / fps;

    info_label_->setText(
        QString("%1  [%2 frames, %3 fps]").arg(anim_.name).arg(total_steps_).arg(fps));

    /* Render first frame using on-demand rendering */
    renderTime(0.0f);
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &Anm2PreviewWidget::nextFrame);
    timer_->start(frame_interval_ms_);
  }

private:
  /* Render a frame at the given time using on-demand interpolation */
  void renderTime(float time)
  {
    if (!has_data_)
      return;

    QImage canvas = anm2_render_frame_at_time(anim_, layer_defs_, sheet_by_id_, time,
                                              canvas_w_, canvas_h_);
    label_->setPixmap(QPixmap::fromImage(canvas));
  }

  void nextFrame()
  {
    step_ = (step_ + 1) % total_steps_;
    renderTime(static_cast<float>(step_));
    timer_->start(frame_interval_ms_);
  }

  /* UI */
  QLabel* info_label_ = nullptr;
  QLabel* label_      = nullptr;
  QTimer* timer_      = nullptr;

  /* Animation data */
  bool has_data_ = false;
  Animation anim_;
  QList<LayerDef> layer_defs_;
  std::map<int, QPixmap> sheet_by_id_;
  int canvas_w_ = 400;
  int canvas_h_ = 300;

  /* Playback state */
  int step_              = 0;
  int total_steps_       = 0;
  int frame_interval_ms_ = 55;
};

/* --------------------------------------------------------------------------
 * Preview
 * callback
 * ------------------------------------------------------------------------
 */

static void* anm2_preview(const char* path, void*, void*)
{
  if (!path)
    return nullptr;
  return new Anm2PreviewWidget(QString::fromUtf8(path));
}

/* --------------------------------------------------------------------------
 * Main
 * animation parser entry point (ABI callback)
 *
 * ------------------------------------------------------------------------ */

static int anm2_parse(const char* file_path_c, const char* base_dir_c,
                      GmmAnimationDataV2* out, void*)
{
  if (!file_path_c || !out)
    return 0;

  QString file_path = QString::fromUtf8(file_path_c);
  QString base_dir  = base_dir_c ? QString::fromUtf8(base_dir_c)
                                 : QFileInfo(file_path).absoluteDir().absolutePath();

  /* Parse the .anm2 XML -- collect ALL animations for named states */
  Animation anim;
  QList<Animation> all_anims;
  QList<Spritesheet> spritesheets;
  QList<LayerDef> layer_defs;
  if (!anm2_parse_file(file_path, anim, spritesheets, layer_defs, &all_anims))
    return 0;

  /* Load spritesheet PNGs relative to base_dir */
  anm2_load_spritesheets_from_dir(base_dir, spritesheets);

  /* Build spritesheet lookup by id */
  std::map<int, QPixmap> sheet_by_id;
  for (const auto& ss : spritesheets)
    sheet_by_id[ss.id] = ss.pixmap;

  /* Read FPS */
  int fps = anm2_read_fps(file_path);

  /* Compute fixed canvas size for the default animation */
  auto [def_cw, def_ch] = anm2_compute_animation_rect(anim, 400, 300);

  /* Pre-bake the first frame as fallback for backward compat */
  int total                       = anm2_compute_total_frames(anim);
  GmmAnimationFrameV2* def_frames = nullptr;
  size_t def_count                = 0;

  if (total > 0) {
    def_frames = static_cast<GmmAnimationFrameV2*>(
        calloc(static_cast<size_t>(total), sizeof(GmmAnimationFrameV2)));
    if (def_frames) {
      def_count = static_cast<size_t>(total);
      for (int step = 0; step < total; ++step) {
        GmmAnimationFrameV2& cf = def_frames[step];
        cf.delay_ms             = 1000.0f / static_cast<float>(fps);

        QImage canvas = anm2_render_frame_at_time(
            anim, layer_defs, sheet_by_id, static_cast<float>(step), def_cw, def_ch);
        QImage rgba = canvas.convertToFormat(QImage::Format_RGBA8888);

        cf.layer_count = 1;
        cf.layers =
            static_cast<GmmAnimationLayerV2*>(calloc(1, sizeof(GmmAnimationLayerV2)));
        if (!cf.layers) {
          for (size_t j = 0; j < static_cast<size_t>(step); ++j) {
            for (size_t k = 0; k < def_frames[j].layer_count; ++k)
              free(def_frames[j].layers[k].rgba_pixels);
            free(def_frames[j].layers);
          }
          free(def_frames);
          def_frames = nullptr;
          def_count  = 0;
          break;
        }

        cf.layers[0].x           = 0;
        cf.layers[0].y           = 0;
        cf.layers[0].width       = rgba.width();
        cf.layers[0].height      = rgba.height();
        cf.layers[0].pixel_count = static_cast<size_t>(rgba.sizeInBytes());
        cf.layers[0].rgba_pixels =
            static_cast<uint8_t*>(malloc(cf.layers[0].pixel_count));
        if (cf.layers[0].rgba_pixels)
          memcpy(cf.layers[0].rgba_pixels, rgba.constBits(), cf.layers[0].pixel_count);
      }
    }
  }

  if (!def_frames || def_count == 0)
    return 0;

  out->fps           = static_cast<float>(fps);
  out->canvas_width  = def_cw;
  out->canvas_height = def_ch;
  out->frames        = def_frames;
  out->frame_count   = def_count;
  out->render_frame  = anm2_render_frame_cb;

  /* ---------------------------------------------------------------
   * Allocate raw
   * animation data for on-demand rendering
   *
   * --------------------------------------------------------------- */
  auto* raw          = new Anm2RawData();
  raw->anim          = anim;
  raw->spritesheets  = spritesheets;
  raw->layer_defs    = layer_defs;
  raw->sheet_by_id   = sheet_by_id;
  raw->fps           = fps;
  raw->total_frames  = total;
  raw->canvas_width  = def_cw;
  raw->canvas_height = def_ch;
  out->raw_animation = raw;

  /* ---------------------------------------------------------------
   * Named states:
   * render each animation from all_anims as a state
   *
   * --------------------------------------------------------------- */
  if (all_anims.size() > 1) {
    out->state_count = static_cast<size_t>(all_anims.size());
    out->states      = static_cast<GmmAnimationStateV2*>(
        calloc(out->state_count, sizeof(GmmAnimationStateV2)));
    if (out->states) {
      for (int si = 0; si < all_anims.size(); ++si) {
        GmmAnimationStateV2& st = out->states[si];
        const Animation& a      = all_anims[si];

        QByteArray name_bytes = a.name.toUtf8();
        st.name               = static_cast<char*>(malloc(name_bytes.size() + 1));
        if (st.name) {
          memcpy(st.name, name_bytes.constData(), name_bytes.size());
          st.name[name_bytes.size()] = '\0';
        }

        auto [cw, ch]    = anm2_compute_animation_rect(a, 400, 300);
        st.canvas_width  = cw;
        st.canvas_height = ch;

        int state_total = anm2_compute_total_frames(a);
        st.frame_count  = static_cast<size_t>(state_total);
        st.frames       = static_cast<GmmAnimationFrameV2*>(
            calloc(st.frame_count, sizeof(GmmAnimationFrameV2)));
        if (st.frames) {
          for (int step = 0; step < state_total; ++step) {
            GmmAnimationFrameV2& cf = st.frames[step];
            cf.delay_ms             = 1000.0f / static_cast<float>(fps);

            QImage canvas = anm2_render_frame_at_time(a, layer_defs, sheet_by_id,
                                                      static_cast<float>(step), cw, ch);
            QImage rgba   = canvas.convertToFormat(QImage::Format_RGBA8888);

            cf.layer_count = 1;
            cf.layers      = static_cast<GmmAnimationLayerV2*>(
                calloc(1, sizeof(GmmAnimationLayerV2)));
            if (cf.layers) {
              cf.layers[0].x           = 0;
              cf.layers[0].y           = 0;
              cf.layers[0].width       = rgba.width();
              cf.layers[0].height      = rgba.height();
              cf.layers[0].pixel_count = static_cast<size_t>(rgba.sizeInBytes());
              cf.layers[0].rgba_pixels =
                  static_cast<uint8_t*>(malloc(cf.layers[0].pixel_count));
              if (cf.layers[0].rgba_pixels)
                memcpy(cf.layers[0].rgba_pixels, rgba.constBits(),
                       cf.layers[0].pixel_count);
            }
          }
        }

        /* Per-state raw data for on-demand rendering */
        auto* state_raw          = new Anm2RawData();
        state_raw->anim          = a;
        state_raw->spritesheets  = spritesheets;
        state_raw->layer_defs    = layer_defs;
        state_raw->sheet_by_id   = sheet_by_id;
        state_raw->fps           = fps;
        state_raw->total_frames  = state_total;
        state_raw->canvas_width  = cw;
        state_raw->canvas_height = ch;
        st.raw_animation         = state_raw;
        st.render_frame          = anm2_render_frame_cb;
      }
    }
  } else {
    out->states      = nullptr;
    out->state_count = 0;
  }

  return 1;
}

/* --------------------------------------------------------------------------
 * Entry
 * point
 * ------------------------------------------------------------------------ */
extern "C"
{

  uint32_t gmm_abi_version()
  {
    return GMM_ABI_VERSION;
  }

  void gmm_register_v2(GmmRegistrationCtxV2* ctx)
  {
    if (!ctx)
      return;

    ctx->register_plugin(ctx, {.name        = "ANM2",
                               .author      = "GameModManager Team",
                               .version     = "2.0.0",
                               .description = "ANM2 animation file preview and parser "
                                              "(The Binding of Isaac: Rebirth)"});

    if (ctx->register_category)
      ctx->register_category(ctx, "File Support");

    anm2_set_resolve_file(reinterpret_cast<void*>(ctx->resolve_file));

    if (ctx->register_preview) {
      ctx->register_preview(ctx, ".anm2", nullptr, anm2_preview, nullptr);
    }

    if (ctx->register_animation_parser) {
      ctx->register_animation_parser(ctx, nullptr, ".anm2", anm2_parse, 10, nullptr);
    }
  }

} /* extern "C" */
