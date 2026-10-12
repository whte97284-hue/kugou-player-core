#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include "audio_tag_parser.hpp"
#include "decryptor.hpp"
#include "lyric_manager.hpp"

namespace lyric_core {

// 酷狗本地音轨资产条目 (包含加密状态、原路径与解析产物)
struct KuGouTrackEntry {
    std::wstring source_path;       // 原始文件路径 (例如 F:\KuGou\KugouMusic\王力宏 - 你不知道的事.kgma)
    std::wstring file_name;         // 文件名
    std::wstring ext;               // 扩展名 (.kgma, .kgm, .kgg, .flac 等)
    uint64_t file_size = 0;         // 原始文件大小 (字节)
    bool is_encrypted = false;      // 是否为加密文件
    std::wstring matched_krc_path;  // 自动关联命中的 .krc 歌词物理路径
    std::wstring playable_path;     // 可直接播放的无损音频路径 (已解密产物或原生音频)
    bool is_ready_to_play = false;  // 是否可立即直通播放
    AudioMetadata metadata;         // 提取出的音频元数据 (采样率、位深、时长、标签)
};

// 酷狗音频源智能探查与即时透明解码内核
class KuGouTrackBridge {
public:
    KuGouTrackBridge();
    ~KuGouTrackBridge() = default;

    // 智能嗅探酷狗客户端当前配置的所有真实音乐下载/存储目录 (零硬编码)
    static std::vector<std::wstring> DiscoverKuGouMusicDirectories();

    // 设置/覆盖解密掩码表路径 (默认自动嗅探 tools/kgm/kgm.mask)
    void SetMaskPath(const std::wstring& maskPath);

    // 设置解密后的高速缓存输出目录 (默认 output 或 player_core/cache)
    void SetCacheOutputDir(const std::wstring& cacheDir);

    // 扫描酷狗音乐库目录，枚举所有歌曲 (自动区分加密音轨与非加密音轨，自动关联歌词)
    std::vector<KuGouTrackEntry> ScanKuGouLibrary(const std::wstring& customDir = L"");

    // 核心接口: 透明即时解析并准备可播音频 (若为加密音轨则触发 40ms 级极速解码并关联歌词)
    // 线程安全，支持直接作为播放引擎的预处理钩子
    bool PreparePlayableTrack(const std::wstring& sourcePath, KuGouTrackEntry& outEntry);

    // 批量静默入库/解码 (支持进度回调，多线程高速并发)
    size_t BatchPrepare(const std::vector<std::wstring>& sourcePaths,
                        std::function<void(size_t done, size_t total, const std::wstring& current)> onProgress = nullptr);

private:
    std::wstring mask_path_;
    std::wstring cache_dir_;
    LyricManager lyric_manager_;
    std::mutex mutex_;
};

} // namespace lyric_core
