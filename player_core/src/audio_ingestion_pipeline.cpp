#include "../include/audio_ingestion_pipeline.hpp"
#include "../include/audio_tag_parser.hpp"
#include "../include/cue_sheet.hpp"
#include "../include/platform_io.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <map>
#include <unordered_map>

namespace {

#pragma pack(push, 1)
struct KgmHeaderRaw {
    uint8_t magic[16];
    uint32_t header_len;
    uint32_t crypto_ver;
    uint32_t key_derive;
    uint8_t file_key[16];
};
#pragma pack(pop)

std::wstring Lower(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    return s;
}

std::wstring ExtLower(const std::wstring& name) {
    size_t d = name.rfind(L'.');
    size_t sep = name.find_last_of(L"\\/");
    if (d == std::wstring::npos || (sep != std::wstring::npos && d < sep)) return L"";
    return Lower(name.substr(d));
}

std::wstring StemOf(const std::wstring& name) {
    size_t sep = name.find_last_of(L"\\/");
    std::wstring base = (sep == std::wstring::npos) ? name : name.substr(sep + 1);
    size_t d = base.rfind(L'.');
    return d == std::wstring::npos ? base : base.substr(0, d);
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring ParentOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"" : path.substr(0, p);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
#ifdef _WIN32
    const wchar_t sep = L'\\';
#else
    const wchar_t sep = L'/';
#endif
    wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + sep + name;
}

std::wstring SniffExt(const uint8_t* buf, size_t len) {
    if (len >= 4 && !std::memcmp(buf, "fLaC", 4)) return L".flac";
    if (len >= 3 && !std::memcmp(buf, "ID3", 3)) return L".mp3";
    if (len >= 2 && buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0) return L".mp3";
    if (len >= 4 && !std::memcmp(buf, "OggS", 4)) return L".ogg";
    if (len >= 12 && !std::memcmp(buf, "RIFF", 4) && !std::memcmp(buf + 8, "WAVE", 4)) return L".wav";
    if (len >= 8 && !std::memcmp(buf + 4, "ftyp", 4)) return L".m4a";
    return L"";
}

bool IsImageExt(const std::wstring& e) { return e == L".jpg" || e == L".jpeg" || e == L".png" || e == L".webp"; }

// 同目录伴生封面择优:cover > folder > front > albumart > 封面 > 目录内唯一图片
std::wstring PickFolderCover(const std::vector<platform::DirEntry>& files) {
    static const wchar_t* const kPriority[] = {L"cover", L"folder", L"front", L"albumartlarge", L"albumart", L"封面", L"album"};
    const platform::DirEntry* best = nullptr;
    int bestRank = 1000;
    size_t imageCount = 0;
    const platform::DirEntry* onlyImage = nullptr;
    for (const auto& f : files) {
        if (!IsImageExt(ExtLower(f.name)) || f.size == 0) continue;
        ++imageCount;
        onlyImage = &f;
        std::wstring stem = Lower(StemOf(f.name));
        for (int r = 0; r < static_cast<int>(sizeof(kPriority) / sizeof(kPriority[0])); ++r) {
            if (stem.compare(0, std::wcslen(kPriority[r]), kPriority[r]) == 0 && r < bestRank) {
                bestRank = r;
                best = &f;
            }
        }
    }
    if (best) return best->path;
    if (imageCount == 1) return onlyImage->path;
    return L"";
}

struct CueBinding {
    std::shared_ptr<CueSheet> sheet;
    size_t file_index = 0;
};

struct WorkItem {
    std::wstring path;
    std::wstring ext;
    uint64_t size = 0;
    std::wstring folder_cover;
    CueBinding cue;
};

bool IsEncryptedExt(const std::wstring& e) { return e == L".kgm" || e == L".kgma" || e == L".vpr"; }

} // namespace

// ===========================================================================
AudioIngestionPipeline::AudioIngestionPipeline() {
    if (platform::PathExists(L"tools/kgm/kgm.mask")) kgm_mask_path_ = L"tools/kgm/kgm.mask";
    target_lib_dir_ = L"output";
}

AudioIngestionPipeline::~AudioIngestionPipeline() {
    Cancel();
    if (async_worker_thread_.joinable()) async_worker_thread_.join();
}

void AudioIngestionPipeline::SetTargetLibraryDir(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    target_lib_dir_ = dir;
}

