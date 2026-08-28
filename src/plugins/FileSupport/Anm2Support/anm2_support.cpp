/**
 * Anm2Support Plugin — .anm2 animation preview (v2 ABI)
 *
 * Parses The Binding of Isaac: Rebirth's XML-based .anm2 animation format
 * and renders a frame-by-frame preview using the referenced spritesheet PNGs.
 *
 * Format reference: https://www.moddingofisaac.com/docs/rep/xml/Anm2_files.html
 *
 * Build: shared library (MODULE), links Qt6::Widgets for QWidget creation.
 */

#include "gmm_abi_v2.h"

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QXmlStreamReader>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <QTransform>
#include <QPainterPath>
#include <map>
#include <climits>

/* --------------------------------------------------------------------------
 * Data structures matching the .anm2 spec
 * ------------------------------------------------------------------------ */

struct Spritesheet {
    int id = -1;
    QString path;   // relative path from game resources dir (backslash-separated)
    QPixmap pixmap;  // loaded PNG
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
    int delay = 1;          // duration in animation frames
    bool visible = true;
    int rotation = 0;       // degrees clockwise
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
    Anm2Frame root_frame;                       // single base transform
    QList<LayerAnimation> layer_animations;
};

/* --------------------------------------------------------------------------
 * .anm2 parser using QXmlStreamReader
 * ------------------------------------------------------------------------ */

static Anm2Frame parseFrame(QXmlStreamReader& xml) {
    Anm2Frame f;
    auto attrs = xml.attributes();
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
    if (f.x_scale == 0) f.x_scale = 100;
    if (f.y_scale == 0) f.y_scale = 100;
    f.delay      = attrs.value("Delay").toInt();
    if (f.delay <= 0) f.delay = 1;
    f.visible    = attrs.value("Visible").toString() != "false";
    f.rotation   = attrs.value("Rotation").toInt();
    f.red_tint   = attrs.value("RedTint").toInt();
    f.green_tint = attrs.value("GreenTint").toInt();
    f.blue_tint  = attrs.value("BlueTint").toInt();
    f.alpha_tint = attrs.value("AlphaTint").toInt();
    if (f.red_tint == 0)   f.red_tint = 255;
    if (f.green_tint == 0) f.green_tint = 255;
    if (f.blue_tint == 0)  f.blue_tint = 255;
    if (f.alpha_tint == 0) f.alpha_tint = 255;
    f.red_offset   = attrs.value("RedOffset").toInt();
    f.green_offset = attrs.value("GreenOffset").toInt();
    f.blue_offset  = attrs.value("BlueOffset").toInt();
    f.interpolated = attrs.value("Interpolated").toString() == "true";
    return f;
}

static bool parseAnm2(const QString& path, Animation& anim,
                       QList<Spritesheet>& spritesheets,
                       QList<LayerDef>& layer_defs) {
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
                    /* Default animation name */
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
                                            la.visible  = xml.attributes().value("Visible").toString() != "false";
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
            break;  /* done with root element */
        } else {
            xml.skipCurrentElement();
        }
    }

    return anim.frame_num > 0;
}

/* --------------------------------------------------------------------------
 * Load spritesheet PNGs relative to the .anm2 file's directory
 * ------------------------------------------------------------------------ */

/* Case-insensitive path resolution for Linux. Splits the relative path
   into segments and for each segment tries to find a matching entry in
   the parent directory (case-insensitive). Returns the resolved path
   or an empty string if not found. */
static QString resolveCaseInsensitive(const QDir& base, const QString& rel) {
    QStringList segments = rel.split('/', Qt::SkipEmptyParts);
    QDir dir(base);
    for (const auto& seg : segments) {
        QStringList entries = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        bool found = false;
        for (const auto& entry : entries) {
            if (entry.compare(seg, Qt::CaseInsensitive) == 0) {
                dir.cd(entry);
                found = true;
                break;
            }
        }
        if (!found) return {};
    }
    return dir.absolutePath();
}

