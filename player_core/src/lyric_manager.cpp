#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/lyric_manager.hpp"
#include "../include/tinf.hpp"
#include "../include/audio_tag_parser.hpp"
#include "../include/platform_io.hpp"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cwctype>

namespace fs = std::filesystem;

namespace lyric_core {

// 酷狗 16 字节解密异或密钥
const uint8_t KrcDecoder::KRC_KEY[16] = {
    0x40, 0x47, 0x61, 0x77, 0x5E, 0x32, 0x74, 0x47,
    0x51, 0x36, 0x31, 0x2D, 0xCE, 0xD2, 0x6E, 0x69
};

std::vector<std::wstring> KuGouPathFinder::custom_dirs_;
std::mutex KuGouPathFinder::mutex_;

namespace {

std::wstring Utf8ToWide(const std::string& str) { return AudioTagParser::Utf8ToWide(str); }
std::string WideToUtf8(const std::wstring& wstr) { return AudioTagParser::WideToUtf8(wstr); }

bool DirExists(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}
bool DirExists(const std::wstring& p) { return platform::IsDirectory(p); }
#ifdef _WIN32
bool DirExists(const wchar_t* p) { return platform::IsDirectory(p); }
#endif

// 不经 std::filesystem 取文件名主干 (非 Windows 下 wstring -> path 的转换依赖区域设置)
std::wstring FileStem(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"/\\");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos && dot > 0) name.erase(dot);
    return name;
}

void Trim(std::wstring& s) {
    while (!s.empty() && std::iswspace(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::iswspace(s[i])) ++i;
    s.erase(0, i);
}

// 匹配用规范化:全角转半角、小写、合并空白、去掉转换器后缀与副本序号
std::wstring Normalize(std::wstring s) {
    for (auto& c : s) {
        if (c >= 0xFF01 && c <= 0xFF5E) c = static_cast<wchar_t>(c - 0xFEE0);
        else if (c == 0x3000) c = L' ';
        c = static_cast<wchar_t>(std::towlower(c));
    }
    static const std::wstring suffixes[] = {L"_kgg-dec", L"_kgm-dec", L"_kgma-dec", L"_vpr-dec", L"_ncm-dec",
                                            L"[hires]", L"[hi-res]", L"(1)", L"(2)", L"(3)"};
    bool stripped = true;
    while (stripped) {
        stripped = false;
        Trim(s);
        for (const auto& suf : suffixes) {
            if (s.size() > suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0) {
                s.erase(s.size() - suf.size());
                stripped = true;
            }
        }
    }
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (std::iswspace(c)) {
            if (!out.empty() && out.back() != L' ') out += L' ';
        } else {
            out += c;
        }
    }
    Trim(out);
    return out;
}

// 「歌手 - 歌名」拆分 (只认带空格的分隔符,避免 A-ha、K-ON 这类名字被拆开)
void SplitArtistTitle(const std::wstring& stem, std::wstring& artist, std::wstring& title) {
    size_t sep = stem.find(L" - ");
    if (sep == std::wstring::npos) {
        artist.clear();
        title = stem;
    } else {
        artist = stem.substr(0, sep);
        title = stem.substr(sep + 3);
    }
}

bool IsHex32(const std::wstring& s, size_t pos) {
    if (pos + 32 > s.size()) return false;
    for (size_t i = pos; i < pos + 32; ++i) {
        if (!std::iswxdigit(s[i])) return false;
    }
    return true;
}

// 酷狗歌词文件名: 「歌手 - 歌名-<32 位 hash>-<id>-<序号>.krc」,去掉 hash 及其后缀
std::wstring StripKrcHashSuffix(const std::wstring& stem) {
    for (size_t pos = stem.rfind(L'-'); pos != std::wstring::npos && pos > 0; pos = stem.rfind(L'-', pos - 1)) {
        if (IsHex32(stem, pos + 1)) return stem.substr(0, pos);
    }
    return stem;
}

// 多歌手拆分:、 , & / ; feat.
std::vector<std::wstring> SplitArtists(const std::wstring& a) {
    std::vector<std::wstring> out;
    std::wstring cur;
    auto flush = [&]() {
        // 去掉括号里的别名,如 "aimer (エメ)"
        size_t paren = cur.find(L'(');
        if (paren != std::wstring::npos && paren > 0) cur.erase(paren);
        Trim(cur);
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
    };
    for (wchar_t c : a) {
        if (c == L'\u3001' || c == L',' || c == L'&' || c == L'/' || c == L';' || c == L'\uFF0C') flush();
        else cur += c;
    }
    flush();
    return out;
}

