#include "../include/music_catalog.hpp"
#include "../include/platform_io.hpp"
#include <algorithm>
#include <cwctype>

namespace {

std::wstring Lower(const std::wstring& s) {
    std::wstring r = s;
    for (auto& ch : r) ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    return r;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring ExtOf(const std::wstring& name) {
    size_t d = name.rfind(L'.');
    return d == std::wstring::npos ? L"" : name.substr(d);
}

int ParseLeadingInt(const std::wstring& s) {
    int v = 0;
    size_t i = 0;
    while (i < s.size() && std::iswspace(static_cast<wint_t>(s[i]))) ++i;
    for (; i < s.size() && s[i] >= L'0' && s[i] <= L'9' && v < 100000; ++i) v = v * 10 + (s[i] - L'0');
    return v;
}

} // namespace

TrackItem MusicCatalog::BuildTrack(const std::wstring& filePath, const AudioMetadata& meta) {
    std::wstring fileName = FileNameOf(filePath);

    // 1. 元数据启发式清洗与补全
    CleanedSongInfo cleaned = SongCleaner::Clean(meta.title, meta.artist, meta.album, fileName);

    TrackItem track;
    track.file_path = filePath;
    track.file_name = fileName;
    track.title = cleaned.title;
    track.artist = cleaned.artist;
    track.album = cleaned.album;
    track.album_artist = meta.album_artist;
    track.genre = meta.genre.empty() ? L"流行" : meta.genre;
    track.year = meta.year;
    track.format = meta.format.empty() ? ExtOf(fileName) : meta.format;
    if (!track.format.empty() && track.format[0] == L'.') track.format = track.format.substr(1);
    for (auto& ch : track.format) ch = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(ch)));
    track.codec = meta.codec.empty() ? track.format : meta.codec;
    track.track_no = ParseLeadingInt(meta.track_number);
    track.disc_no = ParseLeadingInt(meta.disc_number);

    track.duration_ms = meta.duration_ms;
    track.bitrate_kbps = meta.bitrate_kbps;
    track.sample_rate = meta.sample_rate;
    track.channels = meta.channels;
    track.bits_per_sample = meta.bits_per_sample;
    track.file_size = meta.file_size;

    bool dsd = track.codec.compare(0, 3, L"DSD") == 0 || track.codec == L"DST";
    track.is_lossless = meta.is_lossless || track.format == L"FLAC" || track.format == L"KGMA" ||
                        track.format == L"WAV" || dsd || (track.bitrate_kbps >= 800 && track.format != L"MP3");
    track.is_hires = dsd || (track.is_lossless && (track.sample_rate > 48000 || track.bits_per_sample > 16));

    track.has_cover = meta.has_cover;
    track.cover_data = meta.cover_data;
    track.cover_mime = meta.cover_mime_type;
    track.cover_offset = meta.cover_offset;
    track.cover_size = meta.cover_size;

    // 2. 建立拼音索引 (全拼 / 首字母 / 音节键) 与小写检索字段
    track.pinyin_title_full = PinyinHelper::GetFullPinyin(track.title);
    track.pinyin_title_abbr = PinyinHelper::GetPinyinAcronym(track.title);
    track.pinyin_artist_full = PinyinHelper::GetFullPinyin(track.artist);
    track.pinyin_artist_abbr = PinyinHelper::GetPinyinAcronym(track.artist);
    track.pinyin_album_full = PinyinHelper::GetFullPinyin(track.album);
    track.pinyin_album_abbr = PinyinHelper::GetPinyinAcronym(track.album);
    std::string key = PinyinHelper::GetPinyinKey(track.title + L" " + track.artist + L" " + track.album);
    for (auto& ch : key) if (ch == ' ') ch = '\'';
    track.pinyin_key = "'" + key + "'";
    track.search_title = Lower(track.title);
    track.search_artist = Lower(track.artist);
    track.search_album = Lower(track.album);
    return track;
}

