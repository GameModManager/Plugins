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

        // Parse <Animation fps="30" Width="400" Height="300">
        int aPos = xml.indexOf("<Animation");
        if (aPos < 0) return;
        int aLineEnd = xml.indexOf('\n', aPos);
        if (aLineEnd < 0) aLineEnd = xml.length();
        QString aLine = xml.mid(aPos, aLineEnd - aPos);
        cw_ = attrVal(aLine, "Width").toInt();
        ch_ = attrVal(aLine, "Height").toInt();
        if (cw_ <= 0) cw_ = 400;
        if (ch_ <= 0) ch_ = 300;

        // Parse frames
        int pos = 0;
        while (true) {
            pos = xml.indexOf("<Frame", pos);
            if (pos < 0) break;
            int lineEnd = xml.indexOf('\n', pos);
            if (lineEnd < 0) lineEnd = xml.length();
            QString fLine = xml.mid(pos, lineEnd - pos);
            delays_ << attrVal(fLine, "Delay").toDouble();
            if (delays_.last() <= 0) delays_.last() = 100.0;

            int frameEnd = xml.indexOf("</Frame>", pos);
            if (frameEnd < 0) break;
            QString frameBody = xml.mid(pos, frameEnd - pos);

            // Parse layers
            QList<QStringList> frameLayers;
            int lp = 0;
            while (true) {
                lp = frameBody.indexOf("<Layer", lp);
                if (lp < 0) break;
                int layerScopeEnd = frameBody.indexOf("</Layer>", lp);
                if (layerScopeEnd < 0) layerScopeEnd = frameBody.length();
                QString scope = frameBody.mid(lp, layerScopeEnd - lp);

                int lLineEnd = scope.indexOf('\n');
                if (lLineEnd < 0) lLineEnd = scope.length();
                QString lLine = scope.mid(0, lLineEnd);

                int x = 0, y = 0, w = 0, h = 0;
                int pPos = scope.indexOf("<Position");
                if (pPos >= 0) {
                    int pEnd = scope.indexOf('\n', pPos);
                    if (pEnd < 0) pEnd = scope.length();
                    QString pLine = scope.mid(pPos, pEnd - pPos);
                    x = attrVal(pLine, "X").toInt();
                    y = attrVal(pLine, "Y").toInt();
                }
                int cPos = scope.indexOf("<Crop");
                if (cPos >= 0) {
                    int cEnd = scope.indexOf('\n', cPos);
                    if (cEnd < 0) cEnd = scope.length();
                    QString cLine = scope.mid(cPos, cEnd - cPos);
                    w = attrVal(cLine, "Width").toInt();
                    h = attrVal(cLine, "Height").toInt();
                }
                frameLayers << QStringList{QString::number(x), QString::number(y),
                                           QString::number(w), QString::number(h)};
                lp = layerScopeEnd;
            }
            layers_ << frameLayers;
            pos = frameEnd + 8;
        }

        if (!delays_.isEmpty()) {
            renderFrame(0);
            timer_ = new QTimer(this);
            connect(timer_, &QTimer::timeout, this, &Anm2PreviewWidget::next);
            timer_->start((int)delays_[0]);
        }
    }

private:
    void renderFrame(int i) {
        QPixmap pm(cw_, ch_);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        int c = 80;
        for (auto& l : layers_[i]) {
            p.fillRect(l[0].toInt(), l[1].toInt(), l[2].toInt(), l[3].toInt(),
                       QColor(c, c, c + 40));
            c = (c + 30) % 200;
        }
        label_->setPixmap(pm);
    }
    void next() {
        idx_ = (idx_ + 1) % delays_.size();
        renderFrame(idx_);
        timer_->start((int)delays_[idx_]);
    }

    QLabel* label_ = nullptr;
    QTimer* timer_ = nullptr;
    int idx_ = 0, cw_ = 400, ch_ = 300;
    QList<double> delays_;
    QList<QList<QStringList>> layers_;
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
        GmmPreviewInfo info{};
        info.file_extension = ".anm2";
        ctx->register_preview(ctx, info, anm2_preview, nullptr);
    }
}

} /* extern "C" */