void AudioIngestionPipeline::SetKgmMaskPath(const std::wstring& maskPath) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    kgm_mask_path_ = maskPath;
}

void AudioIngestionPipeline::SetThreadCount(size_t threads) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    thread_count_ = threads;
}

void AudioIngestionPipeline::SetLazyCoverExtract(bool lazy) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    lazy_cover_extract_ = lazy;
}

void AudioIngestionPipeline::SetSplitCueSheets(bool split) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    split_cue_ = split;
}

void AudioIngestionPipeline::AddInputPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    input_paths_.push_back(path);
}

void AudioIngestionPipeline::ClearInputPaths() {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    input_paths_.clear();
}

void AudioIngestionPipeline::Cancel() { cancel_requested_.store(true); }

IngestionSummary AudioIngestionPipeline::GetLastSummary() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return last_summary_;
}

// ===========================================================================
// KGM 解密
// ===========================================================================
void AudioIngestionPipeline::KgmDecryptBlock(uint8_t* data, size_t len, uint64_t fileOffset, const uint8_t key17[17],
                                             const uint8_t* mask, uint64_t maskLen) {
    // 原始逐半字节算法:t = c ^ k; low = (m ^ t) & 0xF; high = ((t >> 4) ^ (m >> 4) ^ low) & 0xF
    // 代数化简后等价于:x = c ^ k ^ m; out = x ^ (x << 4)  (取低 8 位)
    size_t ki = static_cast<size_t>(fileOffset % 17);
    uint64_t mi = fileOffset % maskLen;
    for (size_t i = 0; i < len; ++i) {
        uint8_t x = static_cast<uint8_t>(data[i] ^ key17[ki] ^ mask[mi]);
        data[i] = static_cast<uint8_t>(x ^ (x << 4));
        if (++ki == 17) ki = 0;
        if (++mi == maskLen) mi = 0;
    }
}

bool AudioIngestionPipeline::DecryptKgm(const std::wstring& src, const std::wstring& targetDir,
                                        std::wstring& outDecryptedPath, std::wstring& outMessage) {
    auto mask = std::static_pointer_cast<platform::MappedFile>(mask_);
    if (!mask || !mask->Data()) {
        outMessage = L"KGM 掩码表缺失,无法解密";
        return false;
    }
    platform::ReadOnlyFile in;
    if (!in.Open(src, false)) {
        outMessage = L"无法打开源文件";
        return false;
    }
    KgmHeaderRaw header;
    if (in.ReadAt(0, &header, sizeof(header)) != sizeof(header) || header.header_len < sizeof(header) ||
        header.header_len >= in.Size()) {
        outMessage = L"KGM 文件头损坏";
        return false;
    }
    uint8_t key[17];
    std::memcpy(key, header.file_key, 16);
    key[16] = 0;

    constexpr size_t kChunk = 1u << 20;
    std::vector<uint8_t> buf(kChunk);
    uint64_t payload = in.Size() - header.header_len;
    size_t first = in.ReadAt(header.header_len, buf.data(), static_cast<size_t>(std::min<uint64_t>(kChunk, payload)));
    if (first == 0) {
        outMessage = L"KGM 负载为空";
        return false;
    }
    KgmDecryptBlock(buf.data(), first, 0, key, mask->Data(), mask->Size());
    std::wstring ext = SniffExt(buf.data(), first);
    if (ext.empty()) {
        outMessage = L"解密结果无法识别为音频 (掩码表不匹配?)";
        return false;
    }
    outDecryptedPath = JoinPath(targetDir, StemOf(src) + ext);

    // 增量入库:目标已存在且大小一致时跳过重复解密
    platform::ReadOnlyFile existing;
    if (existing.Open(outDecryptedPath, true) && existing.Size() == payload) {
        outMessage = L"已解密 (复用现有产物)";
        return true;
    }
    existing.Close();

    platform::WritableFile out;
    if (!out.Create(outDecryptedPath)) {
        outMessage = L"无法创建输出文件";
        return false;
    }
    bool ok = out.Write(buf.data(), first);
    uint64_t off = first;
    while (ok && off < payload && !cancel_requested_.load()) {
        size_t n = in.ReadAt(header.header_len + off, buf.data(), static_cast<size_t>(std::min<uint64_t>(kChunk, payload - off)));
        if (n == 0) break;
        KgmDecryptBlock(buf.data(), n, off, key, mask->Data(), mask->Size());
        ok = out.Write(buf.data(), n);
        off += n;
    }
    out.Close();
    if (!ok || off != payload) {
        platform::RemoveFile(outDecryptedPath); // 不留半截产物
        outMessage = cancel_requested_.load() ? L"已取消" : L"写入失败";
        return false;
    }
    outMessage = L"解密成功并持久化落盘";
    return true;
}