bool ArtistsMatch(const std::wstring& a, const std::wstring& b) {
    for (const auto& x : SplitArtists(a)) {
        for (const auto& y : SplitArtists(b)) {
            if (x == y) return true;
        }
    }
    return false;
}

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}

} // namespace

// ============================================================================
// ParsedLyrics 实用方法
// ============================================================================

std::string ParsedLyrics::ToStandardLrc() const {
    std::ostringstream oss;
    if (!title.empty()) oss << "[ti:" << WideToUtf8(title) << "]\r\n";
    if (!artist.empty()) oss << "[ar:" << WideToUtf8(artist) << "]\r\n";
    if (!album.empty()) oss << "[al:" << WideToUtf8(album) << "]\r\n";
    if (!by.empty()) oss << "[by:" << WideToUtf8(by) << "]\r\n";
    if (offset_ms != 0) oss << "[offset:" << offset_ms << "]\r\n";

    for (const auto& line : lines) {
        uint32_t totalSec = line.start_ms / 1000;
        uint32_t min = totalSec / 60;
        uint32_t sec = totalSec % 60;
        uint32_t cs = (line.start_ms % 1000) / 10; // 百分秒

        oss << "[" << std::setfill('0') << std::setw(2) << min << ":"
            << std::setfill('0') << std::setw(2) << sec << "."
            << std::setfill('0') << std::setw(2) << cs << "]"
            << WideToUtf8(line.full_text) << "\r\n";
    }
    return oss.str();
}

int ParsedLyrics::FindLineIndexAt(double seconds) const {
    if (lines.empty() || !(seconds >= 0)) return -1;
    const uint32_t targetMs = seconds >= 4294967.0 ? 0xFFFFFFFFu : static_cast<uint32_t>(seconds * 1000.0);

    int left = 0;
    int right = static_cast<int>(lines.size()) - 1;
    int ans = -1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (lines[mid].start_ms <= targetMs) {
            ans = mid;
            left = mid + 1; // 寻找最靠后的满足条件的行
        } else {
            right = mid - 1;
        }
    }
    return ans;
}

int ParsedLyrics::FindSyllableIndexAt(size_t lineIndex, double seconds) const {
    if (lineIndex >= lines.size()) return -1;
    const auto& line = lines[lineIndex];
    if (line.syllables.empty() || !(seconds >= 0)) return -1;

    const uint32_t targetMs = seconds >= 4294967.0 ? 0xFFFFFFFFu : static_cast<uint32_t>(seconds * 1000.0);
    for (size_t i = 0; i < line.syllables.size(); ++i) {
        uint32_t start = line.syllables[i].start_ms;
        uint32_t end = start + line.syllables[i].duration_ms;
        if (targetMs >= start && targetMs < end) {
            return static_cast<int>(i);
        }
    }
    if (targetMs >= line.syllables.back().start_ms) {
        return static_cast<int>(line.syllables.size()) - 1;
    }
    return -1;
}

// ============================================================================
// KuGouPathFinder 智能嗅探
// ============================================================================

void KuGouPathFinder::AddCustomDirectory(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dir.empty() && DirExists(dir)) {
        custom_dirs_.push_back(dir);
    }
}

#ifdef _WIN32
bool KuGouPathFinder::ProbeFromKuGouIni(std::vector<std::wstring>& outDirs) {
    // 优先读取 %APPDATA%\KuGou8\KuGou.ini
    wchar_t appData[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) return false;

    fs::path iniPath = fs::path(appData) / L"KuGou8" / L"KuGou.ini";
    std::error_code ec;
    if (!fs::is_regular_file(iniPath, ec)) return false;

    const size_t before = outDirs.size();
    wchar_t buf[MAX_PATH] = {0};
    // 1. 读取 [LyricConfigSection] -> LyricPath
    GetPrivateProfileStringW(L"LyricConfigSection", L"LyricPath", L"", buf, MAX_PATH, iniPath.c_str());
    if (buf[0] != L'\0' && DirExists(buf)) {
        outDirs.push_back(buf);
    }

    // 2. 读取 [DownloadConfigSection] -> DownloadPath 下的 Lyric 子目录
    buf[0] = L'\0';
    GetPrivateProfileStringW(L"DownloadConfigSection", L"DownloadPath", L"", buf, MAX_PATH, iniPath.c_str());
    if (buf[0] != L'\0') {
        fs::path lyr = fs::path(buf) / L"Lyric";
        if (DirExists(lyr)) outDirs.push_back(lyr.wstring());
    }
    return outDirs.size() > before;
}