static void loadSpritesheets(const QString& anm2_path,
                              QList<Spritesheet>& spritesheets) {
    QDir anm2_dir = QFileInfo(anm2_path).absoluteDir();

    /* Collect candidate base directories.
       .anm2 spritesheet paths like "effects\errorkeeper.png" or
       "Bosses/Classic/Boss_025_Fistula.png" are relative to gfx/ under
       the mod's resources dir. Walk up from the .anm2 file looking for
       gfx/ siblings. Also include the .anm2's own directory (for files
       already inside gfx/). */
    QStringList base_dirs;
    base_dirs << anm2_dir.absolutePath();  // same dir as .anm2
    QDir walk = anm2_dir;
    for (int i = 0; i < 5; ++i) {
        /* Check if gfx/ exists as a child of this directory */
        QDir gfx_candidate(walk.absoluteFilePath("gfx"));
        if (gfx_candidate.exists() && !base_dirs.contains(gfx_candidate.absolutePath()))
            base_dirs << gfx_candidate.absolutePath();
        /* Also check if this directory IS gfx/ (for .anm2 files inside gfx/) */
        if (walk.dirName().compare("gfx", Qt::CaseInsensitive) == 0) {
            if (!base_dirs.contains(walk.absolutePath()))
                base_dirs << walk.absolutePath();
        }
        if (!walk.cdUp()) break;
    }

    for (auto& ss : spritesheets) {
        QString rel = ss.path;
        rel.replace('\\', '/');
        bool loaded = false;

        /* First try exact path (fast) */
        for (const auto& base : base_dirs) {
            QString full = QDir(base).absoluteFilePath(rel);
            if (QFileInfo::exists(full) && ss.pixmap.load(full)) {
                loaded = true;
                break;
            }
        }

        /* Then try case-insensitive (slow but necessary for cross-platform mods) */
        if (!loaded) {
            for (const auto& base : base_dirs) {
                QString resolved = resolveCaseInsensitive(QDir(base), rel);
                if (!resolved.isEmpty() && ss.pixmap.load(resolved)) {
                    loaded = true;
                    break;
                }
            }
        }
    }
}
/* --------------------------------------------------------------------------
 * Preview widget — renders animation frames
 * ------------------------------------------------------------------------ */

class Anm2PreviewWidget : public QWidget {
public:
    explicit Anm2PreviewWidget(const QString& path, QWidget* parent = nullptr)
        : QWidget(parent), step_(0), total_steps_(0) {
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
        if (!parseAnm2(path, anim, spritesheets, layer_defs)) {
            info_label_->setText("Failed to parse .anm2 file");
            return;
        }

        /* Load spritesheet PNGs */
        loadSpritesheets(path, spritesheets);

        /* Build spritesheet lookup by id */
        for (const auto& ss : spritesheets)
            sheet_by_id_[ss.id] = ss.pixmap;

        /* Store layer defs for spritesheet lookup */
        layer_defs_ = layer_defs;

        /* Compute total animation steps.
           Each LayerAnimation[i].frames has some number of entries.
           The animation steps through them — when a layer runs out of
           frames, it holds the last frame. Total steps = max across
           all layer frame counts, capped by frame_num. */
        int max_layer_frames = 0;
        for (const auto& la : anim.layer_animations)
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

        /* Default canvas: 400x300 — will be adjusted per frame */
        canvas_w_ = 400;
        canvas_h_ = 300;

        /* FPS from file — default 18 */
        int fps = 18;
        /* Try reading FPS from the Info tag (already parsed, we re-read here) */
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QXmlStreamReader xr(&f);
            while (xr.readNextStartElement()) {
                if (xr.name() == u"Info") {
                    fps = xr.attributes().value("Fps").toInt();
                    if (fps <= 0) fps = 18;
                    break;
                }
                xr.skipCurrentElement();
            }
        }
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
    QPixmap* sheetForLayer(int layer_id) {
        for (const auto& ld : layer_defs_) {
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
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x + w > max_x) max_x = x + w;
            if (y + h > max_y) max_y = y + h;
        };

        /* Root frame covers entire canvas */
        updateBounds(anim_.root_frame.x_position,
                     anim_.root_frame.y_position,
                     canvas_w_, canvas_h_);