void MusicCatalog::AddPreparedTracks(std::vector<TrackItem>&& batch) {
    tracks_.reserve(tracks_.size() + batch.size());
    for (auto& t : batch) {
        t.id = next_id_++;
        size_t index = tracks_.size();
        artist_index_[t.artist].push_back(index);
        album_index_[t.album].push_back(index);
        format_index_[t.format].push_back(index);
        id_index_[t.id] = index;
        path_index_.emplace(t.file_path, index);
        tracks_.push_back(std::move(t));
    }
    batch.clear();
}

bool MusicCatalog::AddTrack(const std::wstring& filePath, const AudioMetadata& meta) {
    std::vector<TrackItem> one;
    one.push_back(BuildTrack(filePath, meta));
    AddPreparedTracks(std::move(one));
    return true;
}

static int GetTrackQualityScore(const TrackItem* t) {
    if (!t) return 0;
    int score = 0;
    if (t->is_lossless) score += 100000;
    if (t->is_hires) score += 20000;
    score += static_cast<int>(std::min<uint32_t>(t->sample_rate, 400000) / 100); // 192k -> 1920
    score += t->bits_per_sample * 50;
    score += static_cast<int>(std::min<uint32_t>(t->bitrate_kbps, 20000)) / (t->is_lossless ? 10 : 1);
    if (t->has_cover || !t->external_cover_path.empty()) score += 10;
    return score;
}

std::vector<const TrackItem*> MusicCatalog::Search(const std::wstring& query, bool deduplicate) const {
    if (deduplicate) {
        auto groups = SearchGrouped(query);
        std::vector<const TrackItem*> results;
        results.reserve(groups.size());
        for (const auto& g : groups)
            if (g.primary) results.push_back(g.primary);
        return results;
    }

    std::vector<const TrackItem*> results;
    if (query.empty()) {
        results.reserve(tracks_.size());
        for (const auto& t : tracks_) results.push_back(&t);
        return results;
    }

    const std::wstring q = Lower(query);
    std::string qa;
    for (wchar_t c : q)
        if (c < 128 && std::iswalnum(static_cast<wint_t>(c))) qa += static_cast<char>(c);
    // 查询中仅含 ASCII 时才走拼音匹配,中文查询只在含 2 个以上音节时做同音/简繁匹配
    bool asciiQuery = !qa.empty() && std::all_of(q.begin(), q.end(), [](wchar_t c) { return c < 128; });
    std::string qkey;
    if (PinyinHelper::HasPhonetic(q)) {
        std::string k = PinyinHelper::GetPinyinKey(q);
        if (k.find('\'') != std::string::npos) {
            for (auto& ch : k) if (ch == ' ') ch = '\'';
            qkey = "'" + k + "'";
        }
    }

    std::vector<std::pair<int, const TrackItem*>> scored;
    for (const auto& t : tracks_) {
        int s = 0;
        if (t.search_title == q) s = 1000;
        else if (t.search_title.compare(0, q.size(), q) == 0) s = 800;
        else if (t.search_title.find(q) != std::wstring::npos) s = 600;
        else if (t.search_artist == q) s = 500;
        else if (t.search_artist.find(q) != std::wstring::npos) s = 400;
        else if (t.search_album.find(q) != std::wstring::npos) s = 300;
        else if (asciiQuery) {
            if (t.pinyin_title_abbr.find(qa) != std::string::npos || t.pinyin_title_full.find(qa) != std::string::npos) s = 200;
            else if (t.pinyin_artist_abbr.find(qa) != std::string::npos || t.pinyin_artist_full.find(qa) != std::string::npos) s = 150;
            else if (t.pinyin_album_abbr.find(qa) != std::string::npos || t.pinyin_album_full.find(qa) != std::string::npos) s = 100;
        }
        if (s == 0 && !qkey.empty() && t.pinyin_key.find(qkey) != std::string::npos) s = 120;
        if (s > 0) scored.emplace_back(s, &t);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const std::pair<int, const TrackItem*>& a, const std::pair<int, const TrackItem*>& b) {
                         return a.first > b.first;
                     });
    results.reserve(scored.size());
    for (const auto& p : scored) results.push_back(p.second);
    return results;
}

