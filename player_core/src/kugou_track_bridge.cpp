#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/kugou_track_bridge.hpp"
#include "../include/platform_io.hpp"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <algorithm>
#include <cwctype>

namespace fs = std::filesystem;

namespace lyric_core {

namespace {

static std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::towlower);
    return out;
}

static std::wstring AutoFindMaskPath() {
    const std::wstring candidates[] = {
        L"tools\\kgm\\kgm.mask",
        L"..\\tools\\kgm\\kgm.mask",
        L"F:\\软件\\酷狗最新版转换器\\KugouConverterWeb\\tools\\kgm\\kgm.mask"
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) return p;
    }
    return L"tools\\kgm\\kgm.mask";
}

} // namespace

KuGouTrackBridge::KuGouTrackBridge() {
    mask_path_ = AutoFindMaskPath();
    cache_dir_ = L"output";
    if (fs::exists(mask_path_)) {
        DecryptorRegistry::Instance().SetKgmMaskPath(mask_path_);
    }
}

void KuGouTrackBridge::SetMaskPath(const std::wstring& maskPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    mask_path_ = maskPath;
    if (fs::exists(mask_path_)) {
        DecryptorRegistry::Instance().SetKgmMaskPath(mask_path_);
    }
}

void KuGouTrackBridge::SetCacheOutputDir(const std::wstring& cacheDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_dir_ = cacheDir;
    if (!fs::exists(cache_dir_)) {
        std::error_code ec;
        fs::create_directories(cache_dir_, ec);
    }
}

std::vector<std::wstring> KuGouTrackBridge::DiscoverKuGouMusicDirectories() {
    std::vector<std::wstring> candidates;

    // 1. 读取 %APPDATA%\KuGou8\KuGou.ini [DownloadConfigSection] -> DownloadPath
    wchar_t appData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        fs::path iniPath = fs::path(appData) / L"KuGou8" / L"KuGou.ini";
        if (fs::exists(iniPath)) {
            wchar_t buf[MAX_PATH] = {0};
            GetPrivateProfileStringW(L"DownloadConfigSection", L"DownloadPath", L"", buf, MAX_PATH, iniPath.c_str());
            if (buf[0] != L'\0' && fs::exists(buf)) {
                candidates.push_back(buf);
                fs::path p(buf);
                if (fs::exists(p / L"KugouMusic")) candidates.push_back((p / L"KugouMusic").wstring());
                if (fs::exists(p / L"GEDAN" / L"KugouMusic")) candidates.push_back((p / L"GEDAN" / L"KugouMusic").wstring());
                if (fs::exists(p / L"GEDAN")) candidates.push_back((p / L"GEDAN").wstring());
            }
        }
    }

    // 2. 遍历所有固定驱动器根目录
    wchar_t drives[512] = {0};
    DWORD len = GetLogicalDriveStringsW(512, drives);
    if (len > 0) {
        wchar_t* p = drives;
        while (*p) {
            UINT driveType = GetDriveTypeW(p);
            if (driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE) {
                fs::path root(p);
                fs::path kg = root / L"KuGou";
                if (fs::exists(kg)) {
                    candidates.push_back(kg.wstring());
                    if (fs::exists(kg / L"KugouMusic")) candidates.push_back((kg / L"KugouMusic").wstring());
                    if (fs::exists(kg / L"GEDAN" / L"KugouMusic")) candidates.push_back((kg / L"GEDAN" / L"KugouMusic").wstring());
                }
            }
            p += wcslen(p) + 1;
        }
    }

    // 去重规范化
    std::vector<std::wstring> uniqueDirs;
    for (const auto& d : candidates) {
        try {
            fs::path canon = fs::canonical(d);
            std::wstring s = canon.wstring();
            if (std::find(uniqueDirs.begin(), uniqueDirs.end(), s) == uniqueDirs.end()) {
                uniqueDirs.push_back(s);
            }
        } catch (...) {
            if (std::find(uniqueDirs.begin(), uniqueDirs.end(), d) == uniqueDirs.end()) {
                uniqueDirs.push_back(d);
            }
        }
    }
    return uniqueDirs;
}

