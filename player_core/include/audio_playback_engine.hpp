#pragma once
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include "fft_visualizer.hpp"

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

class AudioPlaybackEngine {
public:
    AudioPlaybackEngine();
    ~AudioPlaybackEngine();

    // 初始化硬件音频设备 (exclusiveMode: true 开启 WASAPI 独占直通发烧模式)
    bool Init(bool exclusiveMode = false);

    // 关闭音频设备与资源释放
    void Shutdown();

    // 打开本地音频文件 (支持 FLAC, MP3, OGG, WAV)
    bool OpenFile(const std::wstring& filePath);

    // 播放控制
    bool Play();
    void Pause();
    void Resume();
    void Stop();
    void TogglePlay();

    // 精确跳转进度 (秒)
    bool SeekTo(double seconds);

    // 音量调节 (0.0f ~ 1.0f)
    void SetVolume(float volume);
    float GetVolume() const;

    // 状态查询
    PlaybackState GetState() const;
    double GetPositionSeconds() const;
    double GetDurationSeconds() const;
    std::wstring GetCurrentFilePath() const;

    // 获取实时 FFT 频谱分析器引用 (供 UI 读取 32 频段动效)
    FftVisualizer& GetVisualizer() { return visualizer_; }

    // 回调钩子
    std::function<void(PlaybackState)> on_state_changed;
    std::function<void()> on_playback_ended;

    // 内部数据回调 (由 miniaudio 设备驱动调用)
    void OnAudioFrames(void* pOutput, uint32_t frameCount);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::mutex state_mutex_;
    std::wstring current_file_;
    std::atomic<PlaybackState> state_{PlaybackState::Stopped};
    std::atomic<float> volume_{1.0f};
    std::atomic<double> current_position_{0.0};
    std::atomic<double> duration_{0.0};

    FftVisualizer visualizer_;
};
