#pragma once
#include <vector>
#include <cstdint>
#include <mutex>
#include <cmath>

class FftVisualizer {
public:
    static constexpr size_t FFT_SIZE = 1024;      // 1024 点 FFT
    static constexpr size_t NUM_BANDS = 32;       // 32 段离散频域能量柱

    FftVisualizer();
    ~FftVisualizer() = default;

    // 供给音频回调写入 PCM 采样数据 (单声道或双声道交错输入)
    void PushSamples(const float* samples, size_t count, size_t channels);

    // 获取当前平滑后的 32 频段能量值 (范围 0.0f ~ 1.0f，按 60FPS 动效刷新调用)
    std::vector<float> GetSpectrumBars();

    // 重置能量柱
    void Reset();

private:
    void ComputeFft(std::vector<float>& real, std::vector<float>& imag);

    std::mutex mtx_;
    std::vector<float> sample_buffer_;
    std::vector<float> window_;                  // 预计算 Hann 窗
    std::vector<float> current_bars_;            // 当前瞬时频段幅值
    std::vector<float> smooth_bars_;             // 动效平滑阻尼能量柱
};
