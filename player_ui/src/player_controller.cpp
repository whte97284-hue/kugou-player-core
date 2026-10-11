#include "player_controller.h"

#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <QVariantMap>

#include "cover_provider.h"

namespace {
QString Q(const std::wstring& w) { return QString::fromStdWString(w); }

QString KHz(uint32_t rate) {
    if (rate == 0) return QStringLiteral("?");
    return rate % 1000 ? QString::number(rate / 1000.0, 'f', 1) + "k" : QString::number(rate / 1000) + "k";
}

QVariantMap Stage(const QString& label, const QString& detail, bool ok) {
    QVariantMap m;
    m["label"] = label;
    m["detail"] = detail;
    m["ok"] = ok;
    return m;
}
} // namespace

PlayerController::PlayerController(LibraryService* lib, QObject* parent) : QObject(parent), lib_(lib) {
    QSettings s;
    volume_ = s.value("player/volume", 0.85).toDouble();
    for (int i = 0; i < 32; ++i) spectrum_.append(0.0);
    timer_.setInterval(16); // 约 60Hz 推送进度与频谱
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &PlayerController::tick);
}

PlayerController::~PlayerController() {
    QSettings().setValue("player/volume", volume_);
    timer_.stop();
    if (engine_) {
        engine_->on_playback_ended = nullptr;
        engine_->Shutdown();
    }
}

int PlayerController::albumIndex() const {
    if (!current_ || !snap_) return -1;
    auto it = snap_->trackAlbum.find(current_->id);
    return it == snap_->trackAlbum.end() ? -1 : it->second;
}

QString PlayerController::title() const { return current_ ? Q(current_->title) : QString(); }
QString PlayerController::artist() const { return current_ ? Q(current_->artist) : QString(); }
QString PlayerController::album() const { return current_ ? Q(current_->album) : QString(); }
QString PlayerController::quality() const { return current_ ? LibraryService::trackQuality(*current_) : QString(); }
double PlayerController::duration() const { return engine_ ? engine_->GetDurationSeconds() : 0.0; }

QString PlayerController::cover() const {
    if (!current_) return {};
    return QString("image://cover/t/%1/%2").arg(lib_->generation()).arg(current_->id);
}

QString PlayerController::backdrop() const {
    if (!current_) return {};
    return QString("image://cover/b/%1/%2").arg(lib_->generation()).arg(current_->id);
}

void PlayerController::setVolume(double v) {
    v = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(v + 1, volume_ + 1)) return;
    volume_ = v;
    if (engine_) engine_->SetVolume(static_cast<float>(v));
    emit volumeChanged();
    rebuildSignalChain();
}

void PlayerController::setPlaying(bool p) {
    if (p == playing_) return;
    playing_ = p;
    emit playingChanged();
}

void PlayerController::playAlbum(int albumIndex, int startRow) {
    auto snap = lib_->snapshot();
    if (albumIndex < 0 || albumIndex >= static_cast<int>(snap->albums.size())) return;
    snap_ = snap;
    queue_.clear();
    for (const TrackItem* t : snap_->albums[albumIndex].tracks) queue_.push_back(t->id);
    emit queueChanged();
    openIndex(std::clamp(startRow, 0, static_cast<int>(queue_.size()) - 1), true);
}

void PlayerController::playTrack(double trackId) {
    auto snap = lib_->snapshot();
    auto it = snap->trackAlbum.find(static_cast<uint64_t>(trackId));
    if (it == snap->trackAlbum.end()) return;
    const AlbumInfo& a = snap->albums[it->second];
    int row = 0;
    for (size_t i = 0; i < a.tracks.size(); ++i)
        if (a.tracks[i]->id == static_cast<uint64_t>(trackId)) row = static_cast<int>(i);
    playAlbum(it->second, row);
}

bool PlayerController::openIndex(int index, bool autoPlay) {
    if (!snap_ || index < 0 || index >= static_cast<int>(queue_.size())) return false;
    if (!engine_) {
        engine_ = std::make_unique<AudioPlaybackEngine>();
        engine_->Init(QSettings().value("audio/exclusive", false).toBool());
        // 结束回调来自引擎通知线程,排队回到界面线程再切歌
        QPointer<PlayerController> self(this);
        engine_->on_playback_ended = [self]() {
            if (self) QMetaObject::invokeMethod(self, [self]() { if (self) self->next(); }, Qt::QueuedConnection);
        };
    }
    // 跳过无法解码的曲目 (例如 DSD:当前解码器不支持),最多尝试整个队列一遍
    for (size_t attempt = 0; attempt < queue_.size(); ++attempt) {
        int i = static_cast<int>((index + attempt) % queue_.size());
        const TrackItem* t = snap_->catalog.FindById(queue_[i]);
        if (!t) continue;
        bool ok = t->is_cue_track
                      ? engine_->OpenFile(t->file_path, t->cue_start_ms / 1000.0, t->cue_end_ms / 1000.0)
                      : engine_->OpenFile(t->file_path);
        if (!ok) {
            error_ = tr("无法播放:%1 (%2)").arg(Q(t->title), Q(t->codec));
            emit errorChanged();
            continue;
        }
        if (!error_.isEmpty() && attempt == 0) {
            error_.clear();
            emit errorChanged();
        }
        current_ = t;
        queueIndex_ = i;
        position_ = 0;
        engine_->SetVolume(static_cast<float>(volume_));
        if (autoPlay) engine_->Play();
        setPlaying(autoPlay);
        timer_.start();
        emit trackChanged();
        emit queueChanged();
        emit positionChanged();
        rebuildSignalChain();
        extractAccent();
        return true;
    }
    return false;
}