bool KuGouPathFinder::ProbeFromRegistry(std::vector<std::wstring>& outDirs) {
    // 检查注册表 HKCU\Software\KuGou\Prefetcher
    const size_t before = outDirs.size();
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\KuGou\\Prefetcher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t pathBuf[MAX_PATH + 1] = {0};
        DWORD bytes = MAX_PATH * sizeof(wchar_t); // 留一个字符给结尾 0 (注册表字符串不保证以 0 结尾)
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"LastDetectedPath", NULL, &type, reinterpret_cast<LPBYTE>(pathBuf), &bytes) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            fs::path exePath(pathBuf);
            std::error_code ec;
            if (fs::exists(exePath, ec)) {
                // KGMusic 根目录
                fs::path lyr = exePath.parent_path().parent_path() / L"Lyric";
                if (DirExists(lyr)) outDirs.push_back(lyr.wstring());
            }
        }
        RegCloseKey(hKey);
    }
    return outDirs.size() > before;
}

bool KuGouPathFinder::ProbeFromLogicalDrives(std::vector<std::wstring>& outDirs) {
    // 遍历本地固定磁盘根目录下的 KuGou\Lyric (不碰可移动盘与网络盘,避免无盘光驱 / 掉线网盘卡住)
    wchar_t drives[512] = {0};
    DWORD len = GetLogicalDriveStringsW(511, drives);
    if (len == 0 || len > 511) return false;

    const size_t before = outDirs.size();
    for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
        if (GetDriveTypeW(p) != DRIVE_FIXED) continue;
        fs::path candidate = fs::path(p) / L"KuGou" / L"Lyric";
        if (DirExists(candidate)) outDirs.push_back(candidate.wstring());
    }
    return outDirs.size() > before;
}

bool KuGouPathFinder::ProbeFromSpecialFolders(std::vector<std::wstring>& outDirs) {
    // 检查标准系统音乐目录 %USERPROFILE%\Music\KuGou\Lyric
    wchar_t musicPath[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYMUSIC, NULL, 0, musicPath))) {
        fs::path p = fs::path(musicPath) / L"KuGou" / L"Lyric";
        if (DirExists(p)) {
            outDirs.push_back(p.wstring());
            return true;
        }
    }
    return false;
}
#else
// 非 Windows 平台没有酷狗客户端,只认自定义目录与 ~/Music/KuGou/Lyric
bool KuGouPathFinder::ProbeFromKuGouIni(std::vector<std::wstring>&) { return false; }
bool KuGouPathFinder::ProbeFromRegistry(std::vector<std::wstring>&) { return false; }
bool KuGouPathFinder::ProbeFromLogicalDrives(std::vector<std::wstring>&) { return false; }
bool KuGouPathFinder::ProbeFromSpecialFolders(std::vector<std::wstring>& outDirs) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return false;
    fs::path p = fs::path(home) / "Music" / "KuGou" / "Lyric";
    if (!DirExists(p)) return false;
    outDirs.push_back(Utf8ToWide(p.string()));
    return true;
}
#endif

std::vector<std::wstring> KuGouPathFinder::DiscoverLyricDirectories() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::wstring> results;

    // 1. 用户自定义目录
    for (const auto& d : custom_dirs_) {
        if (DirExists(d)) results.push_back(d);
    }

    // 2. INI 配置 (最准)
    ProbeFromKuGouIni(results);

    // 3. 注册表
    ProbeFromRegistry(results);

    // 4. 全局逻辑驱动器
    ProbeFromLogicalDrives(results);

    // 5. 用户媒体夹
    ProbeFromSpecialFolders(results);

    // 去重与规范化 (Windows 路径大小写不敏感)
    std::vector<std::wstring> uniqueDirs;
    std::vector<std::wstring> keys;
    for (const auto& dir : results) {
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(dir, ec);
        std::wstring canonStr = ec ? dir : canon.wstring();
        while (canonStr.size() > 1 && (canonStr.back() == L'\\' || canonStr.back() == L'/')) canonStr.pop_back();
        std::wstring key = canonStr;
#ifdef _WIN32
        for (auto& c : key) c = static_cast<wchar_t>(std::towlower(c));
#endif
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
            uniqueDirs.push_back(canonStr);
        }
    }
    return uniqueDirs;
}

