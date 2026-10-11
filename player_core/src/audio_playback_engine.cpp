#define MINIAUDIO_IMPLEMENTATION
#include "../include/miniaudio.h"
#include "../include/audio_playback_engine.hpp"
#include "../include/audio_tag_parser.hpp"
#include "../include/platform_io.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <thread>

struct AudioPlaybackEngine::Impl {
    ma_device device;
    ma_decoder decoder;
    bool device_initialized = false;
    bool decoder_initialized = false;
    bool exclusive_requested = false;
    ma_uint32 out_rate = 0;     // 送入设备回调的采样率 (解码器输出采样率)
    ma_uint32 out_channels = 2;
    std::mutex decoder_mutex;

    // 当前片段 (单位:输出帧)
    ma_uint64 seg_start = 0;
    ma_uint64 seg_end = 0;
    std::atomic<ma_uint64> cursor{0};

    // 音频线程私有:当前实际增益 (用于平滑斜坡)
    float gain = 1.0f;

    // 播放结束通知线程
    std::thread notifier;
    std::mutex notify_mutex;
    std::condition_variable notify_cv;
    std::atomic<bool> ended_pending{false};
    std::atomic<bool> quit{false};
};

static void MaDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    AudioPlaybackEngine* engine = reinterpret_cast<AudioPlaybackEngine*>(pDevice->pUserData);
    if (engine) engine->OnAudioFrames(pOutput, frameCount);
}

AudioPlaybackEngine::AudioPlaybackEngine() : impl_(std::make_unique<Impl>()) {
    impl_->notifier = std::thread([this]() { NotifierLoop(); });
}

AudioPlaybackEngine::~AudioPlaybackEngine() {
    Shutdown();
    impl_->quit.store(true);
    impl_->notify_cv.notify_all();
    if (impl_->notifier.joinable()) impl_->notifier.join();
}

void AudioPlaybackEngine::NotifierLoop() {
    while (!impl_->quit.load()) {
        {
            std::unique_lock<std::mutex> lock(impl_->notify_mutex);
            impl_->notify_cv.wait_for(lock, std::chrono::milliseconds(50),
                                      [this] { return impl_->quit.load() || impl_->ended_pending.load(); });
        }
        if (impl_->ended_pending.exchange(false)) {
            // 在普通线程中回调:上层可以在这里直接 OpenFile/Play 下一首,不会与音频线程互锁
            if (on_state_changed) on_state_changed(PlaybackState::Stopped);
            if (on_playback_ended) on_playback_ended();
        }
    }
}

bool AudioPlaybackEngine::InitDevice(uint32_t sampleRate, uint32_t channels, bool exclusive) {
    if (impl_->device_initialized) {
        ma_device_uninit(&impl_->device); // 阻塞直到回调退出;回调只 try_lock 解码器锁,不会死锁
        impl_->device_initialized = false;
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.sampleRate = sampleRate; // 0 = 设备原生 (共享模式下即系统混音采样率,避免二次重采样)
    cfg.dataCallback = MaDataCallback;
    cfg.pUserData = this;
    cfg.noClip = MA_FALSE;
    if (exclusive) {
        // 真·WASAPI 独占:shareMode 才是独占开关,usage 只是 MMCSS 线程优先级提示
        cfg.playback.shareMode = ma_share_mode_exclusive;
        cfg.wasapi.usage = ma_wasapi_usage_pro_audio;
        cfg.performanceProfile = ma_performance_profile_low_latency;
    }

    ma_result res = ma_device_init(NULL, &cfg, &impl_->device);
    bool exclusiveOk = exclusive && res == MA_SUCCESS;
    if (res != MA_SUCCESS && exclusive) {
        // 声卡被占用或不支持该格式:平滑降级为共享模式
        cfg.playback.shareMode = ma_share_mode_shared;
        cfg.wasapi.usage = ma_wasapi_usage_default;
        cfg.performanceProfile = ma_performance_profile_low_latency;
        res = ma_device_init(NULL, &cfg, &impl_->device);
    }
    if (res != MA_SUCCESS) {
        std::wcerr << L"[ERROR] 无法初始化音频设备: " << res << std::endl;
        return false;
    }
    impl_->device_initialized = true;
    impl_->out_rate = impl_->device.sampleRate;
    impl_->out_channels = impl_->device.playback.channels;
    exclusive_active_.store(exclusiveOk);
    output_rate_.store(impl_->device.playback.internalSampleRate);

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        impl_->device_initialized = false;
        return false;
    }
    return true;
}

bool AudioPlaybackEngine::Init(bool exclusiveMode) {
    if (impl_->device_initialized) return true;
    impl_->exclusive_requested = exclusiveMode;
    std::lock_guard<std::mutex> lock(impl_->decoder_mutex);
    return InitDevice(0, 2, exclusiveMode);
}

