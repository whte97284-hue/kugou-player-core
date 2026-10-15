#include "playlist_importer.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "playlist_service.h"

using namespace playlist_core;

namespace {
QString Q(const std::wstring& w) { return QString::fromStdWString(w); }
QString AlbumCover(int gen, int idx) { return QString("image://cover/a/%1/%2").arg(gen).arg(idx); }

struct Matched {
    std::vector<uint64_t> ids;
    std::vector<int> levels;
    MatchSummary sum;
};

Matched MatchSnapshot(const LibrarySnapshot& snap, const std::vector<PlaylistTrack>& tracks) {
    std::vector<LocalTrack> lib;
    lib.reserve(snap.songs.size());
    for (const TrackItem* t : snap.songs) {
        LocalTrack l;
        l.id = t->id;
        l.title = t->title;
        l.artist = t->artist;
        l.file_name = t->file_name;
        l.duration_ms = t->duration_ms;
        l.lossless = t->is_lossless;
        l.bits_per_sample = t->bits_per_sample;
        l.sample_rate = static_cast<int>(t->sample_rate);
        l.bitrate_kbps = static_cast<int>(t->bitrate_kbps);
        lib.push_back(std::move(l));
    }
    Matched m;
    const auto res = MatchToLibrary(tracks, lib, {}, &m.sum);
    m.ids.resize(tracks.size(), 0);
    m.levels.resize(tracks.size(), 0);
    for (size_t i = 0; i < res.size(); ++i) {
        m.levels[i] = static_cast<int>(res[i].level);
        if (res[i].level != MatchLevel::Missing) m.ids[i] = lib[res[i].local].id;
    }
    return m;
}

QString PlatformLabel(const std::string& source) {
    if (source == "kugou") return QObject::tr("酷狗");
    if (source == "netease") return QObject::tr("网易云");
    return {};
}

// 假歌单:从曲库里隔几首挑一首 (其中一首带括号注释、一首时长差几秒),再插几首曲库里没有的
std::string FakeKugouPage(const LibrarySnapshot& snap) {
    QJsonArray arr;
    auto add = [&](const QString& title, const QString& artist, qint64 ms, const QString& album) {
        arr.append(QJsonObject{{"songname", title}, {"singername", artist}, {"duration", ms},
                               {"hash", QString::number(arr.size() + 1000)}, {"album_name", album}});
    };
    const struct { const char* t; const char* a; int ms; } missing[] = {
        {"稻香", "周杰伦", 223000}, {"后来", "刘若英", 341000}, {"小幸运", "田馥甄", 265000},
        {"Creep", "Radiohead", 238000}, {"光年之外", "G.E.M.邓紫棋", 235000}, {"七里香", "周杰伦", 299000}};
    const size_t n = snap.songs.size();
    const size_t step = std::max<size_t>(1, n / 14);
    int k = 0, miss = 0;
    for (size_t i = 0; i < n && k < 14; i += step, ++k) {
        const TrackItem* t = snap.songs[i];
        QString title = Q(t->title);
        qint64 ms = t->duration_ms;
        if (k == 2) title += QStringLiteral(" (电影《不能说的秘密》插曲)");
        if (k == 5) ms += 5500;
        add(title, Q(t->artist), ms, Q(t->album));
        if (k % 2 == 1 && miss < 6) {
            add(QString::fromUtf8(missing[miss].t), QString::fromUtf8(missing[miss].a), missing[miss].ms, {});
            ++miss;
        }
    }
    for (; miss < 6; ++miss) add(QString::fromUtf8(missing[miss].t), QString::fromUtf8(missing[miss].a), missing[miss].ms, {});
    std::string page = "<script>\nvar global = { name: \"周末午后\", pic: \"\" };\nglobal.data = ";
    page += QJsonDocument(arr).toJson(QJsonDocument::Compact).toStdString();
    page += ";\n</script>";
    return page;
}
} // namespace

