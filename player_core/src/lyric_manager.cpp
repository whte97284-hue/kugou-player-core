#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/lyric_manager.hpp"
#include "../include/tinf.hpp"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
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

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int req = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
    if (req <= 0) return L"";
    std::wstring wstr(req, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], req);
    return wstr;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int req = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    if (req <= 0) return "";
    std::string str(req, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &str[0], req, NULL, NULL);
    return str;
}

static std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::towlower);
    return out;
}

static void CleanDecorations(std::wstring& s) {
    // 移除常见的酷狗/转码装饰词
    const std::wstring toRemove[] = {
        L"_kgg-dec", L"_kgm-dec", L"(1)", L"(2)", L"[hires]", L"[hi-res]",
        L"(车载cd制作、自选歌曲、盘面印图)", L"(flac)", L"(mp3)"
    };
    std::wstring lower = ToLower(s);
    for (const auto& pat : toRemove) {
        size_t pos = 0;
        while ((pos = lower.find(pat, pos)) != std::wstring::npos) {
            s.erase(pos, pat.size());
            lower.erase(pos, pat.size());
        }
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
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
    if (lines.empty() || seconds < 0) return -1;
    uint32_t targetMs = static_cast<uint32_t>(seconds * 1000.0);

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
    if (line.syllables.empty()) return -1;

    uint32_t targetMs = static_cast<uint32_t>(seconds * 1000.0);
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
    if (!dir.empty() && fs::exists(dir)) {
        custom_dirs_.push_back(dir);
    }
}

bool KuGouPathFinder::ProbeFromKuGouIni(std::vector<std::wstring>& outDirs) {
    // 优先读取 %APPDATA%\KuGou8\KuGou.ini
    wchar_t appData[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) return false;

    fs::path iniPath = fs::path(appData) / L"KuGou8" / L"KuGou.ini";
    if (!fs::exists(iniPath)) return false;

    wchar_t buf[MAX_PATH] = {0};
    // 1. 读取 [LyricConfigSection] -> LyricPath
    GetPrivateProfileStringW(L"LyricConfigSection", L"LyricPath", L"", buf, MAX_PATH, iniPath.c_str());
    if (buf[0] != L'\0' && fs::exists(buf)) {
        outDirs.push_back(buf);
    }

    // 2. 读取 [DownloadConfigSection] -> DownloadPath 下的 Lyric 子目录
    buf[0] = L'\0';
    GetPrivateProfileStringW(L"DownloadConfigSection", L"DownloadPath", L"", buf, MAX_PATH, iniPath.c_str());
    if (buf[0] != L'\0') {
        fs::path p(buf);
        fs::path lyr = p / L"Lyric";
        if (fs::exists(lyr)) outDirs.push_back(lyr.wstring());
    }
    return !outDirs.empty();
}

bool KuGouPathFinder::ProbeFromRegistry(std::vector<std::wstring>& outDirs) {
    // 检查注册表 HKCU\Software\KuGou
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\KuGou\\Prefetcher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t pathBuf[MAX_PATH] = {0};
        DWORD bytes = sizeof(pathBuf);
        if (RegQueryValueExW(hKey, L"LastDetectedPath", NULL, NULL, reinterpret_cast<LPBYTE>(pathBuf), &bytes) == ERROR_SUCCESS) {
            fs::path exePath(pathBuf);
            if (fs::exists(exePath)) {
                // KGMusic 根目录
                fs::path parent = exePath.parent_path().parent_path();
                fs::path lyr = parent / L"Lyric";
                if (fs::exists(lyr)) outDirs.push_back(lyr.wstring());
            }
        }
        RegCloseKey(hKey);
    }
    return !outDirs.empty();
}

bool KuGouPathFinder::ProbeFromLogicalDrives(std::vector<std::wstring>& outDirs) {
    // 遍历所有固定驱动器根目录如 C:\KuGou\Lyric, D:\KuGou\Lyric, F:\KuGou\Lyric 等
    wchar_t drives[512] = {0};
    DWORD len = GetLogicalDriveStringsW(512, drives);
    if (len == 0) return false;

    wchar_t* p = drives;
    while (*p) {
        UINT driveType = GetDriveTypeW(p);
        if (driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE) {
            fs::path root(p);
            fs::path candidate = root / L"KuGou" / L"Lyric";
            if (fs::exists(candidate)) {
                outDirs.push_back(candidate.wstring());
            }
            fs::path candidate2 = root / L"KuGou";
            if (fs::exists(candidate2) && fs::exists(candidate2 / L"Lyric")) {
                outDirs.push_back((candidate2 / L"Lyric").wstring());
            }
        }
        p += wcslen(p) + 1;
    }
    return true;
}

bool KuGouPathFinder::ProbeFromSpecialFolders(std::vector<std::wstring>& outDirs) {
    // 检查标准系统音乐目录 %USERPROFILE%\Music\KuGou\Lyric
    wchar_t musicPath[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYMUSIC, NULL, 0, musicPath))) {
        fs::path p = fs::path(musicPath) / L"KuGou" / L"Lyric";
        if (fs::exists(p)) outDirs.push_back(p.wstring());
    }
    return true;
}