// ============================================================================
// KrcDecoder 解密与逐字解析
// ============================================================================

bool KrcDecoder::DecodeFile(const std::wstring& krcFilePath, ParsedLyrics& outLyrics) {
    platform::ReadOnlyFile f;
    if (!f.Open(krcFilePath, false)) return false;
    const uint64_t size = f.Size();
    if (size <= 4 || size > 10 * 1024 * 1024) return false; // 限制在 10MB 内

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (f.ReadAt(0, buffer.data(), buffer.size()) != buffer.size()) return false;
    return DecodeMemory(buffer.data(), buffer.size(), outLyrics);
}

bool KrcDecoder::DecodeMemory(const uint8_t* data, size_t size, ParsedLyrics& outLyrics) {
    if (!data || size <= 4) return false;

    // 1. 验证魔数 krc1 (0x6B, 0x72, 0x63, 0x31)
    if (data[0] != 'k' || data[1] != 'r' || data[2] != 'c' || data[3] != '1') {
        return false;
    }

    // 2. 异或混淆还原 (跳过前 4 字节魔数)
    size_t payloadSize = size - 4;
    std::vector<uint8_t> decrypted(payloadSize);
    for (size_t i = 0; i < payloadSize; ++i) {
        decrypted[i] = data[4 + i] ^ KRC_KEY[i % 16];
    }

    // 3. Zlib / Deflate 解压
    std::vector<uint8_t> decompressed;
    if (!ZlibDecompressor::DecompressZlib(decrypted.data(), decrypted.size(), decompressed)) {
        return false;
    }

    // 4. 解析 UTF-8 文本
    std::string rawText(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
    return ParseRawKrcText(rawText, outLyrics);
}

bool KrcDecoder::ParseRawKrcText(const std::string& rawUtf8Text, ParsedLyrics& outLyrics) {
    outLyrics.lines.clear();
    outLyrics.title.clear();
    outLyrics.artist.clear();
    outLyrics.album.clear();
    outLyrics.by.clear();
    outLyrics.offset_ms = 0;

    std::istringstream iss(rawUtf8Text);
    std::string lineStr;

    while (std::getline(iss, lineStr)) {
        // 去除末尾换行
        while (!lineStr.empty() && (lineStr.back() == '\r' || lineStr.back() == ' ')) {
            lineStr.pop_back();
        }
        if (lineStr.empty()) continue;

        // 去除 UTF-8 BOM
        if (lineStr.size() >= 3 && static_cast<uint8_t>(lineStr[0]) == 0xEF &&
            static_cast<uint8_t>(lineStr[1]) == 0xBB && static_cast<uint8_t>(lineStr[2]) == 0xBF) {
            lineStr = lineStr.substr(3);
        }

        // 解析头部元数据标签: [ar:...], [ti:...], [al:...], [by:...], [offset:...]
        if (lineStr.rfind("[ti:", 0) == 0 && lineStr.back() == ']') {
            outLyrics.title = Utf8ToWide(lineStr.substr(4, lineStr.size() - 5));
            continue;
        }
        if (lineStr.rfind("[ar:", 0) == 0 && lineStr.back() == ']') {
            outLyrics.artist = Utf8ToWide(lineStr.substr(4, lineStr.size() - 5));
            continue;
        }
        if (lineStr.rfind("[al:", 0) == 0 && lineStr.back() == ']') {
            outLyrics.album = Utf8ToWide(lineStr.substr(4, lineStr.size() - 5));
            continue;
        }
        if (lineStr.rfind("[by:", 0) == 0 && lineStr.back() == ']') {
            outLyrics.by = Utf8ToWide(lineStr.substr(4, lineStr.size() - 5));
            continue;
        }
        if (lineStr.rfind("[offset:", 0) == 0 && lineStr.back() == ']') {
            try {
                outLyrics.offset_ms = std::stoi(lineStr.substr(8, lineStr.size() - 9));
            } catch (...) {}
            continue;
        }

        // 解析时间轴行: [start_ms, duration_ms]<offset1, dur1, 0>word1<offset2, dur2, 0>word2...
        if (lineStr.front() == '[') {
            size_t closeBracket = lineStr.find(']');
            if (closeBracket == std::string::npos) continue;

            std::string timeSeg = lineStr.substr(1, closeBracket - 1);
            size_t comma = timeSeg.find(',');
            if (comma == std::string::npos) continue;

            uint32_t lineStart = 0;
            uint32_t lineDur = 0;
            try {
                lineStart = static_cast<uint32_t>(std::stoul(timeSeg.substr(0, comma)));
                lineDur = static_cast<uint32_t>(std::stoul(timeSeg.substr(comma + 1)));
            } catch (...) {
                continue;
            }

            LyricLine line;
            line.start_ms = lineStart;
            line.duration_ms = lineDur;

            std::string body = lineStr.substr(closeBracket + 1);
            size_t pos = 0;

            while (pos < body.size()) {
                if (body[pos] == '<') {
                    size_t endAngle = body.find('>', pos);
                    if (endAngle == std::string::npos) break;

                    std::string sylTime = body.substr(pos + 1, endAngle - pos - 1);
                    // 形如 "0,528,0" -> 偏移, 时长, 额外参数
                    uint32_t relStart = 0;
                    uint32_t sylDur = 0;
                    size_t c1 = sylTime.find(',');
                    if (c1 != std::string::npos) {
                        size_t c2 = sylTime.find(',', c1 + 1);
                        try {
                            relStart = static_cast<uint32_t>(std::stoul(sylTime.substr(0, c1)));
                            std::string durStr = (c2 != std::string::npos) ?
                                sylTime.substr(c1 + 1, c2 - c1 - 1) : sylTime.substr(c1 + 1);
                            sylDur = static_cast<uint32_t>(std::stoul(durStr));
                        } catch (...) {}
                    }

                    // 提取下一个 '<' 之前的文字
                    size_t nextAngle = body.find('<', endAngle + 1);
                    std::string wordUtf8;
                    if (nextAngle != std::string::npos) {
                        wordUtf8 = body.substr(endAngle + 1, nextAngle - endAngle - 1);
                        pos = nextAngle;
                    } else {
                        wordUtf8 = body.substr(endAngle + 1);
                        pos = body.size();
                    }

                    std::wstring wordW = Utf8ToWide(wordUtf8);
                    line.full_text += wordW;

                    LyricSyllable syl;
                    syl.text = wordW;
                    syl.start_ms = lineStart + relStart;
                    syl.duration_ms = sylDur;
                    line.syllables.push_back(syl);
                } else {
                    // 没有逐字标签的普通行内容
                    line.full_text += Utf8ToWide(body.substr(pos));
                    break;
                }
            }

            if (!line.full_text.empty() || line.duration_ms > 0) {
                outLyrics.lines.push_back(line);
            }
        }
    }
    // 二分定位依赖行按时间有序
    std::stable_sort(outLyrics.lines.begin(), outLyrics.lines.end(),
                     [](const LyricLine& a, const LyricLine& b) { return a.start_ms < b.start_ms; });
    return !outLyrics.lines.empty();
}

// ============================================================================
// LyricManager 门面与模糊匹配
// ============================================================================

LyricManager::LyricManager() {
    RefreshDirectories();
}

void LyricManager::RefreshDirectories() {
    auto dirs = KuGouPathFinder::DiscoverLyricDirectories();
    std::lock_guard<std::mutex> lock(mutex_);
    search_dirs_ = std::move(dirs);
    index_.clear();
    index_built_at_ = 0;
}

std::vector<std::wstring> LyricManager::GetLyricDirectories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return search_dirs_;
}

