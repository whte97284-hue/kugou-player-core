#include "../include/music_catalog.hpp"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

bool MusicCatalog::AddTrack(const std::wstring& filePath, const AudioMetadata& meta) {
    fs::path p(filePath);
    std::wstring fileName = p.filename().wstring();

    // 1. 元数据启发式清洗与补全
    CleanedSongInfo cleaned = SongCleaner::Clean(meta.title, meta.artist, meta.album, fileName);

    TrackItem track;
    track.id = next_id_++;
    track.file_path = filePath;
    track.file_name = fileName;
    track.title = cleaned.title;
    track.artist = cleaned.artist;
    track.album = cleaned.album;
    track.genre = meta.genre.empty() ? L"流行" : meta.genre;
    track.year = meta.year;
    track.format = meta.format.empty() ? p.extension().wstring() : meta.format;

    // 清理 format 中的 '.'
    if (!track.format.empty() && track.format[0] == L'.') {
        track.format = track.format.substr(1);
    }
    std::transform(track.format.begin(), track.format.end(), track.format.begin(), ::towupper);

    track.duration_ms = meta.duration_ms;
    track.bitrate_kbps = meta.bitrate_kbps;
    track.sample_rate = meta.sample_rate;
    track.channels = meta.channels;
    track.bits_per_sample = meta.bits_per_sample;

    track.is_lossless = (track.format == L"FLAC" || track.format == L"KGMA" || track.format == L"WAV" || track.bitrate_kbps >= 800);
    track.has_cover = meta.has_cover;
    track.cover_data = meta.cover_data;
    track.cover_mime = meta.cover_mime_type;

    // 2. 建立拼音索引 (全拼与首字母缩写)
    track.pinyin_title_full = PinyinHelper::GetFullPinyin(track.title);
    track.pinyin_title_abbr = PinyinHelper::GetPinyinAcronym(track.title);
    track.pinyin_artist_full = PinyinHelper::GetFullPinyin(track.artist);
    track.pinyin_artist_abbr = PinyinHelper::GetPinyinAcronym(track.artist);

    size_t index = tracks_.size();
    tracks_.push_back(track);

    // 3. 构建多维倒排索引
    artist_index_[track.artist].push_back(index);
    album_index_[track.album].push_back(index);
    format_index_[track.format].push_back(index);

    return true;
}

static int GetTrackQualityScore(const TrackItem* t) {
    if (!t) return 0;
    int score = 0;
    if (t->is_lossless) score += 10000;
    if (t->format == L"FLAC") score += 5000;
    else if (t->format == L"KGMA") score += 4000;
    else if (t->format == L"WAV") score += 3000;
    else if (t->format == L"OGG") score += 2000;
    else if (t->format == L"MP3") score += 1000;

    score += static_cast<int>(t->bitrate_kbps);
    if (t->has_cover) score += 100;
    return score;
}

std::vector<const TrackItem*> MusicCatalog::Search(const std::wstring& query, bool deduplicate) const {
    if (!deduplicate) {
        std::vector<const TrackItem*> results;
        if (query.empty()) {
            for (const auto& t : tracks_) results.push_back(&t);
            return results;
        }

        std::wstring lQuery = query;
        std::transform(lQuery.begin(), lQuery.end(), lQuery.begin(), ::towlower);
        std::string asciiQuery;
        for (wchar_t c : lQuery) {
            if (c < 128) asciiQuery += static_cast<char>(c);
        }

        for (const auto& t : tracks_) {
            bool match = false;
            if (PinyinHelper::Matches(t.title, lQuery) || 
                PinyinHelper::Matches(t.artist, lQuery) || 
                PinyinHelper::Matches(t.album, lQuery)) {
                match = true;
            }
            if (!match && !asciiQuery.empty()) {
                if (t.pinyin_title_abbr.find(asciiQuery) != std::string::npos ||
                    t.pinyin_title_full.find(asciiQuery) != std::string::npos ||
                    t.pinyin_artist_abbr.find(asciiQuery) != std::string::npos ||
                    t.pinyin_artist_full.find(asciiQuery) != std::string::npos) {
                    match = true;
                }
            }
            if (match) results.push_back(&t);
        }
        return results;
    }

    auto groups = SearchGrouped(query);
    std::vector<const TrackItem*> results;
    results.reserve(groups.size());
    for (const auto& g : groups) {
        if (g.primary) results.push_back(g.primary);
    }
    return results;
}

std::vector<TrackGroup> MusicCatalog::SearchGrouped(const std::wstring& query) const {
    auto rawHits = Search(query, false);
    std::vector<TrackGroup> groups;
    std::unordered_map<std::wstring, size_t> groupMap;

    for (const auto* t : rawHits) {
        // 归一化语义键 (歌手 + 歌名)
        std::wstring key = t->artist + L"|||" + t->title;
        std::transform(key.begin(), key.end(), key.begin(), ::towlower);

        auto it = groupMap.find(key);
        if (it == groupMap.end()) {
            size_t idx = groups.size();
            groupMap[key] = idx;
            TrackGroup g;
            g.primary = t;
            groups.push_back(g);
        } else {
            auto& g = groups[it->second];
            // 若当前文件音质更高，替换为 primary，旧的转入 duplicates
            if (GetTrackQualityScore(t) > GetTrackQualityScore(g.primary)) {
                g.duplicates.push_back(g.primary);
                g.primary = t;
            } else {
                g.duplicates.push_back(t);
            }
        }
    }
    return groups;
}

std::vector<const TrackItem*> MusicCatalog::FilterByArtist(const std::wstring& artist) const {
    std::vector<const TrackItem*> results;
    auto it = artist_index_.find(artist);
    if (it != artist_index_.end()) {
        for (size_t idx : it->second) {
            results.push_back(&tracks_[idx]);
        }
    }
    return results;
}

std::vector<const TrackItem*> MusicCatalog::FilterByAlbum(const std::wstring& album) const {
    std::vector<const TrackItem*> results;
    auto it = album_index_.find(album);
    if (it != album_index_.end()) {
        for (size_t idx : it->second) {
            results.push_back(&tracks_[idx]);
        }
    }
    return results;
}

std::vector<const TrackItem*> MusicCatalog::FilterByFormat(const std::wstring& format) const {
    std::vector<const TrackItem*> results;
    std::wstring upperFmt = format;
    std::transform(upperFmt.begin(), upperFmt.end(), upperFmt.begin(), ::towupper);

    auto it = format_index_.find(upperFmt);
    if (it != format_index_.end()) {
        for (size_t idx : it->second) {
            results.push_back(&tracks_[idx]);
        }
    }
    return results;
}

std::vector<std::wstring> MusicCatalog::GetAllArtists() const {
    std::vector<std::wstring> list;
    for (const auto& pair : artist_index_) {
        list.push_back(pair.first);
    }
    return list;
}

std::vector<std::wstring> MusicCatalog::GetAllAlbums() const {
    std::vector<std::wstring> list;
    for (const auto& pair : album_index_) {
        list.push_back(pair.first);
    }
    return list;
}

CatalogStats MusicCatalog::GetStats() const {
    CatalogStats s;
    s.total_tracks = tracks_.size();
    s.total_artists = artist_index_.size();
    s.total_albums = album_index_.size();
    for (const auto& t : tracks_) {
        s.total_duration_ms += t.duration_ms;
        if (t.is_lossless) s.lossless_count++;
        if (t.has_cover) s.with_cover_count++;
    }
    return s;
}

void MusicCatalog::Clear() {
    tracks_.clear();
    artist_index_.clear();
    album_index_.clear();
    format_index_.clear();
    next_id_ = 1;
}
