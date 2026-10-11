#define MINIAUDIO_IMPLEMENTATION
#include "../include/miniaudio.h"
#include "../include/audio_playback_engine.hpp"
#include <iostream>
#include <algorithm>

struct AudioPlaybackEngine::Impl {
    ma_device device;
    ma_decoder decoder;
    bool device_initialized = false;
    bool decoder_initialized = false;
    ma_uint32 sample_rate = 44100;
    ma_uint32 channels = 2;
    std::mutex decoder_mutex;
};

static void MaDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    AudioPlaybackEngine* engine = reinterpret_cast<AudioPlaybackEngine*>(pDevice->pUserData);
    if (engine) {
        engine->OnAudioFrames(pOutput, frameCount);
    }
}

AudioPlaybackEngine::AudioPlaybackEngine()
    : impl_(std::make_unique<Impl>()) {
}

AudioPlaybackEngine::~AudioPlaybackEngine() {
    Shutdown();
}

bool AudioPlaybackEngine::Init(bool exclusiveMode) {
    if (impl_->device_initialized) {
        return true;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = impl_->channels;
    deviceConfig.sampleRate = impl_->sample_rate;
    deviceConfig.dataCallback = MaDataCallback;
    deviceConfig.pUserData = this;

    // 发烧级 WASAPI 独占直通配置 (Exclusive Mode)
    if (exclusiveMode) {
        deviceConfig.wasapi.usage = ma_wasapi_usage_pro_audio;
        deviceConfig.performanceProfile = ma_performance_profile_low_latency;
    }

    ma_result res = ma_device_init(NULL, &deviceConfig, &impl_->device);
    if (res != MA_SUCCESS) {
        // 若独占模式不支持，平滑降级为普通共享模式
        deviceConfig.wasapi.usage = ma_wasapi_usage_default;
        res = ma_device_init(NULL, &deviceConfig, &impl_->device);
        if (res != MA_SUCCESS) {
            std::wcerr << L"[ERROR] 无法初始化音频设备: " << res << std::endl;
            return false;
        }
    }

    impl_->device_initialized = true;

    // 启动音频设备驱动
    res = ma_device_start(&impl_->device);
    if (res != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        impl_->device_initialized = false;
        return false;
    }

    return true;
}

void AudioPlaybackEngine::Shutdown() {
    Stop();

    if (impl_->device_initialized) {
        ma_device_stop(&impl_->device);
        ma_device_uninit(&impl_->device);
        impl_->device_initialized = false;
    }

    std::lock_guard<std::mutex> lock(impl_->decoder_mutex);
    if (impl_->decoder_initialized) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoder_initialized = false;
    }
}

bool AudioPlaybackEngine::OpenFile(const std::wstring& filePath) {
    if (!impl_->device_initialized) {
        if (!Init(false)) return false;
    }

    std::lock_guard<std::mutex> lock(impl_->decoder_mutex);

    if (impl_->decoder_initialized) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoder_initialized = false;
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, impl_->channels, impl_->sample_rate);
    ma_result res = ma_decoder_init_file_w(filePath.c_str(), &decoderConfig, &impl_->decoder);
    if (res != MA_SUCCESS) {
        std::wcerr << L"[ERROR] 无法解码音频文件: " << filePath << L" (错误码: " << res << L")" << std::endl;
        return false;
    }

    impl_->decoder_initialized = true;
    current_file_ = filePath;

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&impl_->decoder, &totalFrames) == MA_SUCCESS) {
        duration_.store(static_cast<double>(totalFrames) / impl_->sample_rate);
    } else {
        duration_.store(0.0);
    }

    current_position_.store(0.0);
    visualizer_.Reset();
    state_.store(PlaybackState::Stopped);

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
    if (state_.load() == PlaybackState::Playing) {
        Pause();
    } else if (state_.load() == PlaybackState::Paused) {
        Resume();
    } else if (state_.load() == PlaybackState::Stopped) {
        Play();
    }
}

bool AudioPlaybackEngine::SeekTo(double seconds) {
    std::lock_guard<std::mutex> lock(impl_->decoder_mutex);
    if (!impl_->decoder_initialized) return false;

    if (seconds < 0.0) seconds = 0.0;
    double maxDur = duration_.load();
    if (maxDur > 0.0 && seconds > maxDur) seconds = maxDur;

    ma_uint64 targetFrame = static_cast<ma_uint64>(seconds * impl_->sample_rate);
    ma_result res = ma_decoder_seek_to_pcm_frame(&impl_->decoder, targetFrame);
    if (res == MA_SUCCESS) {
        current_position_.store(seconds);
        visualizer_.Reset();
        return true;
    }
    return false;
}

void AudioPlaybackEngine::SetVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    volume_.store(volume);
}

float AudioPlaybackEngine::GetVolume() const {
    return volume_.load();
}

PlaybackState AudioPlaybackEngine::GetState() const {
    return state_.load();
}

double AudioPlaybackEngine::GetPositionSeconds() const {
    return current_position_.load();
}

double AudioPlaybackEngine::GetDurationSeconds() const {
    return duration_.load();
}

std::wstring AudioPlaybackEngine::GetCurrentFilePath() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_file_;
}

void AudioPlaybackEngine::OnAudioFrames(void* pOutput, uint32_t frameCount) {
    float* pOutF32 = reinterpret_cast<float*>(pOutput);
    size_t totalSamples = frameCount * impl_->channels;

    if (state_.load() != PlaybackState::Playing) {
        std::fill(pOutF32, pOutF32 + totalSamples, 0.0f);
        // 暂停/停止时持续向频谱分析器馈入静音，令能量柱自然阻尼衰减 (而非冻结在最后一帧)
        visualizer_.PushSamples(pOutF32, totalSamples, impl_->channels);
        return;
    }

    std::unique_lock<std::mutex> lock(impl_->decoder_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !impl_->decoder_initialized) {
        std::fill(pOutF32, pOutF32 + totalSamples, 0.0f);
        visualizer_.PushSamples(pOutF32, totalSamples, impl_->channels);
        return;
    }

    ma_uint64 framesRead = 0;
    ma_result res = ma_decoder_read_pcm_frames(&impl_->decoder, pOutF32, frameCount, &framesRead);

    if (res != MA_SUCCESS || framesRead == 0) {
        // 音频播放自然结束
        std::fill(pOutF32, pOutF32 + totalSamples, 0.0f);
        state_.store(PlaybackState::Stopped);
        current_position_.store(duration_.load());
        if (on_playback_ended) {
            on_playback_ended();
        }
        return;
    }

    // 若读取的帧数少于请求帧数，剩余部分静音填充
    if (framesRead < frameCount) {
        std::fill(pOutF32 + framesRead * impl_->channels, pOutF32 + totalSamples, 0.0f);
    }

    // 软音量控制 (平滑浮点线性变换)
    float vol = volume_.load();
    if (vol < 0.999f) {
        for (size_t i = 0; i < framesRead * impl_->channels; ++i) {
            pOutF32[i] *= vol;
        }
    }

    // 推进当前播放时间戳
    double advanceSec = static_cast<double>(framesRead) / impl_->sample_rate;
    current_position_.store(current_position_.load() + advanceSec);

    // 拦截真实 PCM 流推入 FFT 频谱分析器 (产生 32 频段能量柱)
    visualizer_.PushSamples(pOutF32, framesRead * impl_->channels, impl_->channels);
}
