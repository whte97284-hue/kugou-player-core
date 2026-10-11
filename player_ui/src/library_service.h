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

struct IngestionSummary;

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
    // 导入进度 (扫描期间实时刷新):已处理 / 总数 / 当前文件
    Q_PROPERTY(int scanDone READ scanDone NOTIFY progressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY progressChanged)
    Q_PROPERTY(QString scanFile READ scanFile NOTIFY progressChanged)
    // 上次扫描结果:tracks / albums / failed / skipped / seconds / incremental / cancelled
    Q_PROPERTY(QVariantMap lastScan READ lastScan NOTIFY scanningChanged)

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
    int scanDone() const { return scanDone_; }
    int scanTotal() const { return scanTotal_; }
    QString scanFile() const { return scanFile_; }
    QVariantMap lastScan() const { return lastScan_; }

    // 线程安全:获取当前快照 (持有 shared_ptr 期间快照不会被释放)
    std::shared_ptr<const LibrarySnapshot> snapshot() const;

    // 添加文件夹并立即扫描;扫描进行中时排队,当前扫描结束后自动再扫一次
    Q_INVOKABLE void addFolder(const QString& path);
    // 接受文件对话框 / 拖放给出的 file:// URL (或本地路径),返回实际添加的文件夹数
    Q_INVOKABLE int addFolderUrls(const QVariantList& urls);
    Q_INVOKABLE void removeFolder(const QString& path);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void cancelScan();
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
    void finishScan(std::shared_ptr<LibrarySnapshot> snap, const IngestionSummary& sum);
    void saveFolders();
    static std::shared_ptr<LibrarySnapshot> BuildAlbums(std::unique_ptr<MusicCatalog> catalog);

    mutable std::mutex snap_mutex_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    QStringList folders_;
    bool scanning_ = false;
    double progress_ = 0.0;
    QString status_;
    int generation_ = 0;
    int scanDone_ = 0;
    int scanTotal_ = 0;
    QString scanFile_;
    QVariantMap lastScan_;
    bool rescanPending_ = false;
    std::unique_ptr<class ScanJob> job_;
};
