#include "models.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

#include "pinyin_table.hpp"
#include "playlist_service.h"

namespace {
QString Q(const std::wstring& w) { return QString::fromStdWString(w); }
QString AlbumCover(int gen, int idx) { return QString("image://cover/a/%1/%2").arg(gen).arg(idx); }
QString TrackCover(int gen, uint64_t id) { return QString("image://cover/t/%1/%2").arg(gen).arg(id); }
} // namespace

// ============================================================================
AlbumModel::AlbumModel(LibraryService* lib, QObject* parent) : QAbstractListModel(parent), lib_(lib) {
    snap_ = lib_->snapshot();
    connect(lib_, &LibraryService::libraryChanged, this, &AlbumModel::reload);
}

void AlbumModel::reload() {
    beginResetModel();
    snap_ = lib_->snapshot();
    endResetModel();
}

int AlbumModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(snap_->albums.size());
}

QVariant AlbumModel::data(const QModelIndex& index, int role) const {
    int r = index.row();
    if (r < 0 || r >= rowCount()) return {};
    const AlbumInfo& a = snap_->albums[r];
    switch (role) {
        case TitleRole: return a.title;
        case ArtistRole: return a.artist;
        case YearRole: return a.year;
        case CountRole: return static_cast<int>(a.tracks.size());
        case DurationRole: return LibraryService::formatDuration(a.totalMs);
        case BadgeRole: return lib_->qualityBadge(r);
        case CoverRole: return AlbumCover(lib_->generation(), r);
        case IndexRole: return r;
    }
    return {};
}

QHash<int, QByteArray> AlbumModel::roleNames() const {
    return {{TitleRole, "title"}, {ArtistRole, "artist"}, {YearRole, "year"},     {CountRole, "trackCount"},
            {DurationRole, "duration"}, {BadgeRole, "badge"}, {CoverRole, "cover"}, {IndexRole, "albumIndex"}};
}

// ============================================================================
TrackListModel::TrackListModel(LibraryService* lib, QObject* parent) : QAbstractListModel(parent), lib_(lib) {
    snap_ = lib_->snapshot();
    connect(lib_, &LibraryService::libraryChanged, this, &TrackListModel::reresolve);
}

void TrackListModel::reresolve() {
    auto fresh = lib_->snapshot();
    if (fresh == snap_) return;
    int found = -1;
    if (const AlbumInfo* a = current()) {
        // 用旧快照 (仍然有效) 里这张专辑的曲目标识,到新快照里找第一首还在的
        std::unordered_set<QString> keys;
        for (const TrackItem* t : a->tracks) keys.insert(TrackKeyOf(*t));
        for (const TrackItem& t : fresh->catalog.GetAllTracks()) {
            if (!keys.count(TrackKeyOf(t))) continue;
            auto it = fresh->trackAlbum.find(t.id);
            if (it != fresh->trackAlbum.end()) {
                found = it->second;
                break;
            }
        }
    }
    beginResetModel();
    snap_ = fresh;
    album_ = found;
    endResetModel();
    emit albumChanged();
}

const AlbumInfo* TrackListModel::current() const {
    if (album_ < 0 || album_ >= static_cast<int>(snap_->albums.size())) return nullptr;
    return &snap_->albums[album_];
}

void TrackListModel::setAlbumIndex(int i) {
    // 同一张专辑、同一份快照:不重置 (否则列表会无故跳回顶部)
    if (i == album_ && snap_ == lib_->snapshot()) return;
    beginResetModel();
    snap_ = lib_->snapshot();
    album_ = i;
    endResetModel();
    emit albumChanged();
}