void AudioPlaybackEngine::Shutdown() {
    state_.store(PlaybackState::Stopped);
    if (impl_->device_initialized) {
        ma_device_uninit(&impl_->device);
        impl_->device_initialized = false;
    }
    std::lock_guard<std::mutex> lock(impl_->decoder_mutex);
    if (impl_->decoder_initialized) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoder_initialized = false;
    }
}

bool AudioPlaybackEngine::OpenFile(const std::wstring& filePath) { return OpenFile(filePath, 0.0, 0.0); }

bool AudioPlaybackEngine::OpenFile(const std::wstring& filePath, double startSeconds, double endSeconds) {
    if (!impl_->device_initialized && !Init(impl_->exclusive_requested)) return false;
    const std::wstring path = platform::LongPath(filePath);

    // 用自己的解析内核取得原生格式与精确时长:只读文件头,避免 MP3 为求时长而整文件扫描
    AudioMetadata meta;
    ParseOptions po;
    po.load_cover = false;
    po.read_id3v1 = false;
    bool haveMeta = AudioTagParser::ParseFile(filePath, meta, po) && meta.sample_rate > 0;

    std::lock_guard<std::mutex> lock(impl_->decoder_mutex);
    state_.store(PlaybackState::Stopped);
    if (impl_->decoder_initialized) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoder_initialized = false;
    }

    uint32_t srcRate = haveMeta ? meta.sample_rate : 0;
    if (srcRate == 0) {
        ma_decoder probe;
        ma_decoder_config pc = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_file_w(path.c_str(), &pc, &probe) == MA_SUCCESS) {
            ma_format f;
            ma_uint32 ch;
            ma_decoder_get_data_format(&probe, &f, &ch, &srcRate, NULL, 0);
            ma_decoder_uninit(&probe);
        }
    }

    // 独占模式:设备按母带原生采样率重建,实现无重采样直通
    if (impl_->exclusive_requested && srcRate > 0 && srcRate != impl_->device.playback.internalSampleRate) {
        if (!InitDevice(srcRate, impl_->out_channels, true)) {
            if (!InitDevice(0, 2, false)) return false;
        }
    }

    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, impl_->out_channels, impl_->out_rate);
    dc.resampling.algorithm = ma_resample_algorithm_linear;
    dc.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER; // 需要重采样时使用最高阶抗混叠低通
    ma_result res = ma_decoder_init_file_w(path.c_str(), &dc, &impl_->decoder);
    if (res != MA_SUCCESS) {
        std::wcerr << L"[ERROR] 无法解码音频文件: " << filePath << L" (错误码: " << res << L")" << std::endl;
        return false;
    }
    impl_->decoder_initialized = true;

    const double rate = static_cast<double>(impl_->out_rate);
    ma_uint64 totalFrames = 0;
    if (haveMeta && meta.total_samples > 0 && meta.sample_rate > 0) {
        totalFrames = meta.total_samples * impl_->out_rate / meta.sample_rate;
    } else if (haveMeta && meta.duration_ms > 0) {
        totalFrames = static_cast<ma_uint64>(meta.duration_ms) * impl_->out_rate / 1000;
    } else if (ma_decoder_get_length_in_pcm_frames(&impl_->decoder, &totalFrames) != MA_SUCCESS) {
        totalFrames = 0;
    }

    impl_->seg_start = startSeconds > 0 ? static_cast<ma_uint64>(startSeconds * rate) : 0;
    impl_->seg_end = endSeconds > 0 ? static_cast<ma_uint64>(endSeconds * rate) : totalFrames;
    if (totalFrames && impl_->seg_end > totalFrames) impl_->seg_end = totalFrames;
    if (impl_->seg_start > 0) ma_decoder_seek_to_pcm_frame(&impl_->decoder, impl_->seg_start);
    impl_->cursor.store(impl_->seg_start);

    duration_.store(impl_->seg_end > impl_->seg_start ? (impl_->seg_end - impl_->seg_start) / rate : 0.0);
    current_position_.store(0.0);
    source_rate_.store(srcRate ? srcRate : impl_->out_rate);
    output_rate_.store(impl_->device.playback.internalSampleRate);
    visualizer_.SetSampleRate(impl_->out_rate);
    visualizer_.Reset();
    impl_->ended_pending.store(false);
    {
        std::lock_guard<std::mutex> sl(state_mutex_);
        current_file_ = filePath;
    }
    return true;
}

bool AudioPlaybackEngine::Play() {
    if (!impl_->decoder_initialized) return false;
    state_.store(PlaybackState::Playing);
    if (on_state_changed) on_state_changed(PlaybackState::Playing);
    return true;
}