// 歌词库索引:每个 .krc 只解析一次文件名。查不到时最多每分钟重建一次,以便收录酷狗新下载的歌词
void LyricManager::EnsureIndexLocked(bool forceRebuild) {
    const int64_t now = NowSeconds();
    if (index_built_at_ != 0 && !(forceRebuild && now - index_built_at_ >= 60)) return;

    index_.clear();
    index_built_at_ = now;
    for (const auto& dir : search_dirs_) {
        platform::EnumerateFilesRecursive(dir, [this](const std::wstring&, const std::vector<platform::DirEntry>& files) {
            for (const auto& e : files) {
                const size_t dot = e.name.rfind(L'.');
                if (dot == std::wstring::npos) continue;
                std::wstring ext = e.name.substr(dot);
                for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
                if (ext != L".krc") continue;

                std::wstring artist, title;
                SplitArtistTitle(StripKrcHashSuffix(e.name.substr(0, dot)), artist, title);
                KrcEntry k;
                k.title = Normalize(title);
                k.artist = Normalize(artist);
                k.path = e.path;
                k.mtime = e.mtime;
                if (!k.title.empty()) index_.push_back(std::move(k));
            }
            return true;
        });
    }
}

std::wstring LyricManager::MatchLocked(const std::wstring& title, const std::wstring& artist) const {
    const std::wstring qTitle = Normalize(title);
    const std::wstring qArtist = Normalize(artist);
    if (qTitle.empty()) return {};

    const KrcEntry* best = nullptr;
    int bestScore = 0;
    for (const auto& e : index_) {
        if (e.title != qTitle) continue;
        int score = 1;
        if (!qArtist.empty() && !e.artist.empty()) {
            if (!ArtistsMatch(qArtist, e.artist)) continue; // 同名不同歌手:宁缺毋错
            score = 2;
        }
        // 同一首歌有多份歌词时取最新下载的那份
        if (score > bestScore || (score == bestScore && best && e.mtime > best->mtime)) {
            best = &e;
            bestScore = score;
        }
    }
    return best ? best->path : std::wstring();
}

