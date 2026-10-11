#include "../include/audio_ingestion_pipeline.hpp"
#include "../include/audio_tag_parser.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <chrono>

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct KgmHeaderRaw {
    uint8_t  magic[16];
    uint32_t header_len;
    uint32_t crypto_ver;
    uint32_t key_derive;
    uint8_t  file_key[16];
};
#pragma pack(pop)

static std::wstring SniffExt(const uint8_t* buf, size_t len) {
    if (len >= 4 && buf[0] == 'f' && buf[1] == 'L' && buf[2] == 'a' && buf[3] == 'C') return L".flac";
    if (len >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') return L".mp3";
    if (len >= 2 && buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0) return L".mp3";
    if (len >= 4 && buf[0] == 'O' && buf[1] == 'g' && buf[2] == 'g' && buf[3] == 'S') return L".ogg";
    return L".flac";
}

AudioIngestionPipeline::AudioIngestionPipeline() {
    if (fs::exists("tools/kgm/kgm.mask")) {
        kgm_mask_path_ = L"tools/kgm/kgm.mask";
    }
    target_lib_dir_ = L"output";
    thread_count_ = std::max(2u, std::thread::hardware_concurrency());
}

AudioIngestionPipeline::~AudioIngestionPipeline() {
    Cancel();
    if (async_worker_thread_.joinable()) {
        async_worker_thread_.join();
    }
}

void AudioIngestionPipeline::SetTargetLibraryDir(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    target_lib_dir_ = dir;
}

void AudioIngestionPipeline::SetKgmMaskPath(const std::wstring& maskPath) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    kgm_mask_path_ = maskPath;
}

void AudioIngestionPipeline::SetThreadCount(size_t threads) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    thread_count_ = (threads == 0) ? std::max(2u, std::thread::hardware_concurrency()) : threads;
}

void AudioIngestionPipeline::SetLazyCoverExtract(bool lazy) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    lazy_cover_extract_ = lazy;
}

void AudioIngestionPipeline::AddInputPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    input_paths_.push_back(path);
}

void AudioIngestionPipeline::ClearInputPaths() {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    input_paths_.clear();
}

void AudioIngestionPipeline::Cancel() {
    cancel_requested_.store(true);
}

