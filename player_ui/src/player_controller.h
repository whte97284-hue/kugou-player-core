#pragma once
#include <QAbstractListModel>
#include <QColor>
#include <QObject>
#include <QTimer>
#include <QVariantAnimation>
#include <QVariantList>
#include <memory>

#include "audio_playback_engine.hpp"
#include "library_service.h"

// 播放队列模型 (歌曲很多时比 QVariantList 便宜得多:只在内容变化时重置)
class QueueModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TrackIdRole = Qt::UserRole + 1, TitleRole, ArtistRole, DurationRole, CoverRole, QueueIndexRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(std::shared_ptr<const LibrarySnapshot> snap, const std::vector<uint64_t>& ids, int gen);

private:
    std::shared_ptr<const LibrarySnapshot> snap_;
    std::vector<const TrackItem*> rows_;
    int gen_ = 0;
};

// 播放控制器:播放引擎 + 播放队列 + 输出信号链 + 频谱 + 专辑强调色
class PlayerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(double trackId READ trackId NOTIFY trackChanged)
    Q_PROPERTY(int albumIndex READ albumIndex NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString cover READ cover NOTIFY trackChanged)
    Q_PROPERTY(QString backdrop READ backdrop NOTIFY trackChanged)
    Q_PROPERTY(QString quality READ quality NOTIFY trackChanged)
    Q_PROPERTY(QVariantList signalChain READ signalChain NOTIFY signalChainChanged)
    Q_PROPERTY(bool bitPerfect READ bitPerfect NOTIFY signalChainChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY trackChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY accentChanged)
    Q_PROPERTY(double bass READ bass NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)
    Q_PROPERTY(int queueLength READ queueLength NOTIFY queueChanged)
    Q_PROPERTY(int queuePosition READ queuePosition NOTIFY queueIndexChanged)
    Q_PROPERTY(QObject* queueModel READ queueModel CONSTANT)
    // WASAPI 独占输出 (切换时无缝重建输出设备,保持播放位置)
    Q_PROPERTY(bool exclusive READ exclusive WRITE setExclusive NOTIFY modeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modeChanged)
    // 0 顺序播放 / 1 列表循环 / 2 单曲循环
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY modeChanged)
    // 三段律动值 (低 / 中 / 高频),供「正在播放」小图标使用,避免每帧把 32 段频谱转换成 JS 数组
    Q_PROPERTY(double pulse0 READ pulse0 NOTIFY spectrumChanged)
    Q_PROPERTY(double pulse1 READ pulse1 NOTIFY spectrumChanged)
    Q_PROPERTY(double pulse2 READ pulse2 NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(int lyricIndex READ lyricIndex NOTIFY lyricIndexChanged)
    Q_PROPERTY(QString chainSummary READ chainSummary NOTIFY signalChainChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)

public:
    explicit PlayerController(LibraryService* lib, QObject* parent = nullptr);
    ~PlayerController() override;

    bool hasTrack() const { return current_ != nullptr; }
    double trackId() const { return current_ ? static_cast<double>(current_->id) : -1; }
    int albumIndex() const;
    QString title() const;
    QString artist() const;
    QString album() const;
    QString cover() const;
    QString backdrop() const;
    QString quality() const;
    QVariantList signalChain() const { return chain_; }
    bool bitPerfect() const { return bitPerfect_; }
    bool playing() const { return playing_; }
    double position() const { return position_; }
    double duration() const;
    double volume() const { return volume_; }
    void setVolume(double v);
    QColor accent() const { return accent_; }
    QVariantList palette() const { return palette_; }
    double bass() const { return bass_; }
    QVariantList spectrum() const { return spectrum_; }
    int queueLength() const { return static_cast<int>(queue_.size()); }
    int queuePosition() const { return queueIndex_; }
    QString errorText() const { return error_; }
    QObject* queueModel() { return &queueModel_; }
    bool exclusive() const { return exclusive_; }
    void setExclusive(bool on);
    bool shuffle() const { return shuffle_; }
    void setShuffle(bool on);
    int repeatMode() const { return repeat_; }
    void setRepeatMode(int m);
    double pulse0() const { return pulse_[0]; }
    double pulse1() const { return pulse_[1]; }
    double pulse2() const { return pulse_[2]; }
    QVariantList lyrics() const { return lyrics_; }
    int lyricIndex() const { return lyricIndex_; }
    QString chainSummary() const;

    Q_INVOKABLE void playAlbum(int albumIndex, int startRow = 0);
    Q_INVOKABLE void playTrack(double trackId);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void playQueueIndex(int index);
    // 播放任意曲目列表 (歌曲页 / 歌手 / 歌单 / 搜索结果)
    Q_INVOKABLE void playTracks(const QVariantList& trackIds, int start = 0);
    Q_INVOKABLE void playNext(double trackId);
    Q_INVOKABLE void enqueue(const QVariantList& trackIds);
    Q_INVOKABLE void removeFromQueue(int index);

signals:
    void trackChanged();
    void signalChainChanged();
    void playingChanged();
    void positionChanged();
    void volumeChanged();
    void accentChanged();
    void spectrumChanged();
    void queueChanged();
    void queueIndexChanged();
    void modeChanged();
    void errorChanged();
    void lyricsChanged();
    void lyricIndexChanged();

private:
    bool openIndex(int index, bool autoPlay);
    void setQueue(std::vector<uint64_t> ids, int start);
    void queueContentChanged();
    void onEnded();
    void applyVolume();
    // 启动恢复:保存 / 读取上次的队列 (路径 + CUE 起点)、位置
    void saveSession();
    void restoreSession();
    void rebuildSignalChain();
    void tick();
    void loadLyrics();
    void updateLyricIndex();
    void extractAccent();
    void setPlaying(bool p);

    LibraryService* lib_;
    std::unique_ptr<AudioPlaybackEngine> engine_;
    std::shared_ptr<const LibrarySnapshot> snap_; // 当前队列所依附的曲库快照
    std::vector<uint64_t> queue_;
    int queueIndex_ = -1;
    std::vector<uint64_t> unshuffled_; // 打开随机前的原始顺序,关闭随机时恢复
    bool shuffle_ = false;
    bool exclusive_ = false;
    int repeat_ = 0;
    double pulse_[3] = {0, 0, 0};
    QueueModel queueModel_;
    double gain_ = 1.0;              // 淡入淡出增益,与用户音量相乘
    QVariantAnimation fade_;
    bool restored_ = false;
    qint64 lastSessionSave_ = 0;
    const TrackItem* current_ = nullptr;
    QVariantList chain_;
    bool bitPerfect_ = false;
    bool playing_ = false;
    double position_ = 0;
    double volume_ = 0.85;
    QColor accent_{"#c8a46e"};
    QVariantList palette_{QColor("#2e2619"), QColor("#231b16"), QColor("#3a3020"), QColor("#1b1816")};
    double bass_ = 0;
    QVariantList spectrum_;
    QString error_;
    QTimer timer_;
    int accentRequest_ = 0;
    QVariantList lyrics_;
    std::vector<qint64> lyricTimes_; // 毫秒,相对于当前曲目起点
    int lyricIndex_ = -1;
};