std::vector<KuGouTrackEntry> KuGouTrackBridge::ScanKuGouLibrary(const std::wstring& customDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::wstring> dirs;
    if (!customDir.empty() && fs::exists(customDir)) {
        dirs.push_back(customDir);
    } else {
        dirs = DiscoverKuGouMusicDirectories();
    }

    std::vector<KuGouTrackEntry> entries;
    const std::wstring validExts[] = {
        L".kgma", L".kgm", L".kgg", L".flac", L".mp3", L".ogg", L".wav", L".m4a"
    };

    for (const auto& d : dirs) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(d, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;

            std::wstring ext = ToLower(entry.path().extension().wstring());
            bool match = false;
            for (const auto& ve : validExts) {
                if (ext == ve) { match = true; break; }
            }
            if (!match) continue;

            KuGouTrackEntry te;
            te.source_path = entry.path().wstring();
            te.file_name = entry.path().filename().wstring();
            te.ext = ext;
            te.file_size = entry.file_size(ec);
            te.is_encrypted = (ext == L".kgma" || ext == L".kgm" || ext == L".kgg");

            // 自动模糊匹配同名或相配的酷狗歌词
            te.matched_krc_path = lyric_manager_.FindMatchingKrcFile(te.source_path, L"", L"");

            // 若本身是非加密音频，则自身即为可播路径
            if (!te.is_encrypted) {
                te.playable_path = te.source_path;
                te.is_ready_to_play = true;
                ParseOptions po;
                po.load_cover = false;
                AudioTagParser::ParseFile(te.playable_path, te.metadata, po);
            } else {
                // 检查缓存中是否已有现成产物
                std::wstring stem = entry.path().stem().wstring();
                for (const auto& candExt : {L".flac", L".mp3", L".ogg", L".wav"}) {
                    fs::path cand = fs::path(cache_dir_) / (stem + candExt);
                    if (fs::exists(cand)) {
                        te.playable_path = cand.wstring();
                        te.is_ready_to_play = true;
                        ParseOptions po;
                        po.load_cover = false;
                        AudioTagParser::ParseFile(te.playable_path, te.metadata, po);
                        break;
                    }
                }
            }
            entries.push_back(std::move(te));
        }
    }
    return entries;
}

bool KuGouTrackBridge::PreparePlayableTrack(const std::wstring& sourcePath, KuGouTrackEntry& outEntry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fs::exists(sourcePath)) return false;

    fs::path sp(sourcePath);
    outEntry.source_path = sourcePath;
    outEntry.file_name = sp.filename().wstring();
    outEntry.ext = ToLower(sp.extension().wstring());
    std::error_code ec;
    outEntry.file_size = fs::file_size(sp, ec);
    outEntry.is_encrypted = (outEntry.ext == L".kgma" || outEntry.ext == L".kgm" || outEntry.ext == L".kgg");

    // 匹配歌词
    outEntry.matched_krc_path = lyric_manager_.FindMatchingKrcFile(sourcePath, L"", L"");

    if (!outEntry.is_encrypted) {
        outEntry.playable_path = sourcePath;
        outEntry.is_ready_to_play = true;
        ParseOptions po;
        po.load_cover = false;
        return AudioTagParser::ParseFile(outEntry.playable_path, outEntry.metadata, po);
    }

    // 检查缓存中是否已有可播产物
    std::wstring stem = sp.stem().wstring();
    for (const auto& candExt : {L".flac", L".mp3", L".ogg", L".wav"}) {
        fs::path cand = fs::path(cache_dir_) / (stem + candExt);
        if (fs::exists(cand) && fs::file_size(cand, ec) > 0) {
            outEntry.playable_path = cand.wstring();
            outEntry.is_ready_to_play = true;
            ParseOptions po;
            po.load_cover = false;
            return AudioTagParser::ParseFile(outEntry.playable_path, outEntry.metadata, po);
        }
    }

    // 现场触发 40ms 级极速解码
    auto dec = DecryptorRegistry::Instance().ByExtension(outEntry.ext);
    if (!dec) return false;

    if (!fs::exists(cache_dir_)) {
        fs::create_directories(cache_dir_, ec);
    }

    DecryptOptions opt;
    opt.output_dir = cache_dir_;
    DecryptResult dr = dec->Decrypt(sourcePath, opt);
    if (!dr.ok || dr.output.empty() || !fs::exists(dr.output)) {
        return false;
    }

    outEntry.playable_path = dr.output;
    outEntry.is_ready_to_play = true;
    ParseOptions po;
    po.load_cover = false;
    AudioTagParser::ParseFile(outEntry.playable_path, outEntry.metadata, po);
    return true;
}

size_t KuGouTrackBridge::BatchPrepare(const std::vector<std::wstring>& sourcePaths,
                                     std::function<void(size_t done, size_t total, const std::wstring& current)> onProgress) {
    size_t successCount = 0;
    size_t total = sourcePaths.size();
    for (size_t i = 0; i < total; ++i) {
        KuGouTrackEntry entry;
        bool ok = PreparePlayableTrack(sourcePaths[i], entry);
        if (ok) ++successCount;
        if (onProgress) {
            onProgress(i + 1, total, sourcePaths[i]);
        }
    }
    return successCount;
}

} // namespace lyric_core
