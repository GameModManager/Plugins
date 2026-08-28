/**
 * Anm2Support Plugin — .anm2 animation preview (v2 ABI)
 *
 * Non-game-specific file support plugin. Provides a preview widget for
 * .anm2 files via the standard v2 register_preview interface.
 *
 * Build: shared library (MODULE), links Qt6::Widgets for QWidget creation.
 */

#include "gmm_abi_v2.h"

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QString>

/* --------------------------------------------------------------------------
 * Simple helpers (no external deps)
 * ------------------------------------------------------------------------ */
static QString readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream ts(&f);
    return ts.readAll();
}

static QString attrVal(const QString& tag, const char* name) {
    QString needle = QString(name) + "=\"";
    int pos = tag.indexOf(needle);
    if (pos < 0) return {};
    pos += needle.length();
    int end = tag.indexOf('"', pos);
    if (end < 0) return {};
    return tag.mid(pos, end - pos);
}

/* --------------------------------------------------------------------------
 * .anm2 preview widget
 * ------------------------------------------------------------------------ */
/* -- Per-layer frame data (real Isaac .anm2 format) -- */
struct Anm2FrameLayer {
    int x = 0, y = 0;       // position
    int cx = 0, cy = 0;     // crop origin
    int cw = 0, ch = 0;     // crop size (drawn rect)
    int pivot_x = 0, pivot_y = 0;
    bool visible = true;
    int layer_id = -1;
};

struct Anm2Frame {
    double delay = 100.0;
    QList<Anm2FrameLayer> layers;
};

class Anm2PreviewWidget : public QWidget {
public:
    explicit Anm2PreviewWidget(const QString& path, QWidget* parent = nullptr)
        : QWidget(parent), idx_(0) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        label_ = new QLabel(this);
        label_->setAlignment(Qt::AlignCenter);
        lay->addWidget(label_);

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QString xml = QTextStream(&f).readAll();

        /* Parse <Info Fps="18" .../> for default frame delay */
        int infoPos = xml.indexOf("<Info");
        if (infoPos >= 0) {
            int infoEnd = xml.indexOf("/>", infoPos);
            if (infoEnd < 0) infoEnd = xml.indexOf("\n", infoPos);
            if (infoEnd < 0) infoEnd = xml.length();
            QString infoLine = xml.mid(infoPos, infoEnd - infoPos);
            int fps = attrVal(infoLine, "Fps").toInt();
            if (fps > 0) default_delay_ = 1000.0 / fps;
        }

        /* Parse canvas size from <Info> or <Animation> (Isaac doesn't have
           Width/Height; use a reasonable default — spritesheet sizes vary). */
        cw_ = 400;
        ch_ = 300;

        /* Parse <RootAnimation> — the base frame for all layer animations */
        Anm2Frame rootFrame;
        rootFrame.delay = default_delay_;
        parseRootAnimation(xml, rootFrame);

        /* Parse each <LayerAnimation> and merge with root */
        parseLayerAnimations(xml, rootFrame);

        if (!frames_.isEmpty()) {
            renderFrame(0);
            timer_ = new QTimer(this);
            connect(timer_, &QTimer::timeout, this, &Anm2PreviewWidget::next);
            timer_->start((int)frames_[0].delay);
        }
    }