void AudioPlaybackEngine::Pause() {
    if (state_.load() == PlaybackState::Playing) {
        state_.store(PlaybackState::Paused);
        if (on_state_changed) on_state_changed(PlaybackState::Paused);
    }
}

void AudioPlaybackEngine::Resume() {
    if (state_.load() == PlaybackState::Paused) {
        state_.store(PlaybackState::Playing);
        if (on_state_changed) on_state_changed(PlaybackState::Playing);
    }
}

void AudioPlaybackEngine::Stop() {
    if (state_.load() != PlaybackState::Stopped) {
        state_.store(PlaybackState::Stopped);
        SeekTo(0.0);
        visualizer_.Reset();
        if (on_state_changed) on_state_changed(PlaybackState::Stopped);
    }
}

void AudioPlaybackEngine::TogglePlay() {
    PlaybackState s = state_.load();
    if (s == PlaybackState::Playing) Pause();
    else if (s == PlaybackState::Paused) Resume();
    else Play();
}

bool AudioPlaybackEngine::SeekTo(double seconds) {
    std::lock_guard<std::mutex> lock(impl_->decoder_mutex);
    if (!impl_->decoder_initialized) return false;
    if (seconds < 0.0) seconds = 0.0;
    double maxDur = duration_.load();
    if (maxDur > 0.0 && seconds > maxDur) seconds = maxDur;

    ma_uint64 target = impl_->seg_start + static_cast<ma_uint64>(seconds * impl_->out_rate);
    if (ma_decoder_seek_to_pcm_frame(&impl_->decoder, target) != MA_SUCCESS) return false;
    impl_->cursor.store(target);
    current_position_.store(seconds);
    visualizer_.Reset();
    return true;
}

void AudioPlaybackEngine::SetVolume(float volume) { volume_.store(std::min(1.0f, std::max(0.0f, volume))); }

float AudioPlaybackEngine::GetVolume() const { return volume_.load(); }

PlaybackState AudioPlaybackEngine::GetState() const { return state_.load(); }

double AudioPlaybackEngine::GetPositionSeconds() const { return current_position_.load(); }

double AudioPlaybackEngine::GetDurationSeconds() const { return duration_.load(); }

std::wstring AudioPlaybackEngine::GetCurrentFilePath() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_file_;
}

void AudioPlaybackEngine::OnAudioFrames(void* pOutput, uint32_t frameCount) {
    float* out = reinterpret_cast<float*>(pOutput);
    const size_t ch = impl_->out_channels;
    const size_t totalSamples = static_cast<size_t>(frameCount) * ch;

    if (state_.load() != PlaybackState::Playing) {
        std::fill(out, out + totalSamples, 0.0f);
        // 暂停/停止时持续馈入静音,令频谱柱自然阻尼衰减而非冻结
        visualizer_.PushSamples(out, totalSamples, ch);
        return;
    }

    std::unique_lock<std::mutex> lock(impl_->decoder_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !impl_->decoder_initialized) {
        std::fill(out, out + totalSamples, 0.0f);
        return;
    }

    ma_uint64 cursor = impl_->cursor.load();
    ma_uint64 want = frameCount;
    if (impl_->seg_end > 0) want = (cursor < impl_->seg_end) ? std::min<ma_uint64>(want, impl_->seg_end - cursor) : 0;

    ma_uint64 got = 0;
    if (want > 0) ma_decoder_read_pcm_frames(&impl_->decoder, out, want, &got);
    if (got < frameCount) std::fill(out + got * ch, out + totalSamples, 0.0f);
    cursor += got;
    impl_->cursor.store(cursor);

    // 音量:按样本线性斜坡逼近目标值,避免拉链噪声
    const float target = volume_.load();
    float g = impl_->gain;
    if (!(g > 0.9999f && target > 0.9999f)) {
        const size_t n = static_cast<size_t>(got) * ch;
        const float step = n ? (target - g) / static_cast<float>(n) : 0.0f;
        for (size_t i = 0; i < n; ++i) {
            g += step;
            out[i] *= g;
        }
        impl_->gain = target;
    }

    const double rate = static_cast<double>(impl_->out_rate);
    current_position_.store(static_cast<double>(cursor - std::min(cursor, impl_->seg_start)) / rate);
    visualizer_.PushSamples(out, static_cast<size_t>(got) * ch, ch);

    bool reachedEnd = (got == 0) || (impl_->seg_end > 0 && cursor >= impl_->seg_end);
    if (reachedEnd) {
        state_.store(PlaybackState::Stopped);
        current_position_.store(duration_.load());
        impl_->ended_pending.store(true);
        impl_->notify_cv.notify_one(); // 通知线程负责调用上层回调
    }
}