bool AudioIngestionPipeline::DecryptKgm(const std::wstring& src, const std::wstring& targetDir, std::wstring& outDecryptedPath) {
    if (!fs::exists(kgm_mask_path_)) return false;

    HANDLE hMask = CreateFileW(kgm_mask_path_.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hMask == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER maskSize;
    GetFileSizeEx(hMask, &maskSize);
    uint32_t maskLen = static_cast<uint32_t>(maskSize.QuadPart);
    HANDLE hMap = CreateFileMappingW(hMask, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) { CloseHandle(hMask); return false; }
    const uint8_t* pMask = static_cast<const uint8_t*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
    if (!pMask) { CloseHandle(hMap); CloseHandle(hMask); return false; }

    HANDLE hIn = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hIn == INVALID_HANDLE_VALUE) {
        UnmapViewOfFile(pMask); CloseHandle(hMap); CloseHandle(hMask);
        return false;
    }

    KgmHeaderRaw header;
    DWORD readBytes = 0;
    if (!ReadFile(hIn, &header, sizeof(KgmHeaderRaw), &readBytes, NULL) || readBytes < sizeof(KgmHeaderRaw)) {
        CloseHandle(hIn);
        UnmapViewOfFile(pMask); CloseHandle(hMap); CloseHandle(hMask);
        return false;
    }

    uint8_t extKey[17];
    std::memcpy(extKey, header.file_key, 16);
    extKey[16] = 0x00;

    LARGE_INTEGER off;
    off.QuadPart = header.header_len;
    SetFilePointerEx(hIn, off, NULL, FILE_BEGIN);

    constexpr size_t BUF_SIZE = 64 * 1024;
    std::vector<uint8_t> buffer(BUF_SIZE);
    DWORD firstRead = 0;
    if (!ReadFile(hIn, buffer.data(), BUF_SIZE, &firstRead, NULL) || firstRead == 0) {
        CloseHandle(hIn);
        UnmapViewOfFile(pMask); CloseHandle(hMap); CloseHandle(hMask);
        return false;
    }

    // 绝对准确的 KGM v4 掩码还原算法
    for (DWORD i = 0; i < firstRead; ++i) {
        uint8_t k = extKey[i % 17];
        uint8_t m = pMask[i % maskLen];
        uint8_t t = buffer[i] ^ k;
        uint8_t low = (m & 0x0F) ^ (t & 0x0F);
        uint8_t high = (t >> 4) ^ (m >> 4) ^ low;
        buffer[i] = static_cast<uint8_t>(((high & 0x0F) << 4) | (low & 0x0F));
    }

    std::wstring ext = SniffExt(buffer.data(), firstRead);
    fs::path inPath(src);
    fs::path outPath = fs::path(targetDir) / (inPath.stem().wstring() + ext);
    outDecryptedPath = outPath.wstring();

    HANDLE hOut = CreateFileW(outDecryptedPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
        CloseHandle(hIn);
        UnmapViewOfFile(pMask); CloseHandle(hMap); CloseHandle(hMask);
        return false;
    }

    DWORD written = 0;
    WriteFile(hOut, buffer.data(), firstRead, &written, NULL);

    uint64_t totalOffset = firstRead;
    while (ReadFile(hIn, buffer.data(), BUF_SIZE, &readBytes, NULL) && readBytes > 0) {
        for (DWORD i = 0; i < readBytes; ++i) {
            uint64_t curOff = totalOffset + i;
            uint8_t k = extKey[curOff % 17];
            uint8_t m = pMask[curOff % maskLen];
            uint8_t t = buffer[i] ^ k;
            uint8_t low = (m & 0x0F) ^ (t & 0x0F);
            uint8_t high = (t >> 4) ^ (m >> 4) ^ low;
            buffer[i] = static_cast<uint8_t>(((high & 0x0F) << 4) | (low & 0x0F));
        }
        WriteFile(hOut, buffer.data(), readBytes, &written, NULL);
        totalOffset += readBytes;
    }

    CloseHandle(hOut);
    CloseHandle(hIn);
    UnmapViewOfFile(pMask);
    CloseHandle(hMap);
    CloseHandle(hMask);
    return true;
}

