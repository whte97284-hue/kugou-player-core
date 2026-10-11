#include "models.h"

#include <algorithm>

#include "pinyin_table.hpp"

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
SearchModel::SearchModel(LibraryService* lib, QObject* parent) : QAbstractListModel(parent), lib_(lib) {
    snap_ = lib_->snapshot();
    connect(lib_, &LibraryService::libraryChanged, this, [this]() {
        QString q = query_;
        query_.clear();
        setQuery(q);
    });
}

void SearchModel::setQuery(const QString& q) {
    if (q == query_ && !rows_.empty()) return;
    beginResetModel();
    snap_ = lib_->snapshot();
    query_ = q;
    rows_.clear();
    std::wstring wq = q.trimmed().toStdWString();
    if (!wq.empty()) {
        // 专辑:标题或歌手匹配 (支持拼音 / 首字母 / 简繁)
        for (int i = 0; i < static_cast<int>(snap_->albums.size()) && rows_.size() < 6; ++i) {
            const AlbumInfo& a = snap_->albums[i];
            if (PinyinHelper::Matches(a.title.toStdWString(), wq) || PinyinHelper::Matches(a.artist.toStdWString(), wq))
                rows_.push_back({true, i, nullptr});
        }
        // 曲目:内核相关度排序 + 同名多版本折叠
        auto hits = snap_->catalog.Search(wq, true);
        for (const TrackItem* t : hits) {
            if (rows_.size() >= 40) break;
            rows_.push_back({false, -1, t});
        }
    }
    endResetModel();
    emit queryChanged();
    emit countChanged();
}

int SearchModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant SearchModel::data(const QModelIndex& index, int role) const {
    if (index.row() < 0 || index.row() >= count()) return {};
    const Row& r = rows_[index.row()];
    int gen = lib_->generation();
    if (r.album) {
        const AlbumInfo& a = snap_->albums[r.albumIndex];
        switch (role) {
            case KindRole: return QStringLiteral("album");
            case TitleRole: return a.title;
            case SubtitleRole: return tr("专辑 · %1").arg(a.artist);
            case CoverRole: return AlbumCover(gen, r.albumIndex);
            case RefRole: return r.albumIndex;
            case QualityRole: return lib_->qualityBadge(r.albumIndex);
        }
        return {};
    }
    const TrackItem& t = *r.track;
    switch (role) {
        case KindRole: return QStringLiteral("track");
        case TitleRole: return Q(t.title);
        case SubtitleRole: return Q(t.artist) + QStringLiteral(" · ") + Q(t.album);
        case CoverRole: return TrackCover(gen, t.id);
        case RefRole: return static_cast<double>(t.id);
        case QualityRole: return LibraryService::trackQuality(t);
    }
    return {};
}

QHash<int, QByteArray> SearchModel::roleNames() const {
    return {{KindRole, "kind"}, {TitleRole, "title"}, {SubtitleRole, "subtitle"},
            {CoverRole, "cover"}, {RefRole, "ref"},   {QualityRole, "quality"}};
}
