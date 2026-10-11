#include "cover_provider.h"

#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QRunnable>
#include <QStandardPaths>
#include <QtMath>
#include <cmath>
#include <mutex>

#include "library_service.h"
#include "music_catalog.hpp"

namespace {

// ---------------- 内存 LRU (跨线程共享) ----------------
std::mutex g_cacheMutex;
QCache<QString, QImage>& MemCache() {
    static QCache<QString, QImage> c(160 * 1024); // 以 KB 计,约 160MB
    return c;
}

QString DiskCacheDir() {
    static QString dir = [] {
        QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs";
        QDir().mkpath(d);
        return d;
    }();
    return dir;
}

QString CoverIdentity(const TrackItem& t) {
    QByteArray key = QString::fromStdWString(t.file_path).toUtf8();
    key += '|' + QByteArray::number(static_cast<qulonglong>(t.file_size));
    key += '|' + QByteArray::number(static_cast<qulonglong>(t.cover_offset));
    key += '|' + QByteArray::number(t.cover_size);
    key += '|' + QString::fromStdWString(t.external_cover_path).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
}

int Bucket(int edge) {
    for (int b : {128, 256, 512, 1024})
        if (edge <= b) return b;
    return 1600;
}

class Response : public QQuickImageResponse, public QRunnable {
public:
    Response(QString id, QSize req) : id_(std::move(id)), req_(req) { setAutoDelete(false); }
    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }

    void run() override {
        image_ = Produce();
        emit finished();
    }

private:
    QImage Produce() {
        // id: <kind>/<gen>/<ref>
        QStringList parts = id_.split('/');
        if (parts.size() < 3) return CoverProvider::Placeholder(id_, 256);
        const QString kind = parts[0];
        LibraryService* lib = LibraryService::instance();
        auto snap = lib ? lib->snapshot() : nullptr;
        if (!snap) return CoverProvider::Placeholder(id_, 256);

        const TrackItem* track = nullptr;
        QString seed;
        if (kind == "a") {
            int idx = parts[2].toInt();
            if (idx >= 0 && idx < static_cast<int>(snap->albums.size())) {
                track = snap->albums[idx].coverTrack;
                seed = snap->albums[idx].title;
            }
        } else {
            track = snap->catalog.FindById(parts[2].toULongLong());
            if (track) seed = QString::fromStdWString(track->album);
        }

        int edge = std::max(req_.width(), req_.height());
        if (edge <= 0) edge = 512;
        if (kind == "b") edge = 256;
        int bucket = Bucket(edge);

        QImage img;
        if (track) {
            QString memKey = CoverIdentity(*track) + QString::number(bucket);
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                if (QImage* hit = MemCache().object(memKey)) img = *hit;
            }
            if (img.isNull()) {
                QString diskPath = DiskCacheDir() + "/" + memKey + ".jpg";
                if (QFile::exists(diskPath)) img.load(diskPath);
                if (img.isNull()) {
                    img = CoverProvider::LoadTrackCover(*track, bucket);
                    if (!img.isNull()) img.save(diskPath, "JPG", 90);
                }
                if (!img.isNull()) {
                    std::lock_guard<std::mutex> lock(g_cacheMutex);
                    MemCache().insert(memKey, new QImage(img), std::max<qsizetype>(1, img.sizeInBytes() / 1024));
                }
            }
        }
        if (img.isNull()) img = CoverProvider::Placeholder(seed.isEmpty() ? id_ : seed, bucket);
        if (kind == "b") return CoverProvider::BlurSmall(img);
        return img;
    }

    QString id_;
    QSize req_;
    QImage image_;
};

// 三次盒式模糊近似高斯
void BoxBlur(QImage& img, int radius) {
    const int w = img.width(), h = img.height();
    QImage tmp(img.size(), QImage::Format_ARGB32);
    auto pass = [&](const QImage& src, QImage& dst, bool horizontal) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int r = 0, g = 0, b = 0, n = 0;
                for (int k = -radius; k <= radius; ++k) {
                    int sx = horizontal ? std::clamp(x + k, 0, w - 1) : x;
                    int sy = horizontal ? y : std::clamp(y + k, 0, h - 1);
                    QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(sy))[sx];
                    r += qRed(p);
                    g += qGreen(p);
                    b += qBlue(p);
                    ++n;
                }
                reinterpret_cast<QRgb*>(dst.scanLine(y))[x] = qRgb(r / n, g / n, b / n);
            }
        }
    };
    for (int i = 0; i < 3; ++i) {
        pass(img, tmp, true);
        pass(tmp, img, false);
    }
}

} // namespace