std::vector<IngestionItemResult> AudioIngestionPipeline::RunSync(MusicCatalog& catalog) {
    is_running_.store(true);
    cancel_requested_.store(false);

    auto tStart = std::chrono::high_resolution_clock::now();
    std::vector<IngestionItemResult> allResults;

    // 1. 递归展开扫描全量文件
    std::vector<fs::path> targetFiles;
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        for (const auto& inStr : input_paths_) {
            fs::path p(inStr);
            if (fs::is_directory(p)) {
                for (const auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().wstring();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                        if (ext == L".flac" || ext == L".mp3" || ext == L".ogg" || ext == L".wav" ||
                            ext == L".kgma" || ext == L".kgm" || ext == L".kgg" || ext == L".ncm") {
                            targetFiles.push_back(entry.path());
                        }
                    }
                }
            } else if (fs::is_regular_file(p)) {
                targetFiles.push_back(p);
            }
        }
    }

    fs::create_directories(target_lib_dir_);
    size_t total = targetFiles.size();
    if (total == 0) {
        is_running_.store(false);
        return allResults;
    }

    // 2. 多线程工作并发池构建
    size_t numThreads = std::min(thread_count_, total);
    if (numThreads == 0) numThreads = 1;

    std::atomic<size_t> nextIndex{0};
    std::atomic<size_t> processedCount{0};
    std::mutex resultsMutex;
    std::mutex catalogMutex;

    auto workerFunc = [&]() {
        std::vector<std::pair<std::wstring, AudioMetadata>> localBatch;
        localBatch.reserve(100);

        while (!cancel_requested_.load()) {
            size_t idx = nextIndex.fetch_add(1);
            if (idx >= total) break;

            const auto& filePath = targetFiles[idx];
            auto ext = filePath.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

            IngestionItemResult item;
            item.source_path = filePath.wstring();

            if (ext == L".kgma" || ext == L".kgm") {
                item.was_encrypted = true;
                std::wstring decPath;
                bool ok = DecryptKgm(filePath.wstring(), target_lib_dir_, decPath);
                if (ok) {
                    item.target_path = decPath;
                    item.success = true;
                    item.message = L"解密成功并持久化落盘";
                    AudioMetadata meta;
                    if (AudioTagParser::ParseFile(decPath, meta)) {
                        localBatch.emplace_back(decPath, meta);
                    }
                } else {
                    item.success = false;
                    item.message = L"KGM 解密失败";
                }
            } else if (ext == L".ncm" || ext == L".kgg") {
                item.was_encrypted = true;
                fs::path candidate = fs::path(target_lib_dir_) / (filePath.stem().wstring() + L".flac");
                if (!fs::exists(candidate)) candidate = fs::path(target_lib_dir_) / (filePath.stem().wstring() + L".mp3");
                if (!fs::exists(candidate)) candidate = fs::path(target_lib_dir_) / (filePath.stem().wstring() + L".ogg");

                if (fs::exists(candidate)) {
                    item.target_path = candidate.wstring();
                    item.success = true;
                    item.message = L"关联到已有解码音频并入库";
                    AudioMetadata meta;
                    if (AudioTagParser::ParseFile(candidate.wstring(), meta)) {
                        localBatch.emplace_back(candidate.wstring(), meta);
                    }
                } else {
                    item.success = true;
                    item.target_path = filePath.wstring();
                    item.message = L"入库登记待转换源";
                    AudioMetadata meta;
                    meta.format = ext.substr(1);
                    std::transform(meta.format.begin(), meta.format.end(), meta.format.begin(), ::towupper);
                    localBatch.emplace_back(filePath.wstring(), meta);
                }
            } else {
                item.was_encrypted = false;
                item.target_path = filePath.wstring();
                AudioMetadata meta;
                if (AudioTagParser::ParseFile(filePath.wstring(), meta)) {
                    localBatch.emplace_back(filePath.wstring(), meta);
                    item.success = true;
                    item.message = L"直接入库成功";
                } else {
                    item.success = false;
                    item.message = L"元数据解析失败";
                }
            }

            // 局部批次提交，极大减少对曲库的锁争用
            if (localBatch.size() >= 100) {
                std::lock_guard<std::mutex> catLock(catalogMutex);
                for (const auto& pair : localBatch) {
                    catalog.AddTrack(pair.first, pair.second);
                }
                localBatch.clear();
            }

            {
                std::lock_guard<std::mutex> resLock(resultsMutex);
                allResults.push_back(item);
            }

            size_t done = processedCount.fetch_add(1) + 1;
            if (on_progress) {
                IngestionProgress prog;
                prog.current = done;
                prog.total = total;
                prog.current_filename = filePath.filename().wstring();
                prog.is_transcoding = item.was_encrypted;
                prog.status_text = item.message;
                on_progress(prog);
            }
        }

        // 提交剩余批次
        if (!localBatch.empty()) {
            std::lock_guard<std::mutex> catLock(catalogMutex);
            for (const auto& pair : localBatch) {
                catalog.AddTrack(pair.first, pair.second);
            }
            localBatch.clear();
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (size_t t = 0; t < numThreads; ++t) {
        workers.emplace_back(workerFunc);
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    is_running_.store(false);
    return allResults;
}

bool AudioIngestionPipeline::StartAsync(MusicCatalog& catalog,
                                       std::function<void(const IngestionProgress&)> onProgress,
                                       std::function<void(const IngestionSummary&)> onFinished) {
    if (is_running_.load()) return false;

    if (async_worker_thread_.joinable()) {
        async_worker_thread_.join();
    }

    is_running_.store(true);
    cancel_requested_.store(false);

    async_worker_thread_ = std::thread([this, &catalog, onProgress, onFinished]() {
        auto t0 = std::chrono::high_resolution_clock::now();
        this->on_progress = onProgress;
        auto results = this->RunSync(catalog);
        auto t1 = std::chrono::high_resolution_clock::now();

        if (onFinished) {
            IngestionSummary summary;
            summary.total_scanned = results.size();
            for (const auto& r : results) {
                if (r.success) summary.success_count++;
                else summary.failed_count++;
                if (r.was_encrypted) summary.transcoded_count++;
            }
            summary.elapsed_seconds = std::chrono::duration<double>(t1 - t0).count();
            onFinished(summary);
        }
    });

    return true;
}
