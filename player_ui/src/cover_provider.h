#pragma once
#include <QColor>
#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>

struct TrackItem;

// 封面服务:
//   image://cover/a/<gen>/<albumIndex>   专辑封面
//   image://cover/t/<gen>/<trackId>      曲目封面
//   image://cover/b/<gen>/<trackId>      背景用的重度模糊小图 (64px,GPU 放大即为柔和模糊,几乎零开销)
// 内嵌封面按物理偏移惰性读取;缩略图有内存 LRU + 磁盘两级缓存;无封面时生成由标题哈希决定的渐变占位图
class CoverProvider : public QQuickAsyncImageProvider {
public:
    CoverProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    // 同步加载 (供后台线程使用):返回按 maxEdge 缩放后的图像
    static QImage LoadTrackCover(const TrackItem& t, int maxEdge);
    static QImage Placeholder(const QString& seed, int edge);
    static QImage BlurSmall(const QImage& src);

private:
    QThreadPool pool_;
};

// 从封面提取强调色:偏好饱和、亮度适中、占比高的颜色,并保证在深色背景上可读
QColor ExtractAccentColor(const QImage& img);