PlaylistImporter::PlaylistImporter(LibraryService* lib, PlaylistService* playlists, QObject* parent)
    : QObject(parent), lib_(lib), playlists_(playlists) {
    connect(lib_, &LibraryService::libraryChanged, this, [this]() {
        if (state_ == QLatin1String("ready")) rematch(true);
    });
}

PlaylistImporter::~PlaylistImporter() { *alive_ = false; }

QString PlaylistImporter::detect(const QString& text) const {
    Platform p = Platform::Unknown;
    std::string id;
    if (!PlaylistProvider::ParseInput(text.trimmed().toStdString(), p, id)) return {};
    if (p == Platform::Kugou) return QStringLiteral("kugou");
    if (p == Platform::NetEase) return QStringLiteral("netease");
    return QStringLiteral("id");
}

void PlaylistImporter::setState(const QString& s, const QString& err) {
    if (state_ == s && error_ == err) return;
    state_ = s;
    error_ = err;
    emit stateChanged();
}

void PlaylistImporter::parse(const QString& text) {
    const std::string in = text.trimmed().toStdString();
    if (in.empty()) return;
    Platform p = Platform::Unknown;
    std::string id;
    if (!PlaylistProvider::ParseInput(in, p, id)) {
        // 认不出的链接可能是短链:交给后台跟随跳转后再认
        if (text.contains(QStringLiteral("://"))) {
            start(Platform::Unknown, text.trimmed());
            return;
        }
        setState(QStringLiteral("error"), tr("认不出这个链接,请粘贴酷狗或网易云的歌单分享链接"));
        return;
    }
    if (p == Platform::Unknown) {
        pendingId_ = QString::fromStdString(id);
        ++gen_;
        setState(QStringLiteral("platform"));
        return;
    }
    start(p, QString::fromStdString(id));
}

void PlaylistImporter::parseAs(const QString& platform) {
    if (pendingId_.isEmpty()) return;
    start(platform == QLatin1String("netease") ? Platform::NetEase : Platform::Kugou, pendingId_);
}

HttpGetFn PlaylistImporter::httpFn() const {
    if (qEnvironmentVariableIntValue("KP_TEST_PLAYLIST_FAKE") == 0) return nullptr;
    auto page = std::make_shared<std::string>(FakeKugouPage(*lib_->snapshot()));
    return [page](const std::string& url, std::string& body) {
        QThread::msleep(700);
        if (url.find("kugou") == std::string::npos) return false;
        body = *page;
        return true;
    };
}

void PlaylistImporter::start(Platform plat, const QString& input) {
    const int gen = ++gen_;
    setState(QStringLiteral("loading"));
    auto snap = lib_->snapshot();
    const HttpGetFn http = httpFn();
    const std::string in = input.toStdString();
    QPointer<PlaylistImporter> self(this);
    auto alive = alive_;
    QThreadPool::globalInstance()->start([self, alive, gen, plat, in, snap, http]() {
        PlaylistProvider pp(http);
        auto res = std::make_shared<PlaylistResult>(plat == Platform::Unknown ? pp.Parse(in) : pp.ParseAs(plat, in));
        auto matched = std::make_shared<Matched>();
        if (res->ok) *matched = MatchSnapshot(*snap, res->tracks);
        if (!*alive) return;
        QMetaObject::invokeMethod(qApp, [self, gen, res, matched]() {
            if (!self || self->gen_ != gen) return;
            if (!res->ok) {
                self->setState(QStringLiteral("error"), res->error.empty() ? tr("没能读取这个歌单") : QString::fromStdString(res->error));
                return;
            }
            self->result_ = std::move(*res);
            self->matchedIds_ = std::move(matched->ids);
            self->levels_ = std::move(matched->levels);
            self->sum_ = matched->sum;
            self->rematch(false);  // 生成界面数据 (曲库没变时不再重算)
            self->setState(QStringLiteral("ready"));
        }, Qt::QueuedConnection);
    });
}

