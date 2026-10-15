#pragma once
#include <QImage>
#include <QStringList>
#include <QAbstractListModel>
#include <QColor>
#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QVariantList>
#include <memory>

#include <QQuickWindow>

#include "audio_playback_engine.hpp"
#include "library_service.h"

// 播放队列模型 (歌曲很多时比 QVariantList 便宜得多):整体换队列时重置,单条增删 / 挪动只发对应的行信号,
// 列表保持滚动位置并能播放过渡动画。行与 queue_ 一一对应 (找不到的曲目留空行,下标不会错位)
class QueueModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TrackIdRole = Qt::UserRole + 1, TitleRole, ArtistRole, DurationRole, CoverRole, QueueIndexRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(std::shared_ptr<const LibrarySnapshot> snap, const std::vector<uint64_t>& ids, int gen);
    void insertAt(int row, const std::vector<uint64_t>& ids);
    void removeAt(int row);
    void moveRow(int from, int to);

private:
    void shiftedFrom(int first, int last);
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
    // 队列来自哪里:「album:<下标>」「artist:<名字>」「playlist:<id>」等;临时拼出来的队列为空。
    // 页面据此判断「播放」键该显示暂停 (当前队列就是这一页)
    Q_PROPERTY(QString queueTag READ queueTag NOTIFY queueChanged)
    Q_PROPERTY(QObject* queueModel READ queueModel CONSTANT)
    // WASAPI 独占输出 (切换时无缝重建输出设备,保持播放位置)
    Q_PROPERTY(bool exclusive READ exclusive WRITE setExclusive NOTIFY modeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modeChanged)
    // 输出设备 (空 = 跟随系统默认),切换时同样无缝重建
    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY devicesChanged)
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice NOTIFY modeChanged)
    // 无缝播放:当前曲目结束前预先打开下一首,在同一个音频回调内衔接
    Q_PROPERTY(bool gapless READ gapless WRITE setGapless NOTIFY modeChanged)
    // 音量标准化:0 关闭 / 1 按曲目 / 2 按专辑 (ReplayGain,带峰值保护)
    Q_PROPERTY(int replayGain READ replayGain WRITE setReplayGain NOTIFY modeChanged)
    // 0 顺序播放 / 1 列表循环 / 2 单曲循环
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY modeChanged)
    // 三段律动值 (低 / 中 / 高频),供「正在播放」小图标使用,避免每帧把 32 段频谱转换成 JS 数组
    // 「呼吸」:平滑后的低音包络 + 一条约 5 秒一周的缓慢底波,播放时缓缓浮现、暂停时缓缓沉下 (0..1)
    Q_PROPERTY(double breath READ breath NOTIFY spectrumChanged)
    Q_PROPERTY(double pulse0 READ pulse0 NOTIFY spectrumChanged)
    Q_PROPERTY(double pulse1 READ pulse1 NOTIFY spectrumChanged)
    Q_PROPERTY(double pulse2 READ pulse2 NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(int lyricIndex READ lyricIndex NOTIFY lyricIndexChanged)
    // false:歌词没有时间戳,按静态文本展示
    Q_PROPERTY(bool lyricsSynced READ lyricsSynced NOTIFY lyricsChanged)
    Q_PROPERTY(QString chainSummary READ chainSummary NOTIFY signalChainChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    // 定时关闭:0 = 关闭,-1 = 播完当前歌曲后停止,N = N 分钟后暂停;sleepRemaining 为剩余秒数
    Q_PROPERTY(int sleepMinutes READ sleepMinutes NOTIFY sleepChanged)
    Q_PROPERTY(int sleepRemaining READ sleepRemaining NOTIFY sleepChanged)
    // 队列剩余时长 (当前曲目剩下的 + 之后各曲),「1 小时 12 分钟」这类写法;显示的分钟数变了才通知
    Q_PROPERTY(QString queueRemaining READ queueRemaining NOTIFY queueStatsChanged)

public:
    explicit PlayerController(LibraryService* lib, QObject* parent = nullptr);
    // 主窗口创建后调用一次:之后的进度 / 频谱更新由窗口每帧驱动
    void attachWindow(QQuickWindow* window);
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
    QString queueTag() const { return queueTag_; }
    QString queueRemaining() const;
    QString errorText() const { return error_; }
    int sleepMinutes() const { return sleepMinutes_; }
    int sleepRemaining() const;
    QObject* queueModel() { return &queueModel_; }
    bool exclusive() const { return exclusive_; }
    void setExclusive(bool on);
    bool shuffle() const { return shuffle_; }
    void setShuffle(bool on);
    QStringList outputDevices() const { return devices_; }
    QString outputDevice() const { return device_; }
    void setOutputDevice(const QString& name);
    bool gapless() const { return gapless_; }
    void setGapless(bool on);
    int replayGain() const { return rgMode_; }
    void setReplayGain(int mode);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void reloadLyrics() { loadLyrics(); updateLyricIndex(); }
    // 某一时刻 (秒) 正在唱的那句歌词;没有同步歌词或尚未开始时为空 (进度条悬停提示用)
    Q_INVOKABLE QString lyricAt(double seconds) const;
    int repeatMode() const { return repeat_; }
    void setRepeatMode(int m);
    double breath() const { return breath_; }
    double pulse0() const { return pulse_[0]; }
    double pulse1() const { return pulse_[1]; }
    double pulse2() const { return pulse_[2]; }
    QVariantList lyrics() const { return lyrics_; }
    int lyricIndex() const { return lyricIndex_; }
    bool lyricsSynced() const { return lyricsSynced_; }
    QString chainSummary() const;

    Q_INVOKABLE void playAlbum(int albumIndex, int startRow = 0);
    Q_INVOKABLE void playTrack(double trackId);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void playQueueIndex(int index);
    // 播放任意曲目列表 (歌曲页 / 歌手 / 歌单 / 搜索结果);tag 标明来源 (见 queueTag)
    Q_INVOKABLE void playTracks(const QVariantList& trackIds, int start = 0, const QString& tag = QString());
    Q_INVOKABLE void playNext(double trackId);
    // 一组曲目整体插到当前歌曲之后 (保持原顺序)
    Q_INVOKABLE void playNextTracks(const QVariantList& trackIds);
    Q_INVOKABLE void enqueue(const QVariantList& trackIds);
    Q_INVOKABLE void removeFromQueue(int index);
    // 拖动排序:把第 from 首挪到第 to 位 (正在播放的那首跟着走)
    Q_INVOKABLE void moveInQueue(int from, int to);
    // 清空播放列表:只保留正在播放的这一首,播放不中断
    Q_INVOKABLE void clearQueue();
    // 撤销刚才的清空 (提示条上的「撤销」);曲库已重扫或正在播放的歌不在原队列里时返回 false
    Q_INVOKABLE bool undoClearQueue();
    Q_INVOKABLE void setSleepTimer(int minutes);
    // 键盘快捷键:相对跳转 (秒) / 音量增减
    Q_INVOKABLE void seekBy(double seconds) { seek(position_ + seconds); }
    Q_INVOKABLE void changeVolume(double delta) { setVolume(volume_ + delta); }

signals:
    void trackChanged();
    void signalChainChanged();
    void playingChanged();
    void positionChanged();
    void volumeChanged();
    void accentChanged();
    void spectrumChanged();
    void queueChanged();
    void queueStatsChanged();
    void queueIndexChanged();
    void modeChanged();
    void errorChanged();
    void lyricsChanged();
    void lyricIndexChanged();
    void devicesChanged();
    void sleepChanged();
    // 定时关闭到点并已暂停 (界面据此提示)
    void sleepFired();
    // 封面图解码完成 (切歌后异步到达)
    void coverImageChanged();

public:
    // 整理 / 查重移动了文件:下一次曲库刷新时按映射把播放队列接到新快照上 (正在播放的那首不会被移动)
    void remapPathsOnNextScan(const QHash<QString, QString>& pathMap);
    // 正在播放的文件路径 (整理时排除)
    QString currentPath() const;
    // 当前封面 (最长边 256,无封面时为占位图;任务栏缩略图用)
    QImage coverImage() const { return coverImage_; }

private:
    void applyPendingRemap();
    QHash<QString, QString> pendingRemap_;
    bool openIndex(int index, bool autoPlay);
    void setQueue(std::vector<uint64_t> ids, int start, const QString& tag);
    // 专辑下标随重扫变化:按队列首曲在当前快照里重新找回 album: 标签
    void reresolveAlbumTag();
    void queueContentChanged();
    // 单条增删 / 挪动之后:不重置模型,只通知长度与剩余时长、重排预加载
    void queueEdited();
    void updateQueueStats(bool recount);
    void onEnded();
    void applyVolume();
    // 启动恢复:保存 / 读取上次的队列 (路径 + CUE 起点)、位置
    void saveSession();
    void restoreSession();
    void rebuildSignalChain();
    void tick();
    void onFrameTimer();
    void loadLyrics();
    void loadKugouLyrics();
    void applyLyricsText(const QString& text, bool wholeFileTimeline, const QHash<qint64, qint64>& durations = {},
                         const QHash<qint64, QString>& translations = {});
    void updateLyricIndex();
    void extractAccent();
    void setPlaying(bool p);
    void ensureEngine();
    void rebuildOutput();
    void onTrackOpened(const TrackItem* t, int index);
    void onAdvanced();
    void queueGapless();
    float gainFor(const TrackItem* t) const;
    void scheduleGapless();

    LibraryService* lib_;
    std::unique_ptr<AudioPlaybackEngine> engine_;
    std::shared_ptr<const LibrarySnapshot> snap_; // 当前队列所依附的曲库快照
    std::vector<uint64_t> queue_;
    std::vector<uint64_t> context_; // 最近一次整体设为队列的那份列表 (点同一列表里的歌时只跳转)
    QString queueTag_;
    int queueIndex_ = -1;
    std::vector<uint64_t> unshuffled_; // 打开随机前的原始顺序,关闭随机时恢复
    bool shuffle_ = false;
    bool exclusive_ = false;
    int repeat_ = 0;
    bool gapless_ = true;
    int rgMode_ = 0;
    QString device_;
    QStringList devices_;
    uint64_t nextId_ = 0;  // 已交给引擎预加载的下一首
    int nextIndex_ = -1;
    bool gaplessPending_ = false;
    double pulse_[3] = {0, 0, 0};
    double breath_ = 0, breathEnv_ = 0, breathPresence_ = 0, breathPhase_ = 0;
    QueueModel queueModel_;
    double gain_ = 1.0;              // 淡入淡出增益,与用户音量相乘
    QVariantAnimation fade_;
    bool restored_ = false;
    qint64 lastSessionSave_ = 0;
    const TrackItem* current_ = nullptr;
    // current_ 指向的曲目所在的快照。扫描后点歌会先把 snap_ 换成新快照,
    // 若没人再持有旧快照,current_ 就悬空了 (暂停 → 重新扫描 → 点播放时崩溃)
    std::shared_ptr<const LibrarySnapshot> currentSnap_;
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
    QPointer<QQuickWindow> window_;  // 有窗口时按显示器刷新节拍推送进度与频谱 (与 vsync 对齐,高刷屏不再只有 60 帧)
    QElapsedTimer frameClock_;
    qint64 lastTickMs_ = -1;
    int accentRequest_ = 0;
    QImage coverImage_;
    QVariantList lyrics_;
    std::vector<qint64> lyricTimes_; // 毫秒,相对于当前曲目起点
    int lyricIndex_ = -1;
    bool lyricsSynced_ = false;
    quint64 lyricsGen_ = 0; // 每次重新加载歌词递增,丢弃过期的后台结果
    int sleepMinutes_ = 0;
    qint64 sleepDeadline_ = 0; // QDateTime 毫秒;按分钟定时时有效
    uint64_t queueAfterMs_ = 0;   // 当前曲目之后各曲的总时长
    qint64 remainingShown_ = -1;  // 上次通知时的剩余分钟数
    // 最近一次清空前的队列 (撤销用)
    struct ClearedQueue {
        std::shared_ptr<const LibrarySnapshot> snap;
        std::vector<uint64_t> queue, unshuffled, context;
        QString tag;
    } lastCleared_;
    QTimer sleepTimer_;        // 每秒刷新剩余时间
};
