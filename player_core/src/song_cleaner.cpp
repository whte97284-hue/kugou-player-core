#include "../include/song_cleaner.hpp"
#include <cwctype>
#include <algorithm>

std::wstring SongCleaner::Trim(const std::wstring& str) {
    if (str.empty()) return L"";
    size_t first = 0;
    while (first < str.size() && (std::iswspace(str[first]) || str[first] == L'\t' || str[first] == L'\r' || str[first] == L'\n')) {
        first++;
    }
    if (first == str.size()) return L"";
    size_t last = str.size() - 1;
    while (last > first && (std::iswspace(str[last]) || str[last] == L'\t' || str[last] == L'\r' || str[last] == L'\n')) {
        last--;
    }
    return str.substr(first, last - first + 1);
}

std::wstring SongCleaner::CleanTitle(const std::wstring& title) {
    std::wstring t = Trim(title);
    if (t.empty()) return L"";

    // 1. 去除常见的后缀噪点 (如 _kgg-dec, _ncmdump)
    const std::vector<std::wstring> junkSubstrings = {
        L"_kgg-dec", L"_ncmdump", L"[FLAC]", L"[flac]", L"(FLAC)", L"(flac)",
        L"[320K]", L"[320k]", L"(320K)", L"(320k)", L"【无损】", L"【高品质】",
        L"[Hi-Res]", L"(Hi-Res)", L"【官方版】", L"(单曲版)"
    };

    for (const auto& junk : junkSubstrings) {
        size_t pos = 0;
        while ((pos = t.find(junk, pos)) != std::wstring::npos) {
            t.erase(pos, junk.length());
        }
    }

    // 2. 去除前导序号 (如 "01. ", "02 - ", "1 ")
    size_t i = 0;
    while (i < t.size() && std::iswdigit(t[i])) {
        i++;
    }
    if (i > 0 && i < t.size() && (t[i] == L'.' || t[i] == L'-' || t[i] == L' ' || t[i] == L'_')) {
        i++;
        while (i < t.size() && std::iswspace(t[i])) i++;
        t = t.substr(i);
    }

    return Trim(t);
}

std::vector<std::wstring> SongCleaner::SplitArtists(const std::wstring& artist) {
    std::vector<std::wstring> result;
    std::wstring cleaned = Trim(artist);
    if (cleaned.empty()) return { L"未知艺术家" };

    // 拆分分隔符: '、', '/', '&', ',', '，'
    std::wstring cur;
    for (size_t i = 0; i < cleaned.size(); ++i) {
        wchar_t c = cleaned[i];
        if (c == L'、' || c == L'/' || c == L'&' || c == L',' || c == L'，') {
            std::wstring t = Trim(cur);
            if (!t.empty()) result.push_back(t);
            cur.clear();
        } else {
            cur += c;
        }
    }
    std::wstring last = Trim(cur);
    if (!last.empty()) result.push_back(last);

    if (result.empty()) result.push_back(L"未知艺术家");
    return result;
}

bool SongCleaner::GuessFromFileName(const std::wstring& fileName, std::wstring& outArtist, std::wstring& outTitle) {
    std::wstring name = fileName;
    // 去除后缀
    size_t dotPos = name.rfind(L'.');
    if (dotPos != std::wstring::npos) {
        name = name.substr(0, dotPos);
    }

    // 常见命名规范: "歌手 - 歌名"
    size_t sepPos = name.find(L" - ");
    if (sepPos == std::wstring::npos) {
        sepPos = name.find(L"－");
    }
    if (sepPos == std::wstring::npos) {
        sepPos = name.find(L"_");
    }

    if (sepPos != std::wstring::npos) {
        outArtist = Trim(name.substr(0, sepPos));
        outTitle = Trim(name.substr(sepPos + (name.find(L" - ") == sepPos ? 3 : 1)));
        return true;
    }

    outArtist = L"未知艺术家";
    outTitle = Trim(name);
    return false;
}

CleanedSongInfo SongCleaner::Clean(const std::wstring& rawTitle, 
                                   const std::wstring& rawArtist, 
                                   const std::wstring& rawAlbum, 
                                   const std::wstring& fileName) {
    CleanedSongInfo info;
    std::wstring t = CleanTitle(rawTitle);
    std::wstring a = Trim(rawArtist);
    std::wstring al = Trim(rawAlbum);

    // 如果标题或艺术家缺失，从文件名自动启发式补全
    if (t.empty() || a.empty() || a == L"未知艺术家") {
        std::wstring gArtist, gTitle;
        if (GuessFromFileName(fileName, gArtist, gTitle)) {
            if (t.empty()) t = CleanTitle(gTitle);
            if (a.empty()) a = gArtist;
            info.was_cleaned = true;
        }
    }

    if (t.empty()) t = CleanTitle(fileName);
    if (a.empty()) a = L"未知艺术家";
    if (al.empty()) al = L"未知专辑";

    info.title = t;
    info.artist = a;
    info.album = al;
    info.artists = SplitArtists(a);
    return info;
}
