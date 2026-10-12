#include "library_service.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
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

QString SortKeyOf(const std::wstring& text) {
    return QString::fromStdString(PinyinHelper::GetFullPinyin(text)) + QString::fromStdWString(text).toLower();
}

QString LetterOf(const QString& key) {
    for (QChar c : key) {
        if (c.isSpace()) continue;
        char l = c.toLatin1();
        if (l >= 'a' && l <= 'z') return QString(QChar(l - 32));
        if (l >= 'A' && l <= 'Z') return QString(QChar(l));
        return QStringLiteral("#");
    }
    return QStringLiteral("#");
}

QString TrackKeyOf(const TrackItem& t) {
    return QString::fromStdWString(t.file_path) + QLatin1Char('#') + QString::number(t.is_cue_track ? t.cue_start_ms : 0);
}

namespace {
// 「A / B」「A、B」「A feat. B」拆为多位歌手;「&」「,」常见于组合名,不拆
std::vector<std::wstring> SplitArtists(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    auto push = [&]() {
        size_t a = cur.find_first_not_of(L" \t");
        size_t b = cur.find_last_not_of(L" \t");
        if (a != std::wstring::npos) out.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    std::wstring lower;
    for (wchar_t c : s) lower += static_cast<wchar_t>(towlower(c));
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'/' || c == L'\u3001' || c == L';' || c == L'\uff1b' || c == L'\uff0f') {
            push();
            continue;
        }
        bool feat = false;
        for (const wchar_t* f : {L" feat. ", L" ft. ", L" feat ", L" featuring "}) {
            size_t n = wcslen(f);
            if (lower.compare(i, n, f) == 0) {
                push();
                i += n - 1;
                feat = true;
                break;
            }
        }
        if (!feat) cur += c;
    }
    push();
    return out;
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

QString LibraryService::dataDir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d;
}

LibraryService::LibraryService(QObject* parent) : QObject(parent) {
    g_instance = this;
    pending_ = std::make_shared<std::vector<EncryptedFileInfo>>();
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

void LibraryService::saveFolders() {
    QSettings().setValue("library/folders", folders_);
    emit foldersChanged();
}

void LibraryService::addFolder(const QString& path) {
    if (addFolderUrls({path}) == 0) rescan(); // 已存在的文件夹:当作一次手动重扫
}

int LibraryService::addFolderUrls(const QVariantList& urls) {
    int added = 0;
    for (const QVariant& v : urls) {
        QUrl u = v.toUrl();
        QString local = u.isLocalFile() ? u.toLocalFile() : v.toString();
        if (local.isEmpty()) continue;
        QFileInfo fi(local);
        // 拖进来的是文件:取其所在文件夹
        if (fi.exists() && !fi.isDir()) fi = QFileInfo(fi.absolutePath());
        if (!fi.isDir()) continue;
        QString p = QDir::toNativeSeparators(QDir::cleanPath(fi.absoluteFilePath()));
        // 已被某个父文件夹覆盖的子文件夹不重复添加;新加的父文件夹吸收已有的子文件夹
        bool covered = false;
        for (const QString& f : folders_) {
            QString fp = QDir::fromNativeSeparators(f), np = QDir::fromNativeSeparators(p);
            if (np.compare(fp, Qt::CaseInsensitive) == 0 || np.startsWith(fp + "/", Qt::CaseInsensitive)) covered = true;
        }
        if (covered) continue;
        QString np = QDir::fromNativeSeparators(p);
        folders_.erase(std::remove_if(folders_.begin(), folders_.end(), [&](const QString& f) {
                           return QDir::fromNativeSeparators(f).startsWith(np + "/", Qt::CaseInsensitive);
                       }),
                       folders_.end());
        folders_.append(p);
        ++added;
    }
    if (added) {
        saveFolders();
        rescan();
    }
    return added;
}

void LibraryService::removeFolder(const QString& path) {
    if (!folders_.removeOne(path)) return;
    saveFolders();
    if (scanning_) {
        rescanPending_ = true;
        cancelScan();
    } else if (folders_.isEmpty()) {
        // 最后一个文件夹被移除:清空曲库
        finishScan(std::make_shared<LibrarySnapshot>(), IngestionSummary{});
    } else {
        startScan();
    }
}

void LibraryService::rescan() {
    if (folders_.isEmpty()) return;
    if (scanning_) {
        rescanPending_ = true;
        return;
    }
    startScan();
}

void LibraryService::cancelScan() {
    if (scanning_ && job_) job_->pipeline.Cancel();
}

void LibraryService::startScan() {
    job_ = std::make_unique<ScanJob>();
    ScanJob* job = job_.get();
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    job->pipeline.SetLazyCoverExtract(true);
    // 设置页:扫描线程 / CUE 分轨 / 旧编码标签代码页 / ID3v1 回读
    QSettings st;
    // 影响解析结果的设置改过:丢弃扫描缓存,本次全量解析
    if (st.value("library/needRescan", false).toBool()) QFile::remove(dataDir + "/scan.cache");
    job->pipeline.SetThreadCount(static_cast<size_t>(std::max(0, st.value("library/threads", 0).toInt())));
    job->pipeline.SetSplitCueSheets(st.value("library/splitCue", true).toBool());
    job->pipeline.SetLegacyCodePage(static_cast<unsigned>(st.value("library/codePage", 936).toInt()));
    job->pipeline.SetReadId3v1(st.value("library/readId3v1", true).toBool());
    job->pipeline.SetScanCacheFile((dataDir + "/scan.cache").toStdWString());
    job->pipeline.SetTargetLibraryDir((dataDir + "/decoded").toStdWString());
    // 加密源:默认只收集,由界面询问后再解密;产物索引让已解密的文件下次直接入库
    job->pipeline.SetDecryptPolicy(autoDecrypt_ ? DecryptPolicy::Auto : DecryptPolicy::Ask);
    job->pipeline.SetDecryptIndexFile((dataDir + "/decrypt.index").toStdWString());
    for (const QString& f : folders_) job->pipeline.AddInputPath(f.toStdWString());
    job->clock.start();

    scanning_ = true;
    rescanPending_ = false;
    progress_ = 0;
    scanDone_ = scanTotal_ = 0;
    scanFile_.clear();
    status_ = tr("正在查找音乐文件…");
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
            int done = static_cast<int>(p.current), total = static_cast<int>(p.total);
            QString file = Q(p.current_filename);
            QMetaObject::invokeMethod(this, [this, v, done, total, file]() {
                if (!scanning_) return;
                progress_ = v;
                scanDone_ = done;
                scanTotal_ = total;
                scanFile_ = file;
                status_ = tr("正在识别 %1 / %2").arg(done).arg(total);
                emit progressChanged();
            }, Qt::QueuedConnection);
        },
        [this, job](const IngestionSummary& sum) {
            // 专辑聚合同样在后台线程完成,界面线程只做指针交换
            auto snap = BuildAlbums(std::move(job->catalog));
            QMetaObject::invokeMethod(this, [this, snap, sum]() { finishScan(snap, sum); }, Qt::QueuedConnection);
        });
}

