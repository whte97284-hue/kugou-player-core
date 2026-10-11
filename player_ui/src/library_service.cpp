#include "library_service.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>

#include "audio_ingestion_pipeline.hpp"
#include "pinyin_table.hpp"

namespace {
LibraryService* g_instance = nullptr;

QString Q(const std::wstring& w) { return QString::fromStdWString(w); }

std::wstring ParentDir(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring() : p.substr(0, s);
}

// CD1/Disc 2 这类子目录归并到上级目录,使多碟专辑成为一张
std::wstring AlbumDirKey(const std::wstring& path) {
    std::wstring dir = ParentDir(path);
    size_t s = dir.find_last_of(L"\\/");
    std::wstring name = s == std::wstring::npos ? dir : dir.substr(s + 1);
    std::wstring l;
    for (wchar_t c : name) l += static_cast<wchar_t>(towlower(c));
    for (const wchar_t* pre : {L"cd", L"disc", L"disk"}) {
        size_t n = wcslen(pre);
        if (l.compare(0, n, pre) == 0) {
            size_t i = n;
            while (i < l.size() && (l[i] == L' ' || l[i] == L'_' || l[i] == L'-' || l[i] == L'.')) ++i;
            bool digits = i < l.size();
            for (size_t k = i; k < l.size(); ++k) digits = digits && iswdigit(l[k]);
            if (digits) return ParentDir(dir);
        }
    }
    return dir;
}
} // namespace

// 后台扫描作业:持有流水线与新曲库,完成后交给主线程
class ScanJob {
public:
    AudioIngestionPipeline pipeline;
    std::unique_ptr<MusicCatalog> catalog = std::make_unique<MusicCatalog>();
    std::atomic<qint64> lastPost{0};
    QElapsedTimer clock;
};

LibraryService::LibraryService(QObject* parent) : QObject(parent) {
    g_instance = this;
    snap_ = std::make_shared<LibrarySnapshot>();
    QSettings s;
    folders_ = s.value("library/folders").toStringList();
}

LibraryService::~LibraryService() {
    if (job_) job_->pipeline.Cancel();
    job_.reset(); // 流水线析构会等待后台线程结束
    g_instance = nullptr;
}

LibraryService* LibraryService::instance() { return g_instance; }

int LibraryService::trackCount() const { return static_cast<int>(snapshot()->catalog.GetAllTracks().size()); }
int LibraryService::albumCount() const { return static_cast<int>(snapshot()->albums.size()); }

std::shared_ptr<const LibrarySnapshot> LibraryService::snapshot() const {
    std::lock_guard<std::mutex> lock(snap_mutex_);
    return snap_;
}

void LibraryService::addFolder(const QString& path) {
    QString p = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (!folders_.contains(p)) {
        folders_.append(p);
        QSettings().setValue("library/folders", folders_);
        emit foldersChanged();
    }
    rescan();
}

void LibraryService::rescan() {
    if (scanning_ || folders_.isEmpty()) return;
    startScan();
}

void LibraryService::startScan() {
    job_ = std::make_unique<ScanJob>();
    ScanJob* job = job_.get();
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    job->pipeline.SetLazyCoverExtract(true);
    job->pipeline.SetScanCacheFile((dataDir + "/scan.cache").toStdWString());
    job->pipeline.SetTargetLibraryDir((dataDir + "/decoded").toStdWString());
    QString mask = QCoreApplication::applicationDirPath() + "/tools/kgm/kgm.mask";
    if (QFileInfo::exists(mask)) job->pipeline.SetKgmMaskPath(mask.toStdWString());
    for (const QString& f : folders_) job->pipeline.AddInputPath(f.toStdWString());
    job->clock.start();

    scanning_ = true;
    progress_ = 0;
    status_ = tr("正在扫描曲库…");
    emit scanningChanged();
    emit progressChanged();

    job->pipeline.StartAsync(
        *job->catalog,
        [this, job](const IngestionProgress& p) {
            // 进度节流:最多每 60ms 投递一次到界面线程
            qint64 now = job->clock.elapsed();
            if (now - job->lastPost.load() < 60 && p.current != p.total) return;
            job->lastPost.store(now);
            double v = p.total ? double(p.current) / double(p.total) : 0.0;
            QString text = tr("正在扫描 %1 / %2").arg(p.current).arg(p.total);
            QMetaObject::invokeMethod(this, [this, v, text]() {
                progress_ = v;
                status_ = text;
                emit progressChanged();
            }, Qt::QueuedConnection);
        },
        [this, job](const IngestionSummary& sum) {
            // 专辑聚合同样在后台线程完成,界面线程只做指针交换
            auto snap = BuildAlbums(std::move(job->catalog));
            double secs = sum.elapsed_seconds;
            size_t hits = sum.cache_hits;
            QMetaObject::invokeMethod(this, [this, snap, secs, hits]() { finishScan(snap, secs, hits); },
                                      Qt::QueuedConnection);
        });
}