        for (const auto& la : anim_.layer_animations) {
            if (!la.visible) continue;
            int fi = qMin(step, la.frames.size() - 1);
            if (fi < 0) continue;
            const Anm2Frame& fr = la.frames[fi];
            if (!fr.visible) continue;

            int rx = fr.x_position - fr.x_pivot + fr.x_crop;
            int ry = fr.y_position - fr.y_pivot + fr.y_crop;
            int rw = fr.width > 0 ? fr.width : 64;
            int rh = fr.height > 0 ? fr.height : 64;
            updateBounds(rx, ry, rw, rh);
        }

        if (max_x <= min_x) max_x = min_x + canvas_w_;
        if (max_y <= min_y) max_y = min_y + canvas_h_;

        /* Add padding */
        int pad = 10;
        min_x -= pad; min_y -= pad;
        max_x += pad; max_y += pad;

        int w = max_x - min_x;
        int h = max_y - min_y;
        if (w <= 0 || h <= 0) { w = 400; h = 300; }

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

        for (const auto& la : anim_.layer_animations) {
            if (!la.visible) continue;
            int fi = qMin(step, la.frames.size() - 1);
            if (fi < 0) continue;
            const Anm2Frame& fr = la.frames[fi];
            if (!fr.visible) continue;

            /* Find spritesheet for this layer */
            QPixmap* sheet = sheetForLayer(la.layer_id);

            /* Compute draw position */
            double sx = fr.x_scale / 100.0 * root_sx;
            double sy = fr.y_scale / 100.0 * root_sy;
            int draw_x = origin_x + fr.x_position - fr.x_pivot;
            int draw_y = origin_y + fr.y_position - fr.y_pivot;
            int crop_w = fr.width;
            int crop_h = fr.height;

            /* If no crop specified, use full spritesheet or default size */
            if (sheet && crop_w <= 0) crop_w = sheet->width();
            if (sheet && crop_h <= 0) crop_h = sheet->height();
            if (crop_w <= 0) crop_w = 64;
            if (crop_h <= 0) crop_h = 64;

            /* Draw the sprite or a colored placeholder */
            if (sheet && !sheet->isNull()) {
                QPixmap cropped = sheet->copy(fr.x_crop, fr.y_crop, crop_w, crop_h);
                if (!cropped.isNull()) {
                    /* Apply scale */
                    int scaled_w = qMax(1, (int)(crop_w * sx));
                    int scaled_h = qMax(1, (int)(crop_h * sy));
                    QPixmap scaled = cropped.scaled(scaled_w, scaled_h,
                                                    Qt::IgnoreAspectRatio,
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
                /* No spritesheet — draw colored rectangle placeholder */
                static const QColor kPalette[] = {
                    QColor(100, 120, 180), QColor(140, 100, 160),
                    QColor(100, 160, 140), QColor(180, 120, 100),
                    QColor(120, 140, 160), QColor(160, 130, 130),
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
    QLabel* info_label_ = nullptr;
    QLabel* label_ = nullptr;
    QTimer* timer_ = nullptr;

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
    int frame_interval_ms_ = 55;  /* ~18 fps default */
};

/* --------------------------------------------------------------------------
 * Preview callback
 * ------------------------------------------------------------------------ */
static void* anm2_preview(const char* path, void*, void*) {
    if (!path) return nullptr;
    return new Anm2PreviewWidget(QString::fromUtf8(path));
}

/* --------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------ */
extern "C" {

uint32_t gmm_abi_version() { return GMM_ABI_VERSION; }

void gmm_register_v2(GmmRegistrationCtxV2* ctx) {
    if (!ctx) return;

    ctx->register_plugin(ctx, {
        .name = "ANM2 Support",
        .author = "GameModManager Team",
        .version = "2.0.0",
        .description = "ANM2 animation file preview (The Binding of Isaac: Rebirth)"
    });

    if (ctx->register_category)
        ctx->register_category(ctx, "File Support");

    if (ctx->register_preview) {
        ctx->register_preview(ctx, ".anm2", nullptr, anm2_preview, nullptr);
    }
}

} /* extern "C" */