void LibraryService::finishScan(std::shared_ptr<LibrarySnapshot> snap, const IngestionSummary& sum) {
    // 被取消的扫描结果不完整:曲库已有内容时保留旧快照,首次导入时保留已识别的部分
    const bool keepOld = sum.cancelled && albumCount() > 0;
    if (!keepOld) {
        {
            std::lock_guard<std::mutex> lock(snap_mutex_);
            snap_ = snap;
        }
        ++generation_;
    }
    scanning_ = false;
    progress_ = 1.0;
    scanFile_.clear();
    if (!sum.cancelled) {
        pending_ = std::make_shared<std::vector<EncryptedFileInfo>>(sum.encrypted_pending);
        emit pendingEncryptedChanged();
    }

    const int tracks = static_cast<int>(snap->catalog.GetAllTracks().size());
    const int albums = static_cast<int>(snap->albums.size());
    lastScan_ = QVariantMap{
        {"tracks", tracks},
        {"albums", albums},
        {"failed", static_cast<int>(sum.failed_count)},
        {"skipped", static_cast<int>(sum.skipped_count)},
        {"seconds", sum.elapsed_seconds},
        {"incremental", sum.cache_hits > 0},
        {"cancelled", sum.cancelled},
    };
    if (sum.cancelled) status_ = tr("已取消扫描");
    else if (folders_.isEmpty()) status_ = tr("曲库已清空");
    else
        status_ = tr("%1 首 · %2 张专辑 · %3 秒%4")
                      .arg(tracks)
                      .arg(albums)
                      .arg(sum.elapsed_seconds, 0, 'f', 1)
                      .arg(sum.failed_count ? tr(" · %1 个文件无法识别").arg(sum.failed_count) : QString());
    if (!keepOld) emit libraryChanged();
    emit scanningChanged();
    emit progressChanged();
    // 后台线程已经结束,回收作业对象放到事件循环下一轮,避免在回调栈内析构流水线;
    // 扫描期间有新的文件夹变动则紧接着再扫一次
    QMetaObject::invokeMethod(this, [this]() {
        if (scanning_) return;
        job_.reset();
        if (rescanPending_) {
            rescanPending_ = false;
            if (folders_.isEmpty()) finishScan(std::make_shared<LibrarySnapshot>(), IngestionSummary{});
            else startScan();
        }
    }, Qt::QueuedConnection);
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
        a.sortKey = SortKeyOf(a.title.toStdWString());
    }
    std::sort(snap->albums.begin(), snap->albums.end(),
              [](const AlbumInfo& x, const AlbumInfo& y) { return x.sortKey < y.sortKey; });
    for (int i = 0; i < static_cast<int>(snap->albums.size()); ++i)
        for (const TrackItem* t : snap->albums[i].tracks) snap->trackAlbum[t->id] = i;

    // ---- 歌手 ----
    std::unordered_map<std::wstring, int> artistIdx;
    for (int ai = 0; ai < static_cast<int>(snap->albums.size()); ++ai) {
        for (const TrackItem* t : snap->albums[ai].tracks) {
            const std::wstring& raw = !t->artist.empty() ? t->artist : t->album_artist;
            std::vector<std::wstring> names = SplitArtists(raw);
            if (names.empty()) names.push_back(L"\u672a\u77e5\u6b4c\u624b"); // 未知歌手
            for (const std::wstring& n : names) {
                std::wstring key;
                for (wchar_t c : n) key += static_cast<wchar_t>(towlower(c));
                auto it = artistIdx.find(key);
                int idx;
                if (it == artistIdx.end()) {
                    idx = static_cast<int>(snap->artists.size());
                    artistIdx.emplace(key, idx);
                    snap->artists.emplace_back();
                    snap->artists.back().name = Q(n);
                } else {
                    idx = it->second;
                }
                ArtistInfo& ar = snap->artists[idx];
                ar.tracks.push_back(t);
                if (ar.albums.empty() || ar.albums.back() != ai) ar.albums.push_back(ai);
            }
        }
    }
    for (ArtistInfo& ar : snap->artists) {
        ar.sortKey = SortKeyOf(ar.name.toStdWString());
        ar.letter = LetterOf(ar.sortKey);
        // 头像:该歌手曲目最多且有封面的专辑
        int best = -1;
        size_t bestN = 0;
        for (int ai : ar.albums) {
            const AlbumInfo& a = snap->albums[ai];
            size_t n = 0;
            for (const TrackItem* t : ar.tracks) n += snap->trackAlbum[t->id] == ai;
            bool cover = a.coverTrack && (a.coverTrack->has_cover || !a.coverTrack->external_cover_path.empty());
            n = n * 2 + (cover ? 1000 : 0);
            if (best < 0 || n > bestN) best = ai, bestN = n;
        }
        ar.coverAlbum = best;
        std::stable_sort(ar.albums.begin(), ar.albums.end(), [&](int x, int y) {
            return snap->albums[x].year > snap->albums[y].year;
        });
    }
    std::sort(snap->artists.begin(), snap->artists.end(),
              [](const ArtistInfo& x, const ArtistInfo& y) { return x.sortKey < y.sortKey; });

    // ---- 全部歌曲 (按标题拼音) ----
    std::vector<std::pair<QString, const TrackItem*>> keyed;
    keyed.reserve(tracks.size());
    for (const auto& t : tracks) keyed.emplace_back(SortKeyOf(t.title), &t);
    std::sort(keyed.begin(), keyed.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    snap->songs.reserve(keyed.size());
    snap->songLetters.reserve(keyed.size());
    for (const auto& k : keyed) {
        snap->songs.push_back(k.second);
        snap->songLetters.push_back(LetterOf(k.first));
    }
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

QVariantMap LibraryService::artistInfo(int i) const {
    QVariantMap m;
    auto s = snapshot();
    if (i < 0 || i >= static_cast<int>(s->artists.size())) return m;
    const ArtistInfo& a = s->artists[i];
    uint64_t ms = 0;
    for (const TrackItem* t : a.tracks) ms += t->duration_ms;
    m["index"] = i;
    m["name"] = a.name;
    m["cover"] = a.coverAlbum >= 0 ? QString("image://cover/a/%1/%2").arg(generation_).arg(a.coverAlbum) : QString();
    m["albumCount"] = static_cast<int>(a.albums.size());
    m["trackCount"] = static_cast<int>(a.tracks.size());
    m["summary"] = tr("%1 张专辑  ·  %2 首  ·  %3").arg(a.albums.size()).arg(a.tracks.size()).arg(formatDuration(ms));
    return m;
}

QVariantList LibraryService::artistAlbums(int i) const {
    QVariantList out;
    auto s = snapshot();
    if (i < 0 || i >= static_cast<int>(s->artists.size())) return out;
    for (int ai : s->artists[i].albums) out.append(albumInfo(ai));
    return out;
}

int LibraryService::artistIndexOf(const QString& name) const {
    auto s = snapshot();
    for (int i = 0; i < static_cast<int>(s->artists.size()); ++i)
        if (s->artists[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    // 合作曲目:取第一位歌手
    std::vector<std::wstring> parts = SplitArtists(name.toStdWString());
    if (parts.size() > 1) return artistIndexOf(QString::fromStdWString(parts.front()));
    return -1;
}

int LibraryService::albumOfTrack(double trackId) const {
    auto s = snapshot();
    auto it = s->trackAlbum.find(static_cast<uint64_t>(trackId));
    return it == s->trackAlbum.end() ? -1 : it->second;
}

int LibraryService::artistOfTrack(double trackId) const {
    auto s = snapshot();
    const TrackItem* t = s->catalog.FindById(static_cast<uint64_t>(trackId));
    return t ? artistIndexOf(Q(t->artist.empty() ? t->album_artist : t->artist)) : -1;
}
