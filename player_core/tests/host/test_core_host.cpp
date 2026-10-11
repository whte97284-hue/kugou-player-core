// 跨平台集成测试:入库流水线 + 曲库 + 拼音检索 + CUE + 封面 + KGM 解密内核
// 用法: test_core_host <fixture_dir> <work_dir> [bench_copies]
#include "../../include/audio_ingestion_pipeline.hpp"
#include "../../include/audio_tag_parser.hpp"
#include "../../include/cue_sheet.hpp"
#include "../../include/music_catalog.hpp"
#include "../../include/pinyin_table.hpp"
#include "../../include/platform_io.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;
static int g_pass = 0, g_fail = 0;

static void Check(const char* name, bool cond, const std::string& detail = "") {
    (cond ? g_pass : g_fail)++;
    std::printf("  [%s] %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  -- ", detail.c_str());
}

static std::string U8(const std::wstring& w) { return AudioTagParser::WideToUtf8(w); }
static std::wstring W(const std::string& s) { return AudioTagParser::Utf8ToWide(s); }

static const TrackItem* FindCue(const MusicCatalog& c, const std::wstring& title) {
    for (const auto& t : c.GetAllTracks())
        if (t.is_cue_track && t.title == title) return &t;
    return nullptr;
}

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <fixture_dir> <work_dir> [bench_copies]\n", argv[0]);
        return 2;
    }
    fs::path fx = argv[1], work = argv[2];
    int benchCopies = argc > 3 ? std::atoi(argv[3]) : 0;
    fs::remove_all(work);
    fs::create_directories(work);

    // ------------------------------------------------------------------
    std::printf("\n[1] KGM 解密内核:化简公式与原始半字节算法逐值等价\n");
    {
        bool same = true;
        for (int c = 0; c < 256 && same; ++c)
            for (int k = 0; k < 256 && same; k += 3)
                for (int m = 0; m < 256; ++m) {
                    uint8_t t = static_cast<uint8_t>(c ^ k);
                    uint8_t low = static_cast<uint8_t>((m & 0x0F) ^ (t & 0x0F));
                    uint8_t high = static_cast<uint8_t>((t >> 4) ^ (m >> 4) ^ low);
                    uint8_t ref = static_cast<uint8_t>(((high & 0x0F) << 4) | (low & 0x0F));
                    uint8_t key17[17] = {0};
                    key17[0] = static_cast<uint8_t>(k);
                    uint8_t mask[1] = {static_cast<uint8_t>(m)};
                    uint8_t v = static_cast<uint8_t>(c);
                    AudioIngestionPipeline::KgmDecryptBlock(&v, 1, 0, key17, mask, 1);
                    if (v != ref) { same = false; break; }
                }
        Check("x ^ (x << 4) 与原算法在全部 (c,k,m) 组合上结果一致", same);
    }

    // ------------------------------------------------------------------
    std::printf("\n[2] 构建测试曲库 (含 KGMA 加密样本、CUE 整轨、伴生封面、坏文件)\n");
    fs::path lib = work / "library";
    fs::copy(fx / "library_extras", lib, fs::copy_options::recursive);
    fs::create_directories(lib / "formats");
    for (const auto& e : fs::directory_iterator(fx))
        if (e.is_regular_file() && e.path().extension() != ".json") fs::copy_file(e.path(), lib / "formats" / e.path().filename());

    // 用随机掩码表加密一个 FLAC 生成 .kgma,验证流水线解密产物逐字节还原
    fs::path maskPath = work / "test.mask";
    std::vector<uint8_t> mask(1 << 16);
    std::mt19937 rng(7);
    for (auto& b : mask) b = static_cast<uint8_t>(rng());
    std::ofstream(maskPath, std::ios::binary).write(reinterpret_cast<const char*>(mask.data()), mask.size());
    std::vector<uint8_t> plain = ReadAll(fx / "flac_24_96_bigcover_first.flac");
    {
        std::vector<uint8_t> enc = plain;
        uint8_t key17[17];
        for (int i = 0; i < 16; ++i) key17[i] = static_cast<uint8_t>(rng());
        key17[16] = 0;
        // 变换是对合的:f(f(x)) = x,因此加密 = f(明文) ^ key ^ mask
        for (size_t i = 0; i < enc.size(); ++i) {
            uint8_t x = enc[i];
            enc[i] = static_cast<uint8_t>((x ^ (x << 4)) ^ key17[i % 17] ^ mask[i % mask.size()]);
        }
        std::vector<uint8_t> hdr(1024, 0);
        uint32_t hlen = 1024;
        std::memcpy(&hdr[16], &hlen, 4);
        std::memcpy(&hdr[28], key17, 16);
        fs::create_directories(lib / "kugou");
        std::ofstream o(lib / "kugou" / "放課後ティータイム - Listen!!.kgma", std::ios::binary);
        o.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
        o.write(reinterpret_cast<const char*>(enc.data()), enc.size());
    }

    MusicCatalog catalog;
    AudioIngestionPipeline pipe;
    pipe.SetTargetLibraryDir(W((work / "decoded").string()));
    pipe.SetKgmMaskPath(W(maskPath.string()));
    pipe.SetLazyCoverExtract(true);
    pipe.SetThreadCount(4);
    pipe.AddInputPath(W(lib.string()));
    size_t lastProgress = 0;
    bool monotonic = true;
    pipe.on_progress = [&](const IngestionProgress& p) {
        if (p.current != lastProgress + 1) monotonic = false;
        lastProgress = p.current;
    };
    auto results = pipe.RunSync(catalog);
    IngestionSummary sum = pipe.GetLastSummary();
    std::printf("  扫描 %zu 文件 / 入库 %zu 曲目 / 成功 %zu / 失败 %zu / CUE 分轨 %zu / 伴生封面 %zu / 读盘 %llu KB / %.3fs\n",
                sum.total_scanned, sum.tracks_added, sum.success_count, sum.failed_count, sum.cue_tracks,
                sum.external_covers, static_cast<unsigned long long>(sum.bytes_read / 1024), sum.elapsed_seconds);

    Check("进度回调串行且严格单调递增", monotonic && lastProgress == results.size());
    Check("非音频文件 (readme.txt) 不进入扫描", std::none_of(results.begin(), results.end(), [](const IngestionItemResult& r) {
              return r.source_path.find(L"readme.txt") != std::wstring::npos;
          }));
    Check("坏文件 broken.flac 解析失败但不影响整体", sum.failed_count == 1);

    // KGMA
    const IngestionItemResult* kg = nullptr;
    for (const auto& r : results)
        if (r.was_encrypted && r.source_path.find(L".kgma") != std::wstring::npos) kg = &r;
    Check("KGMA 解密成功", kg && kg->success, kg ? U8(kg->message) : "未找到");
    if (kg && kg->success) {
        Check("KGMA 解密产物与原始 FLAC 逐字节一致", ReadAll(fs::path(U8(kg->target_path))) == plain);
        Check("KGMA 产物入库并解析出 24bit/96k", [&] {
            const TrackItem* t = catalog.FindByPath(kg->target_path);
            return t && t->sample_rate == 96000 && t->bits_per_sample == 24 && t->is_hires;
        }());
    }

    // CUE
    const TrackItem* t1 = FindCue(catalog, L"十年");
    const TrackItem* t2 = FindCue(catalog, L"明年今日");
    const TrackItem* t3 = FindCue(catalog, L"黑白灰");
    Check("CUE (GBK, FILE 写 .wav 实为 .flac) 拆分出 3 首", sum.cue_tracks == 3 && t1 && t2 && t3);
    if (t1 && t2 && t3) {
        Check("CUE 第 2 轨起止 = 10.000s ~ 20.493s", t2->cue_start_ms == 10000 && t2->cue_end_ms == 20493,
              std::to_string(t2->cue_start_ms) + "~" + std::to_string(t2->cue_end_ms));
        Check("CUE 末轨时长 = 文件时长 - 起点", t3->duration_ms == 30000 - 20493, std::to_string(t3->duration_ms));
        Check("CUE 专辑 / 歌手 / 流派 继承自表头", t2->album == L"黑白灰" && t2->artist == L"陈奕迅" && t2->genre == L"Pop");
        Check("CUE 分轨关联 folder.jpg 伴生封面", !t1->external_cover_path.empty());
    }

    // 伴生封面继承
    const TrackItem* hires = nullptr;
    for (const auto& t : catalog.GetAllTracks())
        if (t.file_name == L"黑夜不再来_hires.flac") hires = &t;
    Check("CD1 子目录继承上级目录 cover.jpg", hires && hires->external_cover_path.find(L"cover.jpg") != std::wstring::npos);

    // 惰性封面
    const TrackItem* bigCover = nullptr;
    for (const auto& t : catalog.GetAllTracks())
        if (t.file_name == L"mp3_cbr320_id3v23_utf16_apic.mp3") bigCover = &t;
    Check("惰性模式入库不在内存中持有封面", bigCover && bigCover->has_cover && bigCover->cover_data.empty() && bigCover->cover_offset > 0);
    if (bigCover) {
        std::vector<uint8_t> img;
        std::string mime;
        bool ok = MusicCatalog::LoadCover(*bigCover, img, mime);
        Check("LoadCover 按物理偏移一次读出完整封面", ok && img.size() == bigCover->cover_size && mime == "image/jpeg",
              std::to_string(img.size()) + " bytes " + mime);
    }

    // ------------------------------------------------------------------
    std::printf("\n[3] 检索:拼音 / 首字母 / 简繁同音 / 假名罗马音 / 相关度排序 / 去重择优\n");
    auto has = [&](const std::vector<const TrackItem*>& v, const std::wstring& title) {
        return std::any_of(v.begin(), v.end(), [&](const TrackItem* t) { return t->title == title; });
    };
    Check("'陈奕迅' 命中繁体标签 '陳奕迅' 的曲目", has(catalog.Search(L"陈奕迅"), L"黑夜不再来"));
    Check("'cyx' 首字母命中", has(catalog.Search(L"cyx"), L"黑夜不再来"));
    Check("'chenyixun' 全拼命中", has(catalog.Search(L"chenyixun"), L"黑夜不再来"));
    Check("'heiyebuzai' 全拼前缀片段命中", has(catalog.Search(L"heiyebuzai"), L"黑夜不再来"));
    Check("'adamasu' 罗马音命中 'アダマス'", has(catalog.Search(L"adamasu"), L"アダマス"));
    Check("'shinian' 命中 CUE 分轨 '十年'", has(catalog.Search(L"shinian"), L"十年"));
    {
        auto r = catalog.Search(L"黑白灰");
        Check("标题精确匹配排在专辑匹配之前", !r.empty() && r.front()->title == L"黑白灰");
    }
    {
        auto groups = catalog.SearchGrouped(L"黑夜不再来");
        bool ok = groups.size() == 1 && groups[0].primary && groups[0].primary->sample_rate == 96000 &&
                  groups[0].duplicates.size() == 2;
        Check("三个版本折叠为一组,24bit/96k 版本被选为主版本", ok);
    }
    Check("拼音全表覆盖生僻字 '喆' / 繁体 '陳'", PinyinHelper::GetFullPinyin(L"陶喆陳") == "taozhechen");
    Check("拗音/促音罗马音 'きょう' -> kyou, 'ちょっと' -> chotto",
          PinyinHelper::GetFullPinyin(L"きょう") == "kyou" && PinyinHelper::GetFullPinyin(L"ちょっと") == "chotto",
          PinyinHelper::GetFullPinyin(L"きょう") + " " + PinyinHelper::GetFullPinyin(L"ちょっと"));

    auto st = catalog.GetStats();
    std::printf("  曲库统计: %zu 首 / 无损 %zu / Hi-Res %zu / 有封面 %zu / 歌手 %zu / 专辑 %zu\n", st.total_tracks,
                st.lossless_count, st.hires_count, st.with_cover_count, st.total_artists, st.total_albums);

    // ------------------------------------------------------------------
    if (benchCopies > 0) {
        std::printf("\n[4] 吞吐压测:复制 %d 份样本库\n", benchCopies);
        fs::path bench = work / "bench";
        for (int i = 0; i < benchCopies; ++i) {
            fs::path d = bench / ("disc_" + std::to_string(i));
            fs::create_directories(d);
            for (const auto& e : fs::directory_iterator(lib / "formats")) {
                std::error_code ec;
                fs::create_hard_link(e.path(), d / e.path().filename(), ec);
                if (ec) fs::copy_file(e.path(), d / e.path().filename());
            }
        }
        MusicCatalog bc;
        AudioIngestionPipeline bp;
        bp.SetLazyCoverExtract(true);
        bp.AddInputPath(W(bench.string()));
        auto t0 = std::chrono::steady_clock::now();
        bp.RunSync(bc);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        IngestionSummary bs = bp.GetLastSummary();
        std::printf("  %zu 文件 (%.1f MB) -> %zu 曲目, %.3fs, %.0f 首/秒, 读盘 %.1f MB (占库 %.2f%%), 线程 %zu\n",
                    bs.total_scanned, bs.library_bytes / 1048576.0, bs.tracks_added, sec, bs.total_scanned / sec,
                    bs.bytes_read / 1048576.0, 100.0 * bs.bytes_read / std::max<uint64_t>(1, bs.library_bytes), bs.thread_count);
        auto q0 = std::chrono::steady_clock::now();
        auto hits = bc.Search(L"chenyixun");
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - q0).count();
        std::printf("  检索 %zu 首曲库: %.3f ms (%zu 命中)\n", bc.GetAllTracks().size(), ms, hits.size());
        Check("吞吐 >= 500 首/秒 (页缓存热数据,衡量 CPU 开销)", bs.total_scanned / sec >= 500);
        Check("惰性入库读盘量 < 曲库体积的 15%", bs.bytes_read * 100 < bs.library_bytes * 15);
    }

    std::printf("\n==== host core tests: %d passed / %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
