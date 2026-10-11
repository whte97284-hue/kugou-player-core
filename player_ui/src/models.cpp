#include "models.h"

#include <algorithm>

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
    connect(lib_, &LibraryService::libraryChanged, this, [this]() { setAlbumIndex(-1); });
}

const AlbumInfo* TrackListModel::current() const {
    if (album_ < 0 || album_ >= static_cast<int>(snap_->albums.size())) return nullptr;
    return &snap_->albums[album_];
}

void TrackListModel::setAlbumIndex(int i) {
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
        case DiscRole: return t.disc_no;
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
    parts << tr("%1 首").arg(a->tracks.size());
    parts << LibraryService::formatDuration(a->totalMs);
    return parts.join(QStringLiteral("  ·  "));
}

QStringList TrackListModel::badges() const {
    const AlbumInfo* a = current();
    if (!a) return {};
    QStringList b;
    QString q = lib_->qualityBadge(album_);
    if (!q.isEmpty()) b << q;
    if (a->hires && !a->dsd) b << QStringLiteral("Hi-Res");
    if (!a->tracks.empty()) b << Q(a->tracks.front()->codec);
    if (a->cue) b << QStringLiteral("CUE");
    b.removeDuplicates();
    return b;
}

bool TrackListModel::multiDisc() const {
    const AlbumInfo* a = current();
    return a && !a->tracks.empty() && a->tracks.front()->disc_no != a->tracks.back()->disc_no;
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
