#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "audio_tag_parser.hpp"
#include "song_cleaner.hpp"
#include "pinyin_table.hpp"

struct TrackItem {
    uint64_t id = 0;
    std::wstring file_path;
    std::wstring file_name;

    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring genre;
    std::wstring year;
    std::wstring format; // "FLAC", "MP3", "NCM", "KGMA", etc.

    uint32_t duration_ms = 0;
    uint32_t bitrate_kbps = 0;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;

    bool is_lossless = false;
    bool has_cover = false;
    std::vector<uint8_t> cover_data; // 封面缓存
    std::string cover_mime;

    // 拼音索引缓存
    std::string pinyin_title_full;
    std::string pinyin_title_abbr;
    std::string pinyin_artist_full;
    std::string pinyin_artist_abbr;
};

struct TrackGroup {
    const TrackItem* primary = nullptr;
    std::vector<const TrackItem*> duplicates;
};

struct CatalogStats {
    size_t total_tracks = 0;
    size_t total_artists = 0;
    size_t total_albums = 0;
    uint64_t total_duration_ms = 0;
    size_t lossless_count = 0;
    size_t with_cover_count = 0;
};

class MusicCatalog {
public:
    MusicCatalog() = default;

    // 添加单首歌曲
    bool AddTrack(const std::wstring& filePath, const AudioMetadata& meta);

    // 智能全局检索 (deduplicate=true 时按歌手+歌名折叠同名多副本，优选高音质版本)
    std::vector<const TrackItem*> Search(const std::wstring& query, bool deduplicate = false) const;

    // 智能分组检索 (返回去重独立曲目与其名下所有物理实体副本)
    std::vector<TrackGroup> SearchGrouped(const std::wstring& query) const;

    // 按歌手筛选
    std::vector<const TrackItem*> FilterByArtist(const std::wstring& artist) const;

    // 按专辑筛选
    std::vector<const TrackItem*> FilterByAlbum(const std::wstring& album) const;

    // 按格式筛选 (如 "FLAC", "MP3", "KGMA", "KGG", "NCM")
    std::vector<const TrackItem*> FilterByFormat(const std::wstring& format) const;

    // 获取所有歌手列表
    std::vector<std::wstring> GetAllArtists() const;

    // 获取所有专辑列表
    std::vector<std::wstring> GetAllAlbums() const;

    // 获取曲库全部歌曲
    const std::vector<TrackItem>& GetAllTracks() const { return tracks_; }

    // 获取曲库统计信息
    CatalogStats GetStats() const;

    // 清空曲库
    void Clear();

private:
    std::vector<TrackItem> tracks_;
    std::unordered_map<std::wstring, std::vector<size_t>> artist_index_;
    std::unordered_map<std::wstring, std::vector<size_t>> album_index_;
    std::unordered_map<std::wstring, std::vector<size_t>> format_index_;
    uint64_t next_id_ = 1;
};