void PlayerController::toggle() {
    if (!engine_ || !current_) {
        if (!queue_.empty()) openIndex(std::max(0, queueIndex_), true);
        return;
    }
    if (engine_->GetState() == PlaybackState::Playing) {
        engine_->Pause();
        setPlaying(false);
    } else {
        engine_->TogglePlay();
        setPlaying(true);
        timer_.start();
    }
}

void PlayerController::next() {
    if (queue_.empty()) return;
    if (queueIndex_ + 1 < static_cast<int>(queue_.size())) {
        openIndex(queueIndex_ + 1, true);
    } else {
        // 队列结束:停在最后一首的起点
        if (engine_) engine_->Stop();
        setPlaying(false);
        position_ = 0;
        emit positionChanged();
    }
}

void PlayerController::previous() {
    if (queue_.empty()) return;
    // 播放超过 3 秒时「上一首」回到本曲开头,符合主流播放器习惯
    if (position_ > 3.0 || queueIndex_ == 0) {
        seek(0);
        return;
    }
    openIndex(queueIndex_ - 1, playing_);
}

void PlayerController::seek(double seconds) {
    if (!engine_) return;
    engine_->SeekTo(seconds);
    position_ = engine_->GetPositionSeconds();
    emit positionChanged();
}

void PlayerController::tick() {
    if (!engine_) return;
    double p = engine_->GetPositionSeconds();
    if (std::abs(p - position_) > 1e-4) {
        position_ = p;
        emit positionChanged();
    }
    bool isPlaying = engine_->GetState() == PlaybackState::Playing;
    if (playing_ != isPlaying && !isPlaying && engine_->GetState() == PlaybackState::Stopped) setPlaying(false);

    std::vector<float> bars = engine_->GetVisualizer().GetSpectrumBars();
    double energy = 0;
    for (size_t i = 0; i < bars.size() && i < 32; ++i) {
        spectrum_[static_cast<int>(i)] = bars[i];
        energy += bars[i];
    }
    emit spectrumChanged();
    // 暂停且频谱已完全衰减后停止定时器,空闲时零 CPU
    if (!isPlaying && energy < 1e-3) timer_.stop();
}

void PlayerController::rebuildSignalChain() {
    chain_.clear();
    if (!current_ || !engine_) {
        bitPerfect_ = false;
        emit signalChainChanged();
        return;
    }
    const TrackItem& t = *current_;
    uint32_t src = engine_->GetSourceSampleRate();
    uint32_t out = engine_->GetOutputSampleRate();
    uint32_t outBits = engine_->GetOutputBitDepth();
    bool exclusive = engine_->IsExclusiveMode();
    bool lossy = !t.is_lossless;
    bool perfect = !lossy;

    QString source = Q(t.codec.empty() ? t.format : t.codec);
    if (t.is_lossless) source += QString(" %1/%2").arg(t.bits_per_sample).arg(KHz(t.sample_rate));
    else source += QString(" %1k").arg(t.bitrate_kbps);
    chain_ << Stage(source, lossy ? tr("有损压缩音源") : tr("无损音源"), !lossy);

    if (src != out && out) {
        perfect = false;
        chain_ << Stage(tr("重采样 %1→%2").arg(KHz(src), KHz(out)),
                        exclusive ? tr("输出设备独占模式不支持 %1 Hz,已用最高质量滤波转换").arg(src)
                                  : tr("共享模式下系统混音器以 %1 Hz 运行").arg(out),
                        false);
    } else {
        chain_ << Stage(tr("原生 %1").arg(KHz(out)), tr("采样率与音源一致,无重采样"), true);
    }

    if (outBits && t.bits_per_sample > outBits && t.is_lossless) {
        perfect = false;
        chain_ << Stage(tr("%1→%2 bit 抖动").arg(t.bits_per_sample).arg(outBits),
                        tr("设备只接受 %1 bit,已加 TPDF 抖动避免截断失真").arg(outBits), false);
    }
    if (volume_ < 0.999) {
        perfect = false;
        chain_ << Stage(tr("数字音量 %1%").arg(qRound(volume_ * 100)), tr("软件音量会改变样本值,100% 时可实现无损直通"), false);
    }
    chain_ << Stage(exclusive ? tr("WASAPI 独占") : tr("共享模式"),
                    exclusive ? tr("绕过系统混音器,直接送往设备") : tr("经过 Windows 系统混音器"), exclusive);
    if (!exclusive) perfect = false;

    bitPerfect_ = perfect;
    emit signalChainChanged();
}

void PlayerController::extractAccent() {
    if (!current_) return;
    int req = ++accentRequest_;
    TrackItem copy = *current_; // 后台线程只使用副本
    copy.cover_data.clear();
    QPointer<PlayerController> self(this);
    QThreadPool::globalInstance()->start([self, req, copy]() {
        QImage img = CoverProvider::LoadTrackCover(copy, 256);
        if (img.isNull()) img = CoverProvider::Placeholder(QString::fromStdWString(copy.album), 64);
        QColor c = ExtractAccentColor(img);
        QMetaObject::invokeMethod(self, [self, req, c]() {
            if (!self || req != self->accentRequest_) return; // 期间已切歌,丢弃过期结果
            self->accent_ = c;
            emit self->accentChanged();
        }, Qt::QueuedConnection);
    });
}