std::wstring LyricManager::FindMatchingKrcFile(const std::wstring& audioFilePath, const std::wstring& title, const std::wstring& artist) {
    // 候选查询:标签 (歌名 + 歌手),然后是文件名「歌手 - 歌名」
    std::vector<std::pair<std::wstring, std::wstring>> queries;
    if (!title.empty()) queries.emplace_back(title, artist);
    if (!audioFilePath.empty()) {
        std::wstring stem = FileStem(audioFilePath);
        std::wstring fArtist, fTitle;
        SplitArtistTitle(stem, fArtist, fTitle);
        queries.emplace_back(fTitle, fArtist);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (int pass = 0; pass < 2; ++pass) {
        EnsureIndexLocked(pass == 1);
        for (const auto& q : queries) {
            std::wstring hit = MatchLocked(q.first, q.second);
            if (!hit.empty()) return hit;
        }
        if (NowSeconds() - index_built_at_ < 1) break; // 索引刚刚建立,无需再建一次
    }
    return {};
}

bool LyricManager::LoadLyricsForTrack(const std::wstring& audioFilePath, const std::wstring& title, const std::wstring& artist, ParsedLyrics& outLyrics) {
    const std::wstring krcPath = FindMatchingKrcFile(audioFilePath, title, artist);
    if (krcPath.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(krcPath);
        if (it != cache_.end()) {
            outLyrics = it->second;
            return true;
        }
    }

    ParsedLyrics parsed;
    if (!KrcDecoder::DecodeFile(krcPath, parsed)) return false;
    if (parsed.title.empty()) parsed.title = title;
    if (parsed.artist.empty()) parsed.artist = artist;

    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_.size() >= 64) cache_.clear(); // 简单上限,避免长时间播放后无限增长
    cache_[krcPath] = parsed;
    outLyrics = std::move(parsed);
    return true;
}

bool LyricManager::ExportLrcFile(const ParsedLyrics& lyrics, const std::wstring& targetLrcPath) {
    if (lyrics.IsEmpty() || targetLrcPath.empty()) return false;
    std::string lrcText = lyrics.ToStandardLrc();
    platform::WritableFile out;
    if (!out.Create(targetLrcPath)) return false;
    // 写入 UTF-8 BOM
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    const bool ok = out.Write(bom, sizeof(bom)) && out.Write(lrcText.data(), lrcText.size());
    out.Close();
    return ok;
}

} // namespace lyric_core