void LibraryService::finishScan(std::shared_ptr<LibrarySnapshot> snap, double seconds, size_t cacheHits) {
    {
        std::lock_guard<std::mutex> lock(snap_mutex_);
        snap_ = snap;
    }
    ++generation_;
    scanning_ = false;
    progress_ = 1.0;
    status_ = tr("%1 首 · %2 张专辑 · 用时 %3 秒%4")
                  .arg(snap->catalog.GetAllTracks().size())
                  .arg(snap->albums.size())
                  .arg(seconds, 0, 'f', 2)
                  .arg(cacheHits ? tr(" (增量)") : QString());
    emit libraryChanged();
    emit scanningChanged();
    emit progressChanged();
    // 后台线程已经结束,回收作业对象放到事件循环下一轮,避免在回调栈内析构流水线
    QMetaObject::invokeMethod(this, [this]() { if (!scanning_) job_.reset(); }, Qt::QueuedConnection);
}

std::shared_ptr<LibrarySnapshot> LibraryService::BuildAlbums(std::unique_ptr<MusicCatalog> catalog) {
    auto snap = std::make_shared<LibrarySnapshot>();
    snap->catalog = std::move(*catalog);
    const auto& tracks = snap->catalog.GetAllTracks();

    std::map<std::wstring, int> index;
    for (const auto& t : tracks) {
        std::wstring albumLower;
        for (wchar_t c : t.album) albumLower += static_cast<wchar_t>(towlower(c));
        std::wstring key = albumLower + L"\x1F" + AlbumDirKey(t.file_path);
        auto it = index.find(key);
        int ai;
        if (it == index.end()) {
            ai = static_cast<int>(snap->albums.size());
            index.emplace(key, ai);
            snap->albums.emplace_back();
            snap->albums.back().title = Q(t.album);
        } else {
            ai = it->second;
        }
        snap->albums[ai].tracks.push_back(&t);
    }

    for (auto& a : snap->albums) {
        std::sort(a.tracks.begin(), a.tracks.end(), [](const TrackItem* x, const TrackItem* y) {
            if (x->disc_no != y->disc_no) return x->disc_no < y->disc_no;
            if (x->file_path == y->file_path && x->is_cue_track) return x->cue_start_ms < y->cue_start_ms;
            if (x->track_no != y->track_no) return x->track_no < y->track_no;
            return x->file_name < y->file_name;
        });
        std::map<std::wstring, int> artists;
        for (const TrackItem* t : a.tracks) {
            a.totalMs += t->duration_ms;
            a.maxRate = std::max(a.maxRate, t->sample_rate);
            a.maxBits = std::max(a.maxBits, t->bits_per_sample);
            a.lossless = a.lossless && t->is_lossless;
            a.hires = a.hires || t->is_hires;
            a.cue = a.cue || t->is_cue_track;
            if (t->codec.compare(0, 3, L"DSD") == 0) {
                a.dsd = true;
                a.dsdCodec = Q(t->codec);
            }
            if (!a.coverTrack && (t->has_cover || !t->external_cover_path.empty())) a.coverTrack = t;
            if (a.year.isEmpty() && !t->year.empty()) a.year = Q(t->year).left(4);
            artists[!t->album_artist.empty() ? t->album_artist : t->artist]++;
        }
        if (artists.size() == 1) a.artist = Q(artists.begin()->first);
        else if (artists.size() > 1) {
            // 多位歌手:取出现最多者,占比不足一半则视为合辑
            auto best = std::max_element(artists.begin(), artists.end(),
                                         [](const auto& l, const auto& r) { return l.second < r.second; });
            a.artist = best->second * 2 >= static_cast<int>(a.tracks.size()) ? Q(best->first) : QObject::tr("群星");
        }
        if (!a.coverTrack && !a.tracks.empty()) a.coverTrack = a.tracks.front();
        a.sortKey = QString::fromStdString(PinyinHelper::GetFullPinyin(a.title.toStdWString())) + a.title.toLower();
    }
    std::sort(snap->albums.begin(), snap->albums.end(),
              [](const AlbumInfo& x, const AlbumInfo& y) { return x.sortKey < y.sortKey; });
    for (int i = 0; i < static_cast<int>(snap->albums.size()); ++i)
        for (const TrackItem* t : snap->albums[i].tracks) snap->trackAlbum[t->id] = i;
    return snap;
}

