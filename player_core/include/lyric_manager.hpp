#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <mutex>

namespace lyric_core {

// 逐字歌词片元 (KRC 独有的逐字卡拉OK时间轴)
struct LyricSyllable {
    std::wstring text;          // 字词文本
    uint32_t start_ms = 0;      // 绝对开始时间 (毫秒)
    uint32_t duration_ms = 0;   // 演唱持续时间 (毫秒)
};

// 单行歌词
struct LyricLine {
    uint32_t start_ms = 0;      // 行绝对开始时间 (毫秒)
    uint32_t duration_ms = 0;   // 行总持续时间 (毫秒)
    std::wstring full_text;     // 纯文本歌词
    std::vector<LyricSyllable> syllables; // 逐字染色信息 (仅 KRC 有, 普通 LRC 为空)
};

// 完整解析后的歌词文档
struct ParsedLyrics {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring by;
    int32_t offset_ms = 0;
    std::vector<LyricLine> lines;

    bool IsEmpty() const { return lines.empty(); }

    // 导出为国际标准通用 LRC 文本 (去除逐字标签, 输出 [mm:ss.xx] 纯文本时间轴)
    std::string ToStandardLrc() const;

    // 根据当前音频播放位置 (秒), 二分查找当前活跃歌词行索引 (-1 表示未开始)
    int FindLineIndexAt(double seconds) const;

    // 获取某一行当前正在唱的字索引 (-1 表示未唱或该行无逐字信息)
    int FindSyllableIndexAt(size_t lineIndex, double seconds) const;
};

// 酷狗本地歌词路径智能探测器 (不硬编码, 多级注册表/INI/盘符全域嗅探)
class KuGouPathFinder {
public:
    // 执行全系统多级智能嗅探, 返回所有真实存在的酷狗歌词库路径
    static std::vector<std::wstring> DiscoverLyricDirectories();

    // 手动追加自定义歌词候选目录
    static void AddCustomDirectory(const std::wstring& dir);

private:
    static bool ProbeFromKuGouIni(std::vector<std::wstring>& outDirs);
    static bool ProbeFromRegistry(std::vector<std::wstring>& outDirs);
    static bool ProbeFromLogicalDrives(std::vector<std::wstring>& outDirs);
    static bool ProbeFromSpecialFolders(std::vector<std::wstring>& outDirs);

    static std::vector<std::wstring> custom_dirs_;
    static std::mutex mutex_;
};

// KRC (Kugou ResourCe) 解密与解析器
class KrcDecoder {
public:
    // 从物理 .krc 文件解码
    static bool DecodeFile(const std::wstring& krcFilePath, ParsedLyrics& outLyrics);

    // 从二进制内存流解码 (用于流式或者包内提取)
    static bool DecodeMemory(const uint8_t* data, size_t size, ParsedLyrics& outLyrics);

    // 将已解密的原始 KRC 文本解析为结构化歌词
    static bool ParseRawKrcText(const std::string& rawUtf8Text, ParsedLyrics& outLyrics);

private:
    // 酷狗 16 字节异或密钥
    static const uint8_t KRC_KEY[16];
};

// 统一歌词管理器 (严格匹配、内存缓存、实时定位、LRC 导出)。所有公开方法线程安全。
class LyricManager {
public:
    LyricManager();
    ~LyricManager() = default;

    // 刷新并重新探测本地酷狗歌词库 (同时清空文件索引)
    void RefreshDirectories();

    // 获取当前探测到的所有有效歌词目录
    std::vector<std::wstring> GetLyricDirectories() const;

    // 为歌曲查找并载入酷狗 .krc 歌词:先用标签里的歌名 / 歌手,再用文件名 (「歌手 - 歌名.flac」)。
    // 歌名须规范化后完全一致;双方都有歌手时歌手也须相符,宁缺毋错。
    bool LoadLyricsForTrack(const std::wstring& audioFilePath, const std::wstring& title, const std::wstring& artist, ParsedLyrics& outLyrics);

    // 根据文件路径或标识保存转换后的标准 .lrc 文件到目标位置
    bool ExportLrcFile(const ParsedLyrics& lyrics, const std::wstring& targetLrcPath);

    // 在已探测的歌词库中搜索最匹配的 .krc 文件 (找不到返回空)
    std::wstring FindMatchingKrcFile(const std::wstring& audioFilePath, const std::wstring& title, const std::wstring& artist);

private:
    struct KrcEntry {
        std::wstring title;   // 规范化后的歌名
        std::wstring artist;  // 规范化后的歌手
        std::wstring path;
        uint64_t mtime = 0;
    };
    bool EnsureIndexLocked(bool onMiss); // 返回是否重建了索引
    std::vector<uint64_t> DirStampsLocked() const;
    std::wstring MatchLocked(const std::wstring& title, const std::wstring& artist) const;

    std::vector<std::wstring> search_dirs_;
    std::vector<KrcEntry> index_;
    std::vector<uint64_t> dir_stamps_; // 建索引时各歌词目录的修改时间
    int64_t index_built_at_ = 0;   // 秒 (steady clock),0 表示尚未建立
    std::unordered_map<std::wstring, ParsedLyrics> cache_; // key: .krc 路径,容量有上限
    mutable std::mutex mutex_;
};

} // namespace lyric_core
