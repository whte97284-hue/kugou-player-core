#include "../include/fft_visualizer.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

FftVisualizer::FftVisualizer() {
    sample_buffer_.reserve(FFT_SIZE * 2);
    window_.resize(FFT_SIZE);
    current_bars_.assign(NUM_BANDS, 0.0f);
    smooth_bars_.assign(NUM_BANDS, 0.0f);

    // 预计算 Hann 窗函数
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (FFT_SIZE - 1)));
    }
}

void FftVisualizer::Reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    sample_buffer_.clear();
    std::fill(current_bars_.begin(), current_bars_.end(), 0.0f);
    std::fill(smooth_bars_.begin(), smooth_bars_.end(), 0.0f);
}

void FftVisualizer::PushSamples(const float* samples, size_t count, size_t channels) {
    if (!samples || count == 0 || channels == 0) return;

    std::lock_guard<std::mutex> lock(mtx_);
    // 多声道混音为单声道浮点样本
    size_t frames = count / channels;
    for (size_t i = 0; i < frames; ++i) {
        float mono = 0.0f;
        for (size_t c = 0; c < channels; ++c) {
            mono += samples[i * channels + c];
        }
        mono /= static_cast<float>(channels);
        sample_buffer_.push_back(mono);
    }

    // 维持缓冲区大小不超过 2 * FFT_SIZE
    if (sample_buffer_.size() > FFT_SIZE * 2) {
        sample_buffer_.erase(sample_buffer_.begin(), sample_buffer_.begin() + (sample_buffer_.size() - FFT_SIZE * 2));
    }
}

void FftVisualizer::ComputeFft(std::vector<float>& real, std::vector<float>& imag) {
    size_t n = real.size();
    // 1. 位反转排列 (Bit-reversal permutation)
    size_t j = 0;
    for (size_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        size_t k = n / 2;
        while (k <= j) {
            j -= k;
            k /= 2;
        }
        j += k;
    }

    // 2. Cooley-Tukey 蝶形变换
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * static_cast<float>(M_PI) / len;
        float wlen_r = std::cos(angle);
        float wlen_i = std::sin(angle);

        for (size_t i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;

            for (size_t k = 0; k < len / 2; ++k) {
                float u_r = real[i + k];
                float u_i = imag[i + k];
                float v_r = real[i + k + len / 2] * w_r - imag[i + k + len / 2] * w_i;
                float v_i = real[i + k + len / 2] * w_i + imag[i + k + len / 2] * w_r;

                real[i + k] = u_r + v_r;
                imag[i + k] = u_i + v_i;
                real[i + k + len / 2] = u_r - v_r;
                imag[i + k + len / 2] = u_i - v_i;

                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }
}

std::vector<float> FftVisualizer::GetSpectrumBars() {
    std::vector<float> input(FFT_SIZE, 0.0f);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (sample_buffer_.size() >= FFT_SIZE) {
            size_t start = sample_buffer_.size() - FFT_SIZE;
            for (size_t i = 0; i < FFT_SIZE; ++i) {
                input[i] = sample_buffer_[start + i] * window_[i];
            }
        } else if (!sample_buffer_.empty()) {
            size_t available = sample_buffer_.size();
            for (size_t i = 0; i < available; ++i) {
                input[i] = sample_buffer_[i] * window_[i];
            }
        }
    }

    std::vector<float> real = input;
    std::vector<float> imag(FFT_SIZE, 0.0f);
    ComputeFft(real, imag);

    // 计算幅值与对数频带划分
    size_t half = FFT_SIZE / 2; // 512 bins
    std::vector<float> mags(half, 0.0f);
    for (size_t i = 0; i < half; ++i) {
        mags[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]) / (FFT_SIZE / 2);
    }

    // 将 512 个频点按对数尺度映射到 32 个能量频柱
    std::vector<float> newBars(NUM_BANDS, 0.0f);
    for (size_t b = 0; b < NUM_BANDS; ++b) {
        // 对数尺度计算频段起始与终止 bin
        double fStart = std::pow(double(half), double(b) / NUM_BANDS);
        double fEnd = std::pow(double(half), double(b + 1) / NUM_BANDS);
        size_t binStart = static_cast<size_t>(std::floor(fStart));
        size_t binEnd = static_cast<size_t>(std::ceil(fEnd));
        if (binStart >= half) binStart = half - 1;
        if (binEnd > half) binEnd = half;
        if (binEnd <= binStart) binEnd = binStart + 1;

        float maxMag = 0.0f;
        for (size_t k = binStart; k < binEnd && k < half; ++k) {
            if (mags[k] > maxMag) maxMag = mags[k];
        }

        // 高频增益补偿 (人耳听觉灵敏度等响度修正)
        float boost = 1.0f + static_cast<float>(b) * 0.15f;
        float val = maxMag * boost;

        // 分贝映射并归一化到 0.0 ~ 1.0
        float db = 20.0f * std::log10(val + 1e-5f);
        float norm = (db + 60.0f) / 60.0f; // -60dB 到 0dB
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        newBars[b] = norm;
    }

    // 时间轴平滑滤波 (Attack 快速上升 + Decay 慢速阻尼下落)
    std::lock_guard<std::mutex> lock(mtx_);
    for (size_t b = 0; b < NUM_BANDS; ++b) {
        if (newBars[b] >= smooth_bars_[b]) {
            // 上升响应灵敏
            smooth_bars_[b] = smooth_bars_[b] * 0.4f + newBars[b] * 0.6f;
        } else {
            // 下落平滑衰减
            smooth_bars_[b] = std::max(0.0f, smooth_bars_[b] - 0.04f);
        }
    }

    return smooth_bars_;
}
