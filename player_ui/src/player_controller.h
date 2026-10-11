#pragma once
#include <QColor>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "audio_playback_engine.hpp"
#include "library_service.h"

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
    Q_PROPERTY(int queuePosition READ queuePosition NOTIFY queueChanged)
    Q_PROPERTY(QVariantList queueTracks READ queueTracks NOTIFY queueChanged)
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
    QVariantList queueTracks() const;
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

signals:
    void trackChanged();
    void signalChainChanged();
    void playingChanged();
    void positionChanged();
    void volumeChanged();
    void accentChanged();
    void spectrumChanged();
    void queueChanged();
    void errorChanged();
    void lyricsChanged();
    void lyricIndexChanged();

private:
    bool openIndex(int index, bool autoPlay);
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