int TrackListModel::rowCount(const QModelIndex& parent) const {
    const AlbumInfo* a = current();
    return parent.isValid() || !a ? 0 : static_cast<int>(a->tracks.size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const {
    const AlbumInfo* a = current();
    if (!a || index.row() < 0 || index.row() >= static_cast<int>(a->tracks.size())) return {};
    const TrackItem& t = *a->tracks[index.row()];
    switch (role) {
        case TrackIdRole: return static_cast<double>(t.id);
        case NumberRole: return t.track_no > 0 ? t.track_no : index.row() + 1;
        case TitleRole: return Q(t.title);
        case ArtistRole: return Q(t.artist);
        case DurationRole: return LibraryService::formatDuration(t.duration_ms);
        case QualityRole: return LibraryService::trackQuality(t);
        case DiscRole: return std::max(1, t.disc_no); // 没写碟号的算第 1 张,不单独分出「0」
    }
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
    return {{TrackIdRole, "trackId"},   {NumberRole, "number"},   {TitleRole, "title"}, {ArtistRole, "artist"},
            {DurationRole, "duration"}, {QualityRole, "quality"}, {DiscRole, "disc"}};
}

QString TrackListModel::title() const { return current() ? current()->title : QString(); }
QString TrackListModel::artist() const { return current() ? current()->artist : QString(); }
QString TrackListModel::year() const { return current() ? current()->year : QString(); }
QString TrackListModel::cover() const { return current() ? AlbumCover(lib_->generation(), album_) : QString(); }

QString TrackListModel::summary() const {
    const AlbumInfo* a = current();
    if (!a) return {};
    QStringList parts;
    if (!a->year.isEmpty()) parts << a->year;
    const QString g = genre();
    if (!g.isEmpty()) parts << g;
    parts << tr("%1 首").arg(a->tracks.size());
    parts << LibraryService::formatLongDuration(a->totalMs);
    return parts.join(QStringLiteral("  ·  "));
}

// 只留一个「档次」标签 (DSD / Hi-Res / 无损),具体的位深采样率由 formatText 用文字写出,不再叠三个相似的胶囊
QStringList TrackListModel::badges() const {
    const AlbumInfo* a = current();
    if (!a) return {};
    QStringList b;
    if (a->dsd) b << a->dsdCodec;
    else if (a->hires) b << QStringLiteral("Hi-Res");
    else if (a->lossless) b << tr("无损");
    if (a->cue) b << QStringLiteral("CUE");
    return b;
}

bool TrackListModel::multiDisc() const {
    const AlbumInfo* a = current();
    if (!a || a->tracks.empty()) return false;
    const int first = std::max(1, a->tracks.front()->disc_no);
    for (const TrackItem* t : a->tracks)
        if (std::max(1, t->disc_no) != first) return true;
    return false;
}

QString TrackListModel::commonQuality() const {
    const AlbumInfo* a = current();
    if (!a || a->tracks.empty()) return {};
    const QString q = LibraryService::trackQuality(*a->tracks.front());
    for (const TrackItem* t : a->tracks)
        if (LibraryService::trackQuality(*t) != q) return {};
    return q;
}

namespace {
QString KHz(uint32_t rate) {
    return rate % 1000 ? QString::number(rate / 1000.0, 'f', 1) : QString::number(rate / 1000);
}
QString Range(const QString& lo, const QString& hi) { return lo == hi ? lo : lo + QStringLiteral("–") + hi; }
} // namespace

QString TrackListModel::formatText() const {
    const QString head = codecText(), spec = specText();
    if (head.isEmpty()) return spec;
    return spec.isEmpty() ? head : head + QStringLiteral("  ·  ") + spec;
}

QString TrackListModel::codecText() const {
    const AlbumInfo* a = current();
    if (!a) return {};
    QStringList codecs;
    for (const TrackItem* t : a->tracks) {
        const QString c = Q(t->codec.empty() ? t->format : t->codec);
        if (!c.isEmpty() && !codecs.contains(c)) codecs << c;
    }
    return codecs.size() > 2 ? tr("%1 等 %2 种格式").arg(codecs.front()).arg(codecs.size()) : codecs.join(QStringLiteral(" / "));
}

// 位深 / 采样率 (无损);DSD 为 MHz;有损为最高码率。混合时给出范围
QString TrackListModel::specText() const {
    const AlbumInfo* a = current();
    if (!a || a->tracks.empty()) return {};
    uint32_t minRate = UINT32_MAX, maxRate = 0, maxKbps = 0;
    uint16_t minBits = UINT16_MAX, maxBits = 0;
    bool allLossless = true;
    for (const TrackItem* t : a->tracks) {
        allLossless = allLossless && t->is_lossless;
        if (t->sample_rate) minRate = std::min(minRate, t->sample_rate), maxRate = std::max(maxRate, t->sample_rate);
        if (t->bits_per_sample) minBits = std::min(minBits, t->bits_per_sample), maxBits = std::max(maxBits, t->bits_per_sample);
        maxKbps = std::max(maxKbps, t->bitrate_kbps);
    }
    QString spec;
    if (a->dsd && maxRate) {
        spec = Range(QString::number(minRate / 1e6, 'f', 1), QString::number(maxRate / 1e6, 'f', 1)) + QStringLiteral(" MHz");
    } else if (allLossless && maxRate) {
        if (maxBits) spec = Range(QString::number(minBits), QString::number(maxBits)) + QStringLiteral(" bit / ");
        spec += Range(KHz(minRate), KHz(maxRate)) + QStringLiteral(" kHz");
    } else if (maxKbps) {
        spec = tr("最高 %1 kbps").arg(maxKbps);
    }
    return spec;
}

QString TrackListModel::genre() const {
    const AlbumInfo* a = current();
    if (!a) return {};
    std::map<std::wstring, int> count;
    for (const TrackItem* t : a->tracks)
        if (!t->genre.empty()) ++count[t->genre];
    const std::wstring* best = nullptr;
    int n = 0;
    for (const auto& kv : count)
        if (kv.second > n) best = &kv.first, n = kv.second;
    return best ? Q(*best) : QString();
}

QVariantMap TrackListModel::details() const {
    QVariantMap m;
    const AlbumInfo* a = current();
    if (!a || a->tracks.empty()) return m;
    // CUE 分轨共用一个文件:大小按不同的文件累计
    std::set<std::wstring> files;
    std::set<QString> folders;
    uint64_t bytes = 0;
    std::set<int> discSet;
    for (const TrackItem* t : a->tracks) {
        if (files.insert(t->file_path).second) bytes += t->file_size;
        folders.insert(QFileInfo(Q(t->file_path)).absolutePath());
        discSet.insert(std::max(1, t->disc_no));
    }
    const TrackItem* front = a->tracks.front();
    m["size"] = static_cast<double>(bytes);
    m["folder"] = QDir::toNativeSeparators(QFileInfo(Q(front->file_path)).absolutePath());
    m["folderCount"] = static_cast<int>(folders.size());
    m["firstTrackId"] = static_cast<double>(front->id);
    m["discCount"] = static_cast<int>(discSet.size());
    m["albumGain"] = front->has_rg_album ? QString::asprintf("%+.2f dB", front->rg_album_gain) : QString();
    return m;
}

QVariantList TrackListModel::discs() const {
    QVariantList out;
    const AlbumInfo* a = current();
    if (!a) return out;
    int row = 0, disc = -1, count = 0, first = 0;
    uint64_t ms = 0;
    auto flush = [&]() {
        if (disc < 0) return;
        out.append(QVariantMap{{"disc", disc}, {"count", count}, {"duration", LibraryService::formatLongDuration(ms)}, {"firstRow", first}});
    };
    for (const TrackItem* t : a->tracks) {
        const int d = std::max(1, t->disc_no);
        if (d != disc) {
            flush();
            disc = d, count = 0, ms = 0, first = row;
        }
        ++count;
        ms += t->duration_ms;
        ++row;
    }
    flush();
    return out;
}

QVariantList TrackListModel::trackIds() const {
    QVariantList out;
    const AlbumInfo* a = current();
    if (!a) return out;
    out.reserve(static_cast<int>(a->tracks.size()));
    for (const TrackItem* t : a->tracks) out.append(static_cast<double>(t->id));
    return out;
}

// ============================================================================
namespace {
QVariant TrackRole(const LibrarySnapshot& snap, const TrackItem& t, int role, int gen, int titleRole, int artistRole,
                   int albumRole, int albumIndexRole, int durationRole, int qualityRole, int coverRole, int idRole) {
    if (role == idRole) return static_cast<double>(t.id);
    if (role == titleRole) return Q(t.title);
    if (role == artistRole) return Q(t.artist);
    if (role == albumRole) return Q(t.album);
    if (role == durationRole) return LibraryService::formatDuration(t.duration_ms);
    if (role == qualityRole) return LibraryService::trackQuality(t);
    if (role == albumIndexRole || role == coverRole) {
        auto it = snap.trackAlbum.find(t.id);
        int ai = it == snap.trackAlbum.end() ? -1 : it->second;
        if (role == albumIndexRole) return ai;
        return ai >= 0 ? AlbumCover(gen, ai) : TrackCover(gen, t.id);
    }
    return {};
}
} // namespace

// ============================================================================
ArtistModel::ArtistModel(LibraryService* lib, QObject* parent) : QAbstractListModel(parent), lib_(lib) {
    reload();
    connect(lib_, &LibraryService::libraryChanged, this, &ArtistModel::reload);
}

void ArtistModel::reload() {
    beginResetModel();
    snap_ = lib_->snapshot();
    letters_.clear();
    for (const ArtistInfo& a : snap_->artists)
        if (letters_.isEmpty() || letters_.back() != a.letter) letters_ << a.letter;
    endResetModel();
    emit lettersChanged();
}

int ArtistModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(snap_->artists.size());
}

QVariant ArtistModel::data(const QModelIndex& index, int role) const {
    int r = index.row();
    if (r < 0 || r >= rowCount()) return {};
    const ArtistInfo& a = snap_->artists[r];
    switch (role) {
        case NameRole: return a.name;
        case SummaryRole: return tr("%1 首").arg(a.tracks.size());
        case CoverRole: return a.coverAlbum >= 0 ? AlbumCover(lib_->generation(), a.coverAlbum) : QString();
        case IndexRole: return r;
        case LetterRole: return a.letter;
    }
    return {};
}

QHash<int, QByteArray> ArtistModel::roleNames() const {
    return {{NameRole, "name"}, {SummaryRole, "summary"}, {CoverRole, "cover"}, {IndexRole, "artistIndex"}, {LetterRole, "letter"}};
}

int ArtistModel::indexOfLetter(const QString& letter) const {
    for (int i = 0; i < static_cast<int>(snap_->artists.size()); ++i)
        if (snap_->artists[i].letter == letter) return i;
    return -1;
}

// ============================================================================
TrackCollectionModel::TrackCollectionModel(LibraryService* lib, PlaylistService* playlists, QObject* parent)
    : QAbstractListModel(parent), lib_(lib), playlists_(playlists) {
    reload();
    connect(lib_, &LibraryService::libraryChanged, this, &TrackCollectionModel::reload);
    connect(playlists_, &PlaylistService::contentChanged, this, [this](const QString& id) {
        if (source_ == QLatin1String("playlist") && id == playlist_) reload();
    });
    connect(playlists_, &PlaylistService::historyChanged, this, [this]() {
        if (source_ == QLatin1String("history")) reload();
    });
}

void TrackCollectionModel::setSource(const QString& s) {
    if (s == source_) return;
    source_ = s;
    reload();
    emit sourceChanged();
}
void TrackCollectionModel::setArtistIndex(int i) {
    if (i == artist_ && source_ == QLatin1String("artist")) return;
    artist_ = i;
    reload();
    emit sourceChanged();
}
void TrackCollectionModel::setPlaylistId(const QString& id) {
    if (id == playlist_ && source_ == QLatin1String("playlist")) return;
    playlist_ = id;
    reload();
    emit sourceChanged();
}

void TrackCollectionModel::reload() {
    beginResetModel();
    snap_ = lib_->snapshot();
    rows_.clear();
    rowLetters_.clear();
    letters_.clear();
    if (source_ == QLatin1String("all")) {
        rows_ = snap_->songs;
        rowLetters_ = snap_->songLetters;
        for (const QString& l : rowLetters_)
            if (letters_.isEmpty() || letters_.back() != l) letters_ << l;
    } else if (source_ == QLatin1String("artist")) {
        if (artist_ >= 0 && artist_ < static_cast<int>(snap_->artists.size())) rows_ = snap_->artists[artist_].tracks;
    } else if (source_ == QLatin1String("playlist")) {
        rows_ = playlists_->tracksOf(playlist_);
    } else if (source_ == QLatin1String("history")) {
        rows_ = playlists_->historyTracks();
    }
    endResetModel();
    emit contentChanged();
}

int TrackCollectionModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant TrackCollectionModel::data(const QModelIndex& index, int role) const {
    int r = index.row();
    if (r < 0 || r >= count()) return {};
    if (role == LetterRole) return r < static_cast<int>(rowLetters_.size()) ? rowLetters_[r] : QString();
    if (role == NumberRole) return r + 1;
    return TrackRole(*snap_, *rows_[r], role, lib_->generation(), TitleRole, ArtistRole, AlbumRole, AlbumIndexRole,
                     DurationRole, QualityRole, CoverRole, TrackIdRole);
}

QHash<int, QByteArray> TrackCollectionModel::roleNames() const {
    return {{TrackIdRole, "trackId"},   {TitleRole, "title"},     {ArtistRole, "artist"}, {AlbumRole, "album"},
            {AlbumIndexRole, "albumIndex"}, {DurationRole, "duration"}, {QualityRole, "quality"}, {CoverRole, "cover"},
            {LetterRole, "letter"},     {NumberRole, "number"}};
}

QVariantList TrackCollectionModel::trackIds() const {
    QVariantList out;
    out.reserve(static_cast<int>(rows_.size()));
    for (const TrackItem* t : rows_) out.append(static_cast<double>(t->id));
    return out;
}

int TrackCollectionModel::indexOfLetter(const QString& letter) const {
    for (int i = 0; i < static_cast<int>(rowLetters_.size()); ++i)
        if (rowLetters_[i] == letter) return i;
    return -1;
}

int TrackCollectionModel::rowOfTrack(double trackId) const {
    for (int i = 0; i < count(); ++i)
        if (rows_[i]->id == static_cast<uint64_t>(trackId)) return i;
    return -1;
}

// ============================================================================
SearchModel::SearchModel(LibraryService* lib, QObject* parent) : QAbstractListModel(parent), lib_(lib) {
    snap_ = lib_->snapshot();
    connect(lib_, &LibraryService::libraryChanged, this, [this]() { run(); });
}

void SearchModel::setQuery(const QString& q) {
    if (q == query_) return;
    query_ = q;
    emit queryChanged();
    run();
}

void SearchModel::run() {
    beginResetModel();
    snap_ = lib_->snapshot();
    rows_.clear();
    albums_.clear();
    artists_.clear();
    top_.clear();
    const QString trimmed = query_.trimmed();
    std::wstring wq = trimmed.toStdWString();
    if (!wq.empty()) {
        const int gen = lib_->generation();
        int exactArtist = -1, exactAlbum = -1;
        for (int i = 0; i < static_cast<int>(snap_->artists.size()); ++i) {
            const ArtistInfo& a = snap_->artists[i];
            if (!PinyinHelper::Matches(a.name.toStdWString(), wq)) continue;
            if (exactArtist < 0 && (a.name.compare(trimmed, Qt::CaseInsensitive) == 0 ||
                                    QString::fromStdString(PinyinHelper::GetFullPinyin(a.name.toStdWString())) ==
                                        trimmed.toLower().remove(' ')))
                exactArtist = i;
            if (artists_.size() < 12) artists_.append(lib_->artistInfo(i));
        }
        for (int i = 0; i < static_cast<int>(snap_->albums.size()); ++i) {
            const AlbumInfo& a = snap_->albums[i];
            if (!PinyinHelper::Matches(a.title.toStdWString(), wq) && !PinyinHelper::Matches(a.artist.toStdWString(), wq))
                continue;
            if (exactAlbum < 0 && a.title.compare(trimmed, Qt::CaseInsensitive) == 0) exactAlbum = i;
            if (albums_.size() < 12) albums_.append(lib_->albumInfo(i));
        }
        auto hits = snap_->catalog.Search(wq, true);
        for (const TrackItem* t : hits) {
            if (rows_.size() >= 80) break;
            // 隐藏文件夹里的歌不进任何专辑,搜索里也不出现
            if (!snap_->trackAlbum.count(t->id)) continue;
            rows_.push_back(t);
        }
        // 最佳匹配:精确歌手 > 精确专辑 > 第一首歌 > 第一张专辑 > 第一位歌手
        if (exactArtist >= 0) {
            top_ = lib_->artistInfo(exactArtist);
            top_["kind"] = QStringLiteral("artist");
        } else if (exactAlbum >= 0) {
            top_ = lib_->albumInfo(exactAlbum);
            top_["kind"] = QStringLiteral("album");
        } else if (!rows_.empty()) {
            const TrackItem& t = *rows_.front();
            auto it = snap_->trackAlbum.find(t.id);
            top_["kind"] = QStringLiteral("track");
            top_["trackId"] = static_cast<double>(t.id);
            top_["title"] = Q(t.title);
            top_["subtitle"] = Q(t.artist);
            top_["cover"] = it == snap_->trackAlbum.end() ? TrackCover(gen, t.id) : AlbumCover(gen, it->second);
            top_["badge"] = LibraryService::trackQuality(t);
        } else if (!albums_.isEmpty()) {
            top_ = albums_.front().toMap();
            top_["kind"] = QStringLiteral("album");
        } else if (!artists_.isEmpty()) {
            top_ = artists_.front().toMap();
            top_["kind"] = QStringLiteral("artist");
        }
    }
    endResetModel();
    emit resultsChanged();
}

int SearchModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant SearchModel::data(const QModelIndex& index, int role) const {
    if (index.row() < 0 || index.row() >= count()) return {};
    return TrackRole(*snap_, *rows_[index.row()], role, lib_->generation(), TitleRole, ArtistRole, AlbumRole,
                     AlbumIndexRole, DurationRole, QualityRole, CoverRole, TrackIdRole);
}

QHash<int, QByteArray> SearchModel::roleNames() const {
    return {{TrackIdRole, "trackId"},     {TitleRole, "title"},       {ArtistRole, "artist"},
            {AlbumRole, "album"},         {AlbumIndexRole, "albumIndex"}, {DurationRole, "duration"},
            {QualityRole, "quality"},     {CoverRole, "cover"}};
}

QVariantList SearchModel::trackIds() const {
    QVariantList out;
    for (const TrackItem* t : rows_) out.append(static_cast<double>(t->id));
    return out;
}
