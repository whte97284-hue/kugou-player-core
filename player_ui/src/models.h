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

signals:
    void albumChanged();

private:
    const AlbumInfo* current() const;
    LibraryService* lib_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    int album_ = -1;
};

// 命令面板搜索结果:专辑在前,曲目在后
class SearchModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { KindRole = Qt::UserRole + 1, TitleRole, SubtitleRole, CoverRole, RefRole, QualityRole };
    explicit SearchModel(LibraryService* lib, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return query_; }
    void setQuery(const QString& q);
    int count() const { return static_cast<int>(rows_.size()); }

signals:
    void queryChanged();
    void countChanged();

private:
    struct Row {
        bool album;
        int albumIndex;
        const TrackItem* track;
    };
    LibraryService* lib_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    QString query_;
    std::vector<Row> rows_;
};
