#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include "music_catalog.hpp"

struct IngestionItemResult {
    std::wstring source_path;
    std::wstring target_path;
    bool was_encrypted = false;
    bool success = false;
    std::wstring message;
};

struct IngestionProgress {
    size_t current = 0;
    size_t total = 0;
    std::wstring current_filename;
    std::wstring status_text;
    bool is_transcoding = false;
};

struct IngestionSummary {
    size_t total_scanned = 0;
    size_t success_count = 0;
    size_t transcoded_count = 0;
    size_t failed_count = 0;
    double elapsed_seconds = 0.0;
};

class AudioIngestionPipeline {
public:
    AudioIngestionPipeline();
    ~AudioIngestionPipeline();

    // 配置参数
    void SetTargetLibraryDir(const std::wstring& dir);
    void SetKgmMaskPath(const std::wstring& maskPath);
    void SetThreadCount(size_t threads);               // 默认自适应多核 CPU 线程数
    void SetLazyCoverExtract(bool lazy);               // 开启后入库不抽取落盘几万张图片，光速构建万级曲库

    // 输入源管理
    void AddInputPath(const std::wstring& path);
    void ClearInputPaths();

    // 模式 A: 多线程并发同步执行 (阻塞当前线程，供测试与脚本使用)
    std::vector<IngestionItemResult> RunSync(MusicCatalog& catalog);

    // 模式 B: 多线程并发异步执行 (立即返回，完全不卡主线程/UI)
    bool StartAsync(MusicCatalog& catalog,
                    std::function<void(const IngestionProgress&)> onProgress = nullptr,
                    std::function<void(const IngestionSummary&)> onFinished = nullptr);

    // 控制与状态
    void Cancel();
    bool IsRunning() const { return is_running_.load(); }

    // 进度回调通知 (同步模式下触发)
    std::function<void(const IngestionProgress&)> on_progress;

private:
    bool DecryptKgm(const std::wstring& src, const std::wstring& targetDir, std::wstring& outDecryptedPath);

    std::wstring target_lib_dir_;
    std::wstring kgm_mask_path_;
    std::vector<std::wstring> input_paths_;
    size_t thread_count_ = 0;
    bool lazy_cover_extract_ = false;

    std::atomic<bool> is_running_{false};
    std::atomic<bool> cancel_requested_{false};
    std::thread async_worker_thread_;
    std::mutex pipeline_mutex_;
};