CoverProvider::CoverProvider() : QQuickAsyncImageProvider() { pool_.setMaxThreadCount(4); }

QQuickImageResponse* CoverProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* r = new Response(id, requestedSize);
    pool_.start(r);
    return r;
}

QImage CoverProvider::LoadTrackCover(const TrackItem& t, int maxEdge) {
    std::vector<uint8_t> data;
    std::string mime;
    if (!MusicCatalog::LoadCover(t, data, mime) || data.empty()) return {};
    QImage img;
    if (!img.loadFromData(data.data(), static_cast<int>(data.size()))) return {};
    if (std::max(img.width(), img.height()) > maxEdge)
        img = img.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img.convertToFormat(QImage::Format_RGB32);
}

QImage CoverProvider::Placeholder(const QString& seed, int edge) {
    // 由标题哈希决定色相的柔和双色渐变,每张无封面专辑都有稳定且不同的观感
    uint h = qHash(seed);
    QColor a = QColor::fromHsl(int(h % 360), 70, 70);
    QColor b = QColor::fromHsl(int((h / 360 + h % 360 + 40) % 360), 90, 35);
    QImage img(edge, edge, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient g(0, 0, edge, edge);
    g.setColorAt(0, a);
    g.setColorAt(1, b);
    p.fillRect(img.rect(), g);
    QRadialGradient glow(edge * 0.3, edge * 0.25, edge * 0.7);
    glow.setColorAt(0, QColor(255, 255, 255, 50));
    glow.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillRect(img.rect(), glow);
    // 专辑名首字作为大号字形,无封面专辑也能一眼区分
    QString glyph = seed.trimmed().left(1).toUpper();
    if (!glyph.isEmpty() && !seed.contains('/')) {
        QFont f;
        f.setPixelSize(int(edge * 0.46));
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setPen(QColor(255, 255, 255, 150));
        p.drawText(img.rect(), Qt::AlignCenter, glyph);
    }
    return img;
}

QImage CoverProvider::BlurSmall(const QImage& src) {
    QImage small = src.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    BoxBlur(small, 3);
    return small;
}

QColor ExtractAccentColor(const QImage& src) {
    if (src.isNull()) return QColor("#d8b06a");
    QImage img = src.scaled(48, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    // 色相直方图 (36 桶),权重 = 饱和度 × 亮度适中度
    struct Bin { double w = 0, r = 0, g = 0, b = 0; };
    Bin bins[36];
    double total = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QColor c = QColor::fromRgb(line[x]);
            float hh, s, l;
            c.getHslF(&hh, &s, &l);
            if (hh < 0) continue; // 无彩色
            double mid = 1.0 - std::abs(l - 0.55) * 1.8;
            if (mid <= 0 || s < 0.18) continue;
            double w = s * s * mid;
            Bin& bin = bins[std::min(35, int(hh * 36))];
            bin.w += w;
            bin.r += c.redF() * w;
            bin.g += c.greenF() * w;
            bin.b += c.blueF() * w;
            total += w;
        }
    }
    int best = -1;
    for (int i = 0; i < 36; ++i)
        if (best < 0 || bins[i].w + 0.5 * (bins[(i + 35) % 36].w + bins[(i + 1) % 36].w) >
                            bins[best].w + 0.5 * (bins[(best + 35) % 36].w + bins[(best + 1) % 36].w))
            best = i;
    if (best < 0 || total < 1.0 || bins[best].w <= 0) {
        // 近乎黑白的封面:用暖白色强调
        return QColor("#e8dcc8");
    }
    const Bin& b = bins[best];
    QColor c = QColor::fromRgbF(float(b.r / b.w), float(b.g / b.w), float(b.b / b.w));
    float hh, s, l;
    c.getHslF(&hh, &s, &l);
    // 深色背景上的可读性:亮度拉到 0.62~0.75,饱和度不低于 0.45
    l = std::clamp(l, 0.62f, 0.75f);
    s = std::clamp(s, 0.45f, 0.85f);
    return QColor::fromHslF(hh, s, l);
}