void PlaylistImporter::rematch(bool force) {
    auto snap = lib_->snapshot();
    // 曲库变了 / 曲目 ID 失效时重新匹配
    bool stale = matchedIds_.size() != result_.tracks.size();
    if (!stale && !force) {
        std::unordered_set<uint64_t> live;
        for (const TrackItem* t : snap->songs) live.insert(t->id);
        for (uint64_t id : matchedIds_)
            if (id && !live.count(id)) { stale = true; break; }
    }
    if (stale || force) {
        Matched m = MatchSnapshot(*snap, result_.tracks);
        matchedIds_ = std::move(m.ids);
        levels_ = std::move(m.levels);
        sum_ = m.sum;
    }

    std::unordered_map<uint64_t, const TrackItem*> byId;
    for (const TrackItem* t : snap->songs) byId[t->id] = t;
    const int libGen = lib_->generation();
    QStringList covers;
    std::vector<int> seenAlbums;
    QVariantList rows;
    rows.reserve(static_cast<int>(result_.tracks.size()));
    for (size_t i = 0; i < result_.tracks.size(); ++i) {
        const PlaylistTrack& p = result_.tracks[i];
        QVariantMap r;
        r["title"] = Q(p.title);
        r["artist"] = Q(p.artist);
        r["album"] = Q(p.album);
        r["duration"] = p.duration_ms / 1000.0;
        r["level"] = levels_[i];
        r["trackId"] = -1.0;
        auto it = matchedIds_[i] ? byId.find(matchedIds_[i]) : byId.end();
        if (it != byId.end()) {
            const TrackItem* t = it->second;
            r["trackId"] = static_cast<double>(t->id);
            QString fmt = Q(t->format);
            if (t->is_hires) fmt = QStringLiteral("Hi-Res");
            else if (!t->is_lossless && t->bitrate_kbps > 0) fmt += QStringLiteral(" %1k").arg(t->bitrate_kbps);
            r["local"] = fmt;
            auto al = snap->trackAlbum.find(t->id);
            if (al != snap->trackAlbum.end()) {
                r["cover"] = AlbumCover(libGen, al->second);
                if (covers.size() < 4 && levels_[i] == 2 &&
                    std::find(seenAlbums.begin(), seenAlbums.end(), al->second) == seenAlbums.end()) {
                    seenAlbums.push_back(al->second);
                    covers << AlbumCover(libGen, al->second);
                }
            }
        }
        rows.push_back(r);
    }
    rows_ = rows;
    meta_ = QVariantMap{{"name", Q(result_.meta.name)},
                        {"author", Q(result_.meta.author)},
                        {"source", PlatformLabel(result_.meta.source)},
                        {"total", static_cast<int>(result_.tracks.size())},
                        {"covers", covers}};
    emit resultChanged();
}

QString PlaylistImporter::save(const QString& name, bool includeLikely) {
    if (state_ != QLatin1String("ready")) return {};
    QVariantList ids;
    for (size_t i = 0; i < matchedIds_.size(); ++i)
        if (matchedIds_[i] && (levels_[i] == 2 || (includeLikely && levels_[i] == 1)))
            ids.push_back(static_cast<double>(matchedIds_[i]));
    if (ids.isEmpty()) return {};
    QString n = name.trimmed();
    if (n.isEmpty()) n = Q(result_.meta.name);
    const QString id = playlists_->create(n);
    playlists_->addTracks(id, ids);
    return id;
}

int PlaylistImporter::copyMissing() const {
    QStringList lines;
    for (size_t i = 0; i < result_.tracks.size() && i < levels_.size(); ++i) {
        if (levels_[i] != 0) continue;
        const PlaylistTrack& p = result_.tracks[i];
        lines << (p.artist.empty() ? Q(p.title) : Q(p.artist) + QStringLiteral(" - ") + Q(p.title));
    }
    if (!lines.isEmpty()) QGuiApplication::clipboard()->setText(lines.join('\n'));
    return static_cast<int>(lines.size());
}

void PlaylistImporter::reset() {
    ++gen_;
    pendingId_.clear();
    result_ = {};
    matchedIds_.clear();
    levels_.clear();
    sum_ = {};
    rows_.clear();
    meta_.clear();
    emit resultChanged();
    setState(QStringLiteral("idle"));
}