std::vector<std::wstring> KuGouPathFinder::DiscoverLyricDirectories() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::wstring> results;

    // 1. 用户自定义目录
    for (const auto& d : custom_dirs_) {
        if (fs::exists(d)) results.push_back(d);
    }

    // 2. INI 配置 (最准)
    ProbeFromKuGouIni(results);

    // 3. 注册表
    ProbeFromRegistry(results);

    // 4. 全局逻辑驱动器
    ProbeFromLogicalDrives(results);

    // 5. 用户媒体夹
    ProbeFromSpecialFolders(results);

    // 去重与规范化
    std::vector<std::wstring> uniqueDirs;
    for (auto& dir : results) {
        try {
            fs::path canon = fs::canonical(dir);
            std::wstring canonStr = canon.wstring();
            if (std::find(uniqueDirs.begin(), uniqueDirs.end(), canonStr) == uniqueDirs.end()) {
                uniqueDirs.push_back(canonStr);
            }
        } catch (...) {
            if (std::find(uniqueDirs.begin(), uniqueDirs.end(), dir) == uniqueDirs.end()) {
                uniqueDirs.push_back(dir);
            }
        }
    }
    return uniqueDirs;
}

// ============================================================================
// KrcDecoder 解密与逐字解析
// ============================================================================

bool KrcDecoder::DecodeFile(const std::wstring& krcFilePath, ParsedLyrics& outLyrics) {
    HANDLE hFile = CreateFileW(krcFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fSize;
    GetFileSizeEx(hFile, &fSize);
    if (fSize.QuadPart <= 4 || fSize.QuadPart > 10 * 1024 * 1024) { // 限制在 10MB 内
        CloseHandle(hFile);
        return false;
    }

    DWORD toRead = static_cast<DWORD>(fSize.QuadPart);
    std::vector<uint8_t> buffer(toRead);
    DWORD bytesRead = 0;
    ReadFile(hFile, buffer.data(), toRead, &bytesRead, NULL);
    CloseHandle(hFile);

    if (bytesRead != toRead) return false;
    return DecodeMemory(buffer.data(), bytesRead, outLyrics);
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
    return !outLyrics.lines.empty();
}

// ============================================================================
// LyricManager 门面与模糊匹配
// ============================================================================

LyricManager::LyricManager() {
    RefreshDirectories();
}

void LyricManager::RefreshDirectories() {
    std::lock_guard<std::mutex> lock(mutex_);
    search_dirs_ = KuGouPathFinder::DiscoverLyricDirectories();
}

std::vector<std::wstring> LyricManager::GetLyricDirectories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return search_dirs_;
}

std::wstring LyricManager::FindMatchingKrcFile(const std::wstring& audioFilePath, const std::wstring& title, const std::wstring& artist) {
    std::wstring qTitle = title;
    std::wstring qArtist = artist;

    if (qTitle.empty() && !audioFilePath.empty()) {
        fs::path p(audioFilePath);
        std::wstring stem = p.stem().wstring();
        CleanDecorations(stem);
        size_t dash = stem.find(L'-');
        if (dash != std::wstring::npos) {
            qArtist = stem.substr(0, dash);
            qTitle = stem.substr(dash + 1);
        } else {
            qTitle = stem;
        }
    }

    CleanDecorations(qTitle);
    CleanDecorations(qArtist);

    std::wstring lowTitle = ToLower(qTitle);
    std::wstring lowArtist = ToLower(qArtist);

    std::wstring bestMatch;
    int bestScore = 0;

    for (const auto& dirPath : search_dirs_) {
        if (!fs::exists(dirPath)) continue;

        try {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext != L".krc") continue;

                std::wstring fn = ToLower(entry.path().filename().wstring());
                int score = 0;

                // 酷狗命名规则: [歌手] - [歌名]-哈希-ID.krc
                bool matchTitle = (!lowTitle.empty() && fn.find(lowTitle) != std::wstring::npos);
                bool matchArtist = (!lowArtist.empty() && fn.find(lowArtist) != std::wstring::npos);

                if (matchTitle && matchArtist) score = 100;
                else if (matchTitle) score = 60;
                else if (matchArtist) score = 20;

                if (score > bestScore) {
                    bestScore = score;
                    bestMatch = entry.path().wstring();
                    if (bestScore == 100) break; // 最佳匹配
                }
            }
        } catch (...) {}
        if (bestScore == 100) break;
    }
    return bestMatch;
}

bool LyricManager::LoadLyricsForTrack(const std::wstring& audioFilePath, const std::wstring& title, const std::wstring& artist, ParsedLyrics& outLyrics) {
    std::wstring key = ToLower(artist + L"_" + title + L"_" + audioFilePath);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            outLyrics = it->second;
            return true;
        }
    }

    std::wstring krcPath = FindMatchingKrcFile(audioFilePath, title, artist);
    if (krcPath.empty()) return false;

    if (KrcDecoder::DecodeFile(krcPath, outLyrics)) {
        if (outLyrics.title.empty()) outLyrics.title = title;
        if (outLyrics.artist.empty()) outLyrics.artist = artist;

        std::lock_guard<std::mutex> lock(mutex_);
        cache_[key] = outLyrics;
        return true;
    }
    return false;
}

bool LyricManager::ExportLrcFile(const ParsedLyrics& lyrics, const std::wstring& targetLrcPath) {
    if (lyrics.IsEmpty() || targetLrcPath.empty()) return false;
    std::string lrcText = lyrics.ToStandardLrc();
    std::ofstream ofs(targetLrcPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    // 写入 UTF-8 BOM
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    ofs.write(reinterpret_cast<const char*>(bom), 3);
    ofs.write(lrcText.data(), lrcText.size());
    return true;
}

} // namespace lyric_core