// ===========================================================================
// 主流程
// ===========================================================================
std::vector<IngestionItemResult> AudioIngestionPipeline::RunSync(MusicCatalog& catalog) {
    is_running_.store(true);
    cancel_requested_.store(false);
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();

    std::vector<std::wstring> inputs;
    std::wstring targetDir, maskPath;
    size_t requestedThreads;
    bool lazy, splitCue;
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        inputs = input_paths_;
        targetDir = target_lib_dir_;
        maskPath = kgm_mask_path_;
        requestedThreads = thread_count_;
        lazy = lazy_cover_extract_;
        splitCue = split_cue_;
    }

    IngestionSummary summary;

    // ---------------- 1. 枚举 ----------------
    std::vector<WorkItem> items;
    std::unordered_map<std::wstring, std::wstring> dirCover;
    for (const auto& input : inputs) {
        if (cancel_requested_.load()) break;
        if (platform::IsDirectory(input)) {
            platform::EnumerateFilesRecursive(input, [&](const std::wstring& dir, const std::vector<platform::DirEntry>& files) {
                std::wstring cover = PickFolderCover(files);
                if (cover.empty()) {
                    auto it = dirCover.find(ParentOf(dir)); // CD1/CD2 子目录沿用上级目录封面
                    if (it != dirCover.end()) cover = it->second;
                }
                if (!cover.empty()) dirCover[dir] = cover;

                size_t firstIdx = items.size();
                std::unordered_map<std::wstring, size_t> byName, byStem;
                for (const auto& f : files) {
                    std::wstring e = ExtLower(f.name);
                    if (!AudioTagParser::IsSupportedExtension(e)) continue;
                    WorkItem w;
                    w.path = f.path;
                    w.ext = e;
                    w.size = f.size;
                    w.folder_cover = cover;
                    byName[Lower(f.name)] = items.size();
                    byStem[Lower(StemOf(f.name))] = items.size();
                    items.push_back(std::move(w));
                }
                if (splitCue && items.size() > firstIdx) {
                    for (const auto& f : files) {
                        if (ExtLower(f.name) != L".cue") continue;
                        auto sheet = std::make_shared<CueSheet>();
                        if (!CueSheetParser::ParseFile(f.path, *sheet)) continue;
                        for (size_t fi = 0; fi < sheet->files.size(); ++fi) {
                            std::wstring ref = Lower(FileNameOf(sheet->files[fi].file_name));
                            size_t target = SIZE_MAX;
                            auto byNameIt = byName.find(ref);
                            if (byNameIt != byName.end()) {
                                target = byNameIt->second;
                            } else {
                                // CUE 中常写 .wav,实际已转为 .flac/.ape:按主文件名回退匹配
                                auto byStemIt = byStem.find(Lower(StemOf(ref)));
                                if (byStemIt != byStem.end()) target = byStemIt->second;
                                else if (sheet->files.size() == 1 && items.size() - firstIdx == 1) target = firstIdx;
                            }
                            if (target == SIZE_MAX) continue;
                            WorkItem& w = items[target];
                            if (!w.cue.sheet && sheet->files[fi].tracks.size() > 1) {
                                w.cue.sheet = sheet;
                                w.cue.file_index = fi;
                            }
                        }
                    }
                }
                return !cancel_requested_.load();
            });
        } else if (platform::PathExists(input)) {
            WorkItem w;
            w.path = input;
            w.ext = ExtLower(input);
            platform::ReadOnlyFile f;
            if (f.Open(input, true)) w.size = f.Size();
            items.push_back(std::move(w));
        }
    }
    summary.enumerate_seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    for (const auto& w : items) summary.library_bytes += w.size;

    const size_t total = items.size();
    std::vector<IngestionItemResult> results(total);
    if (total == 0) {
        summary.elapsed_seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        last_summary_ = summary;
        is_running_.store(false);
        return {};
    }

    // 需要解密时,掩码表整个运行期只映射一次
    bool needMask = std::any_of(items.begin(), items.end(), [](const WorkItem& w) { return IsEncryptedExt(w.ext); });
    if (needMask && !mask_) {
        auto m = std::make_shared<platform::MappedFile>();
        if (!maskPath.empty() && m->Open(maskPath)) mask_ = m;
    }
    if (needMask && !targetDir.empty()) platform::CreateDirectories(targetDir);

    // ---------------- 2. 存储感知并发度 ----------------
    unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    summary.storage_seek_penalty = inputs.empty() ? -1 : platform::QuerySeekPenalty(inputs.front());
    size_t numThreads = requestedThreads;
    if (numThreads == 0) {
        if (summary.storage_seek_penalty == 1) numThreads = 4;                    // 机械盘:适度队列深度利用 NCQ
        else if (summary.storage_seek_penalty == 0) numThreads = std::min<size_t>(hw * 2, 32); // 固态盘:I/O 并行
        else numThreads = std::min<size_t>(hw, 8);
    }
    numThreads = std::max<size_t>(1, std::min(numThreads, total));
    summary.thread_count = numThreads;

    ParseOptions parseOpt;
    parseOpt.load_cover = !lazy;

    std::atomic<size_t> nextIndex{0};
    std::atomic<uint64_t> bytesRead{0};
    std::atomic<size_t> cueTracks{0}, externalCovers{0};
    std::mutex catalogMutex, progressMutex;
    size_t progressDone = 0;
    auto progressFn = on_progress;

    auto worker = [&]() {
        std::vector<TrackItem> batch;
        batch.reserve(64);
        auto flush = [&]() {
            if (batch.empty()) return;
            std::lock_guard<std::mutex> lock(catalogMutex);
            catalog.AddPreparedTracks(std::move(batch));
            batch.clear();
        };
        auto addTrack = [&](const std::wstring& path, const AudioMetadata& meta, const WorkItem& w) {
            TrackItem t = MusicCatalog::BuildTrack(path, meta);
            if (!t.has_cover && !w.folder_cover.empty()) {
                t.external_cover_path = w.folder_cover;
                externalCovers.fetch_add(1);
            }
            batch.push_back(std::move(t));
        };

        while (!cancel_requested_.load()) {
            size_t idx = nextIndex.fetch_add(1);
            if (idx >= total) break;
            const WorkItem& w = items[idx];
            IngestionItemResult& item = results[idx];
            item.source_path = w.path;

            if (IsEncryptedExt(w.ext)) {
                item.was_encrypted = true;
                std::wstring decPath, msg;
                if (DecryptKgm(w.path, targetDir, decPath, msg)) {
                    item.target_path = decPath;
                    item.success = true;
                    AudioMetadata meta;
                    if (AudioTagParser::ParseFile(decPath, meta, parseOpt)) {
                        bytesRead.fetch_add(meta.io_bytes_read);
                        addTrack(decPath, meta, w);
                    }
                } else {
                    item.success = false;
                }
                item.message = msg;
            } else if (w.ext == L".ncm" || w.ext == L".kgg") {
                item.was_encrypted = true;
                // 已有解码产物时优先关联真实音频
                std::wstring candidate;
                for (const wchar_t* e : {L".flac", L".mp3", L".ogg", L".m4a"}) {
                    std::wstring c = JoinPath(targetDir, StemOf(w.path) + e);
                    if (platform::PathExists(c)) { candidate = c; break; }
                }
                AudioMetadata meta;
                if (!candidate.empty() && AudioTagParser::ParseFile(candidate, meta, parseOpt)) {
                    item.target_path = candidate;
                    item.message = L"关联到已有解码音频并入库";
                    bytesRead.fetch_add(meta.io_bytes_read);
                    addTrack(candidate, meta, w);
                } else {
                    AudioMetadata raw;
                    AudioTagParser::ParseFile(w.path, raw, parseOpt); // NCM 头部元数据可直接解出歌名/歌手/时长
                    bytesRead.fetch_add(raw.io_bytes_read);
                    if (raw.format.empty()) raw.format = (w.ext == L".ncm") ? L"NCM" : L"KGG";
                    item.target_path = w.path;
                    item.message = raw.title.empty() ? L"入库登记待转换源" : L"入库登记待转换源 (已解出加密元数据)";
                    addTrack(w.path, raw, w);
                }
                item.success = true;
            } else {
                item.target_path = w.path;
                AudioMetadata meta;
                if (AudioTagParser::ParseFile(w.path, meta, parseOpt)) {
                    bytesRead.fetch_add(meta.io_bytes_read);
                    item.success = true;
                    if (w.cue.sheet) {
                        // CUE 整轨拆分为独立曲目
                        const CueSheet& sheet = *w.cue.sheet;
                        const CueFileEntry& fe = sheet.files[w.cue.file_index];
                        for (const auto& tr : fe.tracks) {
                            AudioMetadata m = meta;
                            m.title = tr.title;
                            m.artist = !tr.performer.empty() ? tr.performer : (!sheet.performer.empty() ? sheet.performer : meta.artist);
                            m.album = !sheet.title.empty() ? sheet.title : meta.album;
                            if (!sheet.performer.empty()) m.album_artist = sheet.performer;
                            if (!sheet.genre.empty()) m.genre = sheet.genre;
                            if (!sheet.date.empty()) m.year = sheet.date;
                            m.track_number = std::to_wstring(tr.number);
                            uint32_t end = tr.end_ms ? tr.end_ms : meta.duration_ms;
                            m.duration_ms = end > tr.start_ms ? end - tr.start_ms : 0;
                            m.cover_data.clear(); // 封面只保留偏移,不重复复制
                            TrackItem t = MusicCatalog::BuildTrack(w.path, m);
                            t.is_cue_track = true;
                            t.cue_start_ms = tr.start_ms;
                            t.cue_end_ms = tr.end_ms;
                            if (!t.has_cover && !w.folder_cover.empty()) t.external_cover_path = w.folder_cover;
                            batch.push_back(std::move(t));
                        }
                        cueTracks.fetch_add(fe.tracks.size());
                        item.message = L"CUE 分轨入库 " + std::to_wstring(fe.tracks.size()) + L" 首";
                    } else {
                        addTrack(w.path, meta, w);
                        item.message = L"直接入库成功";
                    }
                } else {
                    item.success = false;
                    item.message = L"元数据解析失败";
                }
            }

            if (batch.size() >= 64) flush();

            if (progressFn) {
                std::lock_guard<std::mutex> lock(progressMutex);
                IngestionProgress prog;
                prog.current = ++progressDone;
                prog.total = total;
                prog.current_filename = FileNameOf(w.path);
                prog.is_transcoding = item.was_encrypted;
                prog.status_text = item.message;
                progressFn(prog);
            }
        }
        flush();
    };

    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (size_t t = 0; t < numThreads; ++t) workers.emplace_back(worker);
    for (auto& th : workers) th.join();

    // ---------------- 3. 汇总 ----------------
    std::vector<IngestionItemResult> out;
    out.reserve(total);
    for (auto& r : results) {
        if (r.source_path.empty()) continue; // 取消时未处理的条目
        if (r.success) summary.success_count++;
        else summary.failed_count++;
        if (r.was_encrypted) summary.transcoded_count++;
        out.push_back(std::move(r));
    }
    summary.total_scanned = out.size();
    summary.cue_tracks = cueTracks.load();
    summary.external_covers = externalCovers.load();
    summary.bytes_read = bytesRead.load();
    summary.tracks_added = catalog.GetAllTracks().size();
    summary.cancelled = cancel_requested_.load();
    summary.elapsed_seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        last_summary_ = summary;
    }
    mask_.reset();
    is_running_.store(false);
    return out;
}

bool AudioIngestionPipeline::StartAsync(MusicCatalog& catalog, std::function<void(const IngestionProgress&)> onProgress,
                                        std::function<void(const IngestionSummary&)> onFinished) {
    if (is_running_.exchange(true)) return false;
    if (async_worker_thread_.joinable()) async_worker_thread_.join();
    cancel_requested_.store(false);
    on_progress = std::move(onProgress); // 在调用线程中设置,避免与工作线程竞争

    async_worker_thread_ = std::thread([this, &catalog, onFinished]() {
        this->RunSync(catalog);
        if (onFinished) onFinished(GetLastSummary());
    });
    return true;
}