std::vector<TrackGroup> MusicCatalog::SearchGrouped(const std::wstring& query) const {
    auto rawHits = Search(query, false);
    std::vector<TrackGroup> groups;
    std::unordered_map<std::wstring, size_t> groupMap;
    groupMap.reserve(rawHits.size());

    for (const auto* t : rawHits) {
        // 归一化语义键 (歌手 + 歌名)
        std::wstring key = t->search_artist + L"|||" + t->search_title;
        auto it = groupMap.find(key);
        if (it == groupMap.end()) {
            groupMap.emplace(std::move(key), groups.size());
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
        for (size_t idx : it->second) results.push_back(&tracks_[idx]);
    }
    return results;
}

std::vector<const TrackItem*> MusicCatalog::FilterByAlbum(const std::wstring& album) const {
    std::vector<const TrackItem*> results;
    auto it = album_index_.find(album);
    if (it != album_index_.end()) {
        for (size_t idx : it->second) results.push_back(&tracks_[idx]);
    }
    return results;
}

std::vector<const TrackItem*> MusicCatalog::FilterByFormat(const std::wstring& format) const {
    std::vector<const TrackItem*> results;
    std::wstring upperFmt = format;
    for (auto& ch : upperFmt) ch = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(ch)));
    auto it = format_index_.find(upperFmt);
    if (it != format_index_.end()) {
        for (size_t idx : it->second) results.push_back(&tracks_[idx]);
    }
    return results;
}

std::vector<std::wstring> MusicCatalog::GetAllArtists() const {
    std::vector<std::wstring> list;
    list.reserve(artist_index_.size());
    for (const auto& pair : artist_index_) list.push_back(pair.first);
    return list;
}

std::vector<std::wstring> MusicCatalog::GetAllAlbums() const {
    std::vector<std::wstring> list;
    list.reserve(album_index_.size());
    for (const auto& pair : album_index_) list.push_back(pair.first);
    return list;
}

const TrackItem* MusicCatalog::FindById(uint64_t id) const {
    auto it = id_index_.find(id);
    return it == id_index_.end() ? nullptr : &tracks_[it->second];
}

const TrackItem* MusicCatalog::FindByPath(const std::wstring& filePath) const {
    auto it = path_index_.find(filePath);
    return it == path_index_.end() ? nullptr : &tracks_[it->second];
}

bool MusicCatalog::LoadCover(const TrackItem& track, std::vector<uint8_t>& outData, std::string& outMime) {
    if (!track.cover_data.empty()) {
        outData = track.cover_data;
        outMime = track.cover_mime;
        return true;
    }
    if (track.has_cover) {
        AudioMetadata m;
        m.has_cover = true;
        m.cover_offset = track.cover_offset;
        m.cover_size = track.cover_size;
        m.cover_mime_type = track.cover_mime;
        if (AudioTagParser::ExtractCover(track.file_path, m)) {
            outData = std::move(m.cover_data);
            outMime = m.cover_mime_type;
            return true;
        }
    }
    if (!track.external_cover_path.empty()) {
        platform::ReadOnlyFile f;
        if (f.Open(track.external_cover_path, false) && f.Size() > 0 && f.Size() <= (32u << 20)) {
            outData.resize(static_cast<size_t>(f.Size()));
            if (f.ReadAt(0, outData.data(), outData.size()) == outData.size()) {
                std::wstring e = Lower(ExtOf(track.external_cover_path));
                outMime = (e == L".png") ? "image/png" : (e == L".webp" ? "image/webp" : "image/jpeg");
                return true;
            }
        }
        outData.clear();
    }
    return false;
}

CatalogStats MusicCatalog::GetStats() const {
    CatalogStats s;
    s.total_tracks = tracks_.size();
    s.total_artists = artist_index_.size();
    s.total_albums = album_index_.size();
    for (const auto& t : tracks_) {
        s.total_duration_ms += t.duration_ms;
        if (t.is_lossless) s.lossless_count++;
        if (t.has_cover || !t.external_cover_path.empty()) s.with_cover_count++;
        if (t.is_hires) s.hires_count++;
        if (!t.is_cue_track || t.track_no <= 1) s.total_size_bytes += t.file_size;
    }
    return s;
}

void MusicCatalog::Clear() {
    tracks_.clear();
    artist_index_.clear();
    album_index_.clear();
    format_index_.clear();
    id_index_.clear();
    path_index_.clear();
    next_id_ = 1;
}