QString LibraryService::formatDuration(uint64_t ms) {
    uint64_t s = ms / 1000;
    if (s >= 3600) return QString("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString LibraryService::trackQuality(const TrackItem& t) {
    if (t.codec.compare(0, 3, L"DSD") == 0) return Q(t.codec);
    if (t.is_lossless && t.sample_rate)
        return QString("%1/%2").arg(t.bits_per_sample).arg(t.sample_rate % 1000 ? QString::number(t.sample_rate / 1000.0, 'f', 1)
                                                                                    : QString::number(t.sample_rate / 1000));
    if (t.bitrate_kbps) return QString("%1k").arg(t.bitrate_kbps);
    return Q(t.format);
}

QString LibraryService::qualityBadge(int i) const {
    auto s = snapshot();
    if (i < 0 || i >= static_cast<int>(s->albums.size())) return {};
    const AlbumInfo& a = s->albums[i];
    if (a.dsd) return a.dsdCodec;
    if (a.hires) return QString("%1/%2").arg(a.maxBits).arg(a.maxRate % 1000 ? QString::number(a.maxRate / 1000.0, 'f', 1)
                                                                           : QString::number(a.maxRate / 1000));
    if (a.lossless) return tr("无损");
    return {};
}

int LibraryService::featuredAlbum() const {
    auto s = snapshot();
    const int n = static_cast<int>(s->albums.size());
    if (n == 0) return -1;
    std::vector<int> pool;
    for (int i = 0; i < n; ++i) {
        const AlbumInfo& a = s->albums[i];
        if (a.coverTrack && (a.coverTrack->has_cover || !a.coverTrack->external_cover_path.empty()) && (a.hires || a.dsd))
            pool.push_back(i);
    }
    if (pool.empty())
        for (int i = 0; i < n; ++i) pool.push_back(i);
    uint seed = qHash(QDate::currentDate().toJulianDay());
    return pool[seed % pool.size()];
}

QVariantMap LibraryService::albumInfo(int i) const {
    QVariantMap m;
    auto s = snapshot();
    if (i < 0 || i >= static_cast<int>(s->albums.size())) return m;
    const AlbumInfo& a = s->albums[i];
    m["index"] = i;
    m["title"] = a.title;
    m["artist"] = a.artist;
    m["year"] = a.year;
    m["cover"] = QString("image://cover/a/%1/%2").arg(generation_).arg(i);
    m["badge"] = qualityBadge(i);
    m["trackCount"] = static_cast<int>(a.tracks.size());
    QStringList parts;
    if (!a.year.isEmpty()) parts << a.year;
    parts << tr("%1 首").arg(a.tracks.size()) << formatDuration(a.totalMs);
    m["summary"] = parts.join(QStringLiteral("  ·  "));
    return m;
}
