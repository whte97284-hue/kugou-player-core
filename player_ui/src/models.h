#pragma once
#include <QAbstractListModel>
#include <QString>
#include <memory>

#include "library_service.h"

// 专辑墙
class AlbumModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TitleRole = Qt::UserRole + 1, ArtistRole, YearRole, CountRole, DurationRole, BadgeRole, CoverRole, IndexRole };
    explicit AlbumModel(LibraryService* lib, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void reload();
    LibraryService* lib_;
    std::shared_ptr<const LibrarySnapshot> snap_;
};

// 专辑详情的曲目列表
class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int albumIndex READ albumIndex WRITE setAlbumIndex NOTIFY albumChanged)
    Q_PROPERTY(QString title READ title NOTIFY albumChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY albumChanged)
    Q_PROPERTY(QString year READ year NOTIFY albumChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY albumChanged)
    Q_PROPERTY(QString cover READ cover NOTIFY albumChanged)
    Q_PROPERTY(QStringList badges READ badges NOTIFY albumChanged)
    Q_PROPERTY(bool multiDisc READ multiDisc NOTIFY albumChanged)
    // 整张专辑共同的音质 (每首都一样时);不一样时为空,曲目行只给与众不同的那几首标音质
    Q_PROPERTY(QString commonQuality READ commonQuality NOTIFY albumChanged)
    // 格式说明:「FLAC · 24 bit / 96 kHz」;混合时给出范围
    Q_PROPERTY(QString formatText READ formatText NOTIFY albumChanged)
    Q_PROPERTY(QString codecText READ codecText NOTIFY albumChanged) // 「FLAC」「FLAC / MP3」
    Q_PROPERTY(QString specText READ specText NOTIFY albumChanged)   // 「24 bit / 96 kHz」「16–24 bit / 44.1–96 kHz」
    Q_PROPERTY(QString genre READ genre NOTIFY albumChanged)
    // 页尾的专辑资料:{ size, folder, folderCount, firstTrackId, albumGain, discCount }
    Q_PROPERTY(QVariantMap details READ details NOTIFY albumChanged)
    // 每张碟:[{ disc, count, duration, firstRow }] (按曲目顺序)
    Q_PROPERTY(QVariantList discs READ discs NOTIFY albumChanged)
    // 全部曲目 ID (整张加入队列 / 歌单 / 收藏)
    Q_PROPERTY(QVariantList trackIds READ trackIds NOTIFY albumChanged)
public:
    enum Roles { TrackIdRole = Qt::UserRole + 1, NumberRole, TitleRole, ArtistRole, DurationRole, QualityRole, DiscRole };
    explicit TrackListModel(LibraryService* lib, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int albumIndex() const { return album_; }
    void setAlbumIndex(int i);
    QString title() const;
    QString artist() const;
    QString year() const;
    QString summary() const;
    QString cover() const;
    QStringList badges() const;
    bool multiDisc() const;
    QString commonQuality() const;
    QString formatText() const;
    QString codecText() const;
    QString specText() const;
    QString genre() const;
    QVariantMap details() const;
    QVariantList discs() const;
    QVariantList trackIds() const;

signals:
    void albumChanged();

private:
    const AlbumInfo* current() const;
    // 重新扫描后在新快照里找回同一张专辑 (曲目 ID 与专辑下标每次扫描都会变)
    void reresolve();
    LibraryService* lib_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    int album_ = -1;
};

class PlaylistService;

// 歌手墙
class ArtistModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QStringList letters READ letters NOTIFY lettersChanged)
public:
    enum Roles { NameRole = Qt::UserRole + 1, SummaryRole, CoverRole, IndexRole, LetterRole };
    explicit ArtistModel(LibraryService* lib, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QStringList letters() const { return letters_; }
    Q_INVOKABLE int indexOfLetter(const QString& letter) const;

signals:
    void lettersChanged();

private:
    void reload();
    LibraryService* lib_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    QStringList letters_;
};

// 通用曲目列表:source = "all" (全部歌曲,按标题拼音) / "artist" (某位歌手) / "playlist" (某个歌单) / "history" (播放历史)
class TrackCollectionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int artistIndex READ artistIndex WRITE setArtistIndex NOTIFY sourceChanged)
    Q_PROPERTY(QString playlistId READ playlistId WRITE setPlaylistId NOTIFY sourceChanged)
    Q_PROPERTY(int count READ count NOTIFY contentChanged)
    Q_PROPERTY(QStringList letters READ letters NOTIFY contentChanged)
public:
    enum Roles {
        TrackIdRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumRole, AlbumIndexRole,
        DurationRole, QualityRole, CoverRole, LetterRole, NumberRole
    };
    TrackCollectionModel(LibraryService* lib, PlaylistService* playlists, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString source() const { return source_; }
    void setSource(const QString& s);
    int artistIndex() const { return artist_; }
    void setArtistIndex(int i);
    QString playlistId() const { return playlist_; }
    void setPlaylistId(const QString& id);
    int count() const { return static_cast<int>(rows_.size()); }
    QStringList letters() const { return letters_; }

    Q_INVOKABLE QVariantList trackIds() const;
    Q_INVOKABLE int indexOfLetter(const QString& letter) const;
    Q_INVOKABLE int rowOfTrack(double trackId) const;

signals:
    void sourceChanged();
    void contentChanged();

private:
    void reload();
    LibraryService* lib_;
    PlaylistService* playlists_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    QString source_ = QStringLiteral("all");
    int artist_ = -1;
    QString playlist_;
    std::vector<const TrackItem*> rows_;
    std::vector<QString> rowLetters_;
    QStringList letters_;
};

// 搜索:曲目为列表行;专辑、歌手、最佳匹配作为属性提供
class SearchModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY resultsChanged)
    // 最佳匹配:kind (track / album / artist) + 对应字段
    Q_PROPERTY(QVariantMap top READ top NOTIFY resultsChanged)
public:
    enum Roles { TrackIdRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumRole, AlbumIndexRole, DurationRole, QualityRole, CoverRole };
    explicit SearchModel(LibraryService* lib, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return query_; }
    void setQuery(const QString& q);
    int count() const { return static_cast<int>(rows_.size()); }
    QVariantList albums() const { return albums_; }
    QVariantList artists() const { return artists_; }
    QVariantMap top() const { return top_; }
    Q_INVOKABLE QVariantList trackIds() const;

signals:
    void queryChanged();
    void resultsChanged();

private:
    void run();
    LibraryService* lib_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    QString query_;
    std::vector<const TrackItem*> rows_;
    QVariantList albums_;
    QVariantList artists_;
    QVariantMap top_;
};
