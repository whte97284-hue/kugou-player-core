#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "music_catalog.hpp"

// 一张专辑 (同一目录 + 同一专辑名 聚合),曲目指针指向所属快照中的 MusicCatalog,快照存活期间稳定
struct AlbumInfo {
    QString title;
    QString artist;
    QString year;
    std::vector<const TrackItem*> tracks; // 按 碟号 / 曲号 / CUE 起点 / 文件名 排序
    const TrackItem* coverTrack = nullptr;
    uint64_t totalMs = 0;
    uint32_t maxRate = 0;
    uint16_t maxBits = 0;
    bool lossless = true;
    bool hires = false;
    bool dsd = false;
    bool cue = false;
    QString dsdCodec;
    QString sortKey; // 拼音排序键
};

// 不可变的曲库快照:扫描完成后整体替换,界面线程与后台线程互不干扰
struct LibrarySnapshot {
    MusicCatalog catalog;
    std::vector<AlbumInfo> albums;
    std::unordered_map<uint64_t, int> trackAlbum; // 曲目 ID -> 专辑下标
};

class LibraryService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY progressChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY libraryChanged)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY libraryChanged)
    Q_PROPERTY(int generation READ generation NOTIFY libraryChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)

public:
    explicit LibraryService(QObject* parent = nullptr);
    ~LibraryService() override;

    static LibraryService* instance();

    bool scanning() const { return scanning_; }
    double progress() const { return progress_; }
    QString statusText() const { return status_; }
    int trackCount() const;
    int albumCount() const;
    int generation() const { return generation_; }
    QStringList folders() const { return folders_; }

    // 线程安全:获取当前快照 (持有 shared_ptr 期间快照不会被释放)
    std::shared_ptr<const LibrarySnapshot> snapshot() const;

    Q_INVOKABLE void addFolder(const QString& path);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE QString qualityBadge(int albumIndex) const;
    // 今日推荐:有封面的 Hi-Res 专辑优先,按日期稳定挑选 (同一天内不变)
    Q_INVOKABLE int featuredAlbum() const;
    // 专辑摘要:title / artist / year / cover / badge / summary / trackCount
    Q_INVOKABLE QVariantMap albumInfo(int albumIndex) const;

    static QString formatDuration(uint64_t ms);
    static QString trackQuality(const TrackItem& t);

signals:
    void scanningChanged();
    void progressChanged();
    void libraryChanged();
    void foldersChanged();

private:
    void startScan();
    void finishScan(std::shared_ptr<LibrarySnapshot> snap, double seconds, size_t cacheHits);
    static std::shared_ptr<LibrarySnapshot> BuildAlbums(std::unique_ptr<MusicCatalog> catalog);

    mutable std::mutex snap_mutex_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    QStringList folders_;
    bool scanning_ = false;
    double progress_ = 0.0;
    QString status_;
    int generation_ = 0;
    std::unique_ptr<class ScanJob> job_;
};
