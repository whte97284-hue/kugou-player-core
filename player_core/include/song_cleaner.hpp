#pragma once
#include <string>
#include <vector>
#include <regex>

struct CleanedSongInfo {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::vector<std::wstring> artists; // 分割后的多歌手列表
    bool was_cleaned = false;
};

class SongCleaner {
public:
    // 清洗歌曲信息，当标签缺失时回退提取文件名
    static CleanedSongInfo Clean(const std::wstring& rawTitle, 
                                 const std::wstring& rawArtist, 
                                 const std::wstring& rawAlbum, 
                                 const std::wstring& fileName);

    // 去除标题中的垃圾标记 (如 [FLAC], (320k), 【无损】, _kgg-dec 等)
    static std::wstring CleanTitle(const std::wstring& title);

    // 标准化艺术家名称并拆分多歌手
    static std::vector<std::wstring> SplitArtists(const std::wstring& artist);

    // 从文件名猜测歌手与歌名 (当元数据为空时)
    static bool GuessFromFileName(const std::wstring& fileName, std::wstring& outArtist, std::wstring& outTitle);

    // 去除首尾空格与全角/半角标点噪声
    static std::wstring Trim(const std::wstring& str);
};