private:
    /* Extract a single attribute value from an XML tag string */
    static QString attr(const QString& tag, const char* name) {
        return attrVal(tag, name);
    }

    /* Find the extent of an XML element by matching open→close tags.
       Handles self-closing tags (<Foo ... />) and nested tags.
       Returns the position right after the closing tag / self-close. */
    static int findElementEnd(const QString& xml, int openPos) {
        /* Check for self-closing: look for /> before the next newline or > */
        int scanEnd = xml.indexOf('\n', openPos);
        if (scanEnd < 0) scanEnd = xml.length();
        int selfClose = xml.indexOf("/>", openPos);
        if (selfClose >= 0 && selfClose < scanEnd)
            return selfClose + 2;

        /* Not self-closing — find matching close tag.
           Extract the tag name first. */
        int nameStart = openPos + 1;
        int nameEnd = xml.indexOf(QChar(' '), nameStart);
        if (nameEnd < 0) nameEnd = xml.indexOf('>', nameStart);
        if (nameEnd < 0) return scanEnd;
        QString tagName = xml.mid(nameStart, nameEnd - nameStart);

        QString closeTag = "</" + tagName + ">";
        int depth = 1;
        int pos = nameEnd;
        while (depth > 0 && pos < xml.length()) {
            int nextOpen = xml.indexOf("<" + tagName, pos);
            int nextClose = xml.indexOf(closeTag, pos);
            if (nextClose < 0) return xml.length();
            if (nextOpen >= 0 && nextOpen < nextClose) {
                /* Check it's not self-closing */
                int sc = xml.indexOf("/>", nextOpen);
                if (sc >= 0 && sc < nextClose && sc < nextOpen + 200) {
                    pos = sc + 2;
                } else {
                    depth++;
                    pos = nextOpen + tagName.length() + 1;
                }
            } else {
                depth--;
                if (depth == 0) return nextClose + closeTag.length();
                pos = nextClose + closeTag.length();
            }
        }
        return xml.length();
    }

    /* Parse <RootAnimation> → single base frame */
    void parseRootAnimation(const QString& xml, Anm2Frame& rootFrame) {
        int raPos = xml.indexOf("<RootAnimation");
        if (raPos < 0) return;
        int raEnd = findElementEnd(xml, raPos);
        QString raBody = xml.mid(raPos, raEnd - raPos);

        /* <Frame XPosition="..." ... /> */
        int fPos = raBody.indexOf("<Frame");
        if (fPos < 0) return;
        int fEnd = findElementEnd(raBody, fPos);
        QString fTag = raBody.mid(fPos, fEnd - fPos);

        Anm2FrameLayer fl;
        fl.x = attr(fTag, "XPosition").toInt();
        fl.y = attr(fTag, "YPosition").toInt();
        fl.cx = attr(fTag, "XCrop").toInt();
        fl.cy = attr(fTag, "YCrop").toInt();
        fl.cw = attr(fTag, "Width").toInt();
        fl.ch = attr(fTag, "Height").toInt();
        fl.pivot_x = attr(fTag, "XPivot").toInt();
        fl.pivot_y = attr(fTag, "YPivot").toInt();
        fl.visible = (attr(fTag, "Visible") != "false");
        fl.layer_id = -1;
        rootFrame.layers.append(fl);

        double d = attr(fTag, "Delay").toDouble();
        if (d > 0) rootFrame.delay = d;
    }

    /* Parse <LayerAnimations> → each <LayerAnimation> has N <Frame/>s */
    void parseLayerAnimations(const QString& xml, const Anm2Frame& rootFrame) {
        int laPos = xml.indexOf("<LayerAnimations");
        if (laPos < 0) return;
        int laEnd = findElementEnd(xml, laPos);
        QString laBody = xml.mid(laPos, laEnd - laPos);

        /* Each <LayerAnimation LayerId="0"> */
        int pos = 0;
        while (true) {
            int layerStart = laBody.indexOf("<LayerAnimation", pos);
            if (layerStart < 0) break;
            int layerEnd = findElementEnd(laBody, layerStart);
            QString layerBlock = laBody.mid(layerStart, layerEnd - layerStart);
            int layerId = attr(layerBlock, "LayerId").toInt();
            pos = layerEnd;

            /* Parse all <Frame/> inside this LayerAnimation.
               Isaac puts one Frame per layer per animation frame. */
            int fpos = 0;
            while (true) {
                int fStart = layerBlock.indexOf("<Frame", fpos);
                if (fStart < 0) break;
                int fEnd = findElementEnd(layerBlock, fStart);
                QString fTag = layerBlock.mid(fStart, fEnd - fStart);
                fpos = fEnd;

                Anm2FrameLayer fl;
                fl.x = attr(fTag, "XPosition").toInt();
                fl.y = attr(fTag, "YPosition").toInt();
                fl.cx = attr(fTag, "XCrop").toInt();
                fl.cy = attr(fTag, "YCrop").toInt();
                fl.cw = attr(fTag, "Width").toInt();
                fl.ch = attr(fTag, "Height").toInt();
                fl.pivot_x = attr(fTag, "XPivot").toInt();
                fl.pivot_y = attr(fTag, "YPivot").toInt();
                fl.visible = (attr(fTag, "Visible") != "false");
                fl.layer_id = layerId;

                double d = attr(fTag, "Delay").toDouble();

                /* Each Frame in a LayerAnimation = one animation frame.
                   We build one Anm2Frame per unique delay-index by merging
                   all layer frames that share the same position in the
                   sequence.  Simple approach: one Anm2Frame per <Frame>
                   entry — the animation steps through them. */
                Anm2Frame af = rootFrame;
                af.delay = (d > 0) ? d : rootFrame.delay;
                af.layers.append(fl);
                frames_.append(af);
            }
        }

        /* If no layer animations were found, fall back to root-only */
        if (frames_.isEmpty()) {
            frames_.append(rootFrame);
        }
    }

    void renderFrame(int i) {
        if (i < 0 || i >= frames_.size()) return;
        const Anm2Frame& frame = frames_[i];

        /* Compute bounding box from all visible layers */
        int maxX = 0, maxY = 0;
        for (const auto& fl : frame.layers) {
            if (!fl.visible || fl.cw <= 0 || fl.ch <= 0) continue;
            int rx = fl.x - fl.pivot_x + fl.cx;
            int ry = fl.y - fl.pivot_y + fl.cy;
            if (rx + fl.cw > maxX) maxX = rx + fl.cw;
            if (ry + fl.ch > maxY) maxY = ry + fl.ch;
        }
        if (maxX <= 0) maxX = cw_;
        if (maxY <= 0) maxY = ch_;

        QPixmap pm(maxX, maxY);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);

        /* Draw layers back-to-front by layer_id */
        QList<Anm2FrameLayer> sorted = frame.layers;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Anm2FrameLayer& a, const Anm2FrameLayer& b) {
                      return a.layer_id < b.layer_id;
                  });

        int colorIdx = 0;
        static const QColor palette[] = {
            QColor(120, 120, 160), QColor(140, 110, 150), QColor(100, 140, 140),
            QColor(160, 120, 110), QColor(110, 150, 120), QColor(150, 130, 140)
        };
        for (const auto& fl : sorted) {
            if (!fl.visible || fl.cw <= 0 || fl.ch <= 0) continue;
            int rx = fl.x - fl.pivot_x + fl.cx;
            int ry = fl.y - fl.pivot_y + fl.cy;
            QColor col = palette[colorIdx % 6];
            p.fillRect(rx, ry, fl.cw, fl.ch, col);
            p.setPen(col.darker(130));
            p.drawRect(rx, ry, fl.cw, fl.ch);
            colorIdx++;
        }

        label_->setPixmap(pm);
    }

    void next() {
        if (frames_.isEmpty()) return;
        idx_ = (idx_ + 1) % frames_.size();
        renderFrame(idx_);
        timer_->start((int)frames_[idx_].delay);
    }

    QLabel* label_ = nullptr;
    QTimer* timer_ = nullptr;
    int idx_ = 0, cw_ = 400, ch_ = 300;
    double default_delay_ = 100.0;
    QList<Anm2Frame> frames_;
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
        .version = "1.0.0",
        .description = "ANM2 animation file preview"
    });

    if (ctx->register_category)
        ctx->register_category(ctx, "File Support");

    if (ctx->register_preview) {
        ctx->register_preview(ctx, ".anm2", nullptr, anm2_preview, nullptr);
    }
}

} /* extern "C" */
