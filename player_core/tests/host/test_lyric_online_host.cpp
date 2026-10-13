// 歌词内核宿主测试 (在线部分): KRC [language:] 中文翻译 / LRC 解析 / 酷狗在线接口 (模拟 HTTP) 与缓存
// 用法: test_lyric_online_host <work_dir>
#include "../../include/lyric_manager.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace lyric_core;
namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// Python 生成: 两行日文 KRC + [language:] (type 1 中文翻译, type 0 罗马音), zlib 压缩后按酷狗密钥异或, 带 krc1 头
static const unsigned char kKrcBlob[] = {107,114,99,49,56,219,52,231,159,124,182,7,65,176,222,17,73,149,124,222,133,21,9,221,87,134,124,28,64,21,25,0,235,204,24,0,41,240,141,225,216,96,30,250,32,197,145,252,117,149,211,240,88,216,161,224,127,142,243,156,7,85,173,238,40,77,151,14,38,248,173,227,102,248,117,191,216,173,251,57,99,70,120,8,174,205,170,66,15,180,154,59,212,126,249,62,209,23,208,235,176,203,42,253,28,29,53,217,250,142,168,60,174,131,108,223,105,70,121,91,99,129,98,84,141,11,124,193,20,10,217,172,43,53,32,45,55,109,31,86,246,113,98,86,202,57,62,134,132,23,82,21,137,0,214,78,73,90,65,21,106,234,210,141,61,42,13,17,35,40,234,66,101,180,182,161,24,127,254,68,239,47,81,126,182,48,106,86,58,162,75,241,145,190,254,55,148,208,211,105,219,192,77,246,151,116,130,130,123,183,166,7,61,139,125,22,60,8,64,160,105,225,99,34,72,145,7,191,189,36,161,74,196,19,7,231,174,213,36,21,225,57,254,251,49,103,33,122,184,78,125,137,93,177,222,232,94,127,77,33,184,105,71,51,206,56,186,59,93,68,110,153,227,49,96,240,141,227,153,188,92,245,136,97,164,176,53,135,172,172,44,124,20,98,97,221,190,86,139,177,245,208,155,225,184,9,97,60,160,57,212,15,230,201,12,248,174,76,233,64,17,249,189,15,17,209,171,46,231,70,235,24,133,106,4,83,169,37,193,246,213,40,253,68,208,177};

static std::string Base64(const std::string& in) {
    static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        out += a[v >> 18]; out += a[(v >> 12) & 63]; out += a[(v >> 6) & 63]; out += a[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += a[v >> 18]; out += a[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8;
        out += a[v >> 18]; out += a[(v >> 12) & 63]; out += a[(v >> 6) & 63]; out += '=';
    }
    return out;
}

static void Touch(const fs::path& p, const std::string& data) {
    std::ofstream(p, std::ios::binary) << data;
}

int main(int argc, char** argv) {
    fs::path work = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "lyric_host";
    fs::remove_all(work);
        fs::create_directories(work / "cache");
    const std::string blob(reinterpret_cast<const char*>(kKrcBlob), sizeof(kKrcBlob));

    std::printf("[1] KRC 解密 + [language:] 翻译\n");
    {
        ParsedLyrics l;
        CHECK(KrcDecoder::DecodeMemory(kKrcBlob, sizeof(kKrcBlob), l));
        CHECK(l.lines.size() == 2);
        CHECK(l.title == L"Refrain" && l.artist == L"Aimer");
        if (l.lines.size() == 2) {
            CHECK(l.lines[0].start_ms == 6850 && l.lines[0].full_text == L"夏の午後に");
            CHECK(l.lines[0].syllables.size() == 3 && l.lines[0].syllables[1].start_ms == 7350);
            CHECK(l.lines[0].translation == L"在夏日的午后");
            CHECK(l.lines[1].translation == L"你的声音");
            CHECK(l.lines[0].romaji == L"natsu no");
        }
        CHECK(l.HasTranslation());
        std::string lrc = l.ToStandardLrc(true);
        CHECK(lrc.find("[00:06.85]夏の午後に\r\n[00:06.85]在夏日的午后\r\n") != std::string::npos);
        CHECK(l.ToStandardLrc(false).find("在夏日的午后") == std::string::npos);
    }

    std::printf("[2] LRC 解析 (多时间戳 / 双语同时间戳 / offset)\n");
    {
        ParsedLyrics l;
        std::string lrc = "\xEF\xBB\xBF[ti:梦良衣]\n[ar:ilem]\n[offset:-200]\n[00:04.00]作词：ilem\n"
                          "[00:10.5][01:00.120]副歌\n[00:10.5]chorus\n[00:20.00]\n[00:30.00]结尾\n";
        CHECK(KrcDecoder::ParseLrcText(lrc, l));
        CHECK(l.title == L"梦良衣" && l.artist == L"ilem" && l.offset_ms == -200);
        CHECK(l.lines.size() == 4);
        if (l.lines.size() == 4) {
            CHECK(l.lines[0].start_ms == 4000);
            CHECK(l.lines[1].start_ms == 10500 && l.lines[1].full_text == L"副歌" && l.lines[1].translation == L"chorus");
            CHECK(l.lines[1].duration_ms == 9500); // 到空白行 00:20 结束
            CHECK(l.lines[2].start_ms == 30000);
            CHECK(l.lines[3].start_ms == 60120 && l.lines[3].translation.empty());
        }
    }

    std::printf("[3] 酷狗在线接口 (模拟 HTTP) + 缓存\n");
    {
        int calls = 0;
        std::string lastSearch;
        bool krcFails = false;
        auto http = [&](const std::string& url, std::string& body) -> bool {
            ++calls;
            if (url.find("http://krcs.kugou.com/search?") == 0) {
                lastSearch = url;
                body = R"({"status":200,"errcode":200,"info":"OK","candidates":[)"
                       R"({"id":"1","accesskey":"AAAA","song":"Live","singer":"x","duration":300000},)"
                       R"({"id":116319439,"accesskey":"EA6F1950D581BFD6E9CD96FF5A238F53","song":"梦良衣","singer":"ilem","duration":185000}]})";
                return true;
            }
            if (url.find("http://krcs.kugou.com/download?") == 0) {
                if (url.find("id=116319439") == std::string::npos || url.find("accesskey=EA6F") == std::string::npos) return false;
                if (url.find("fmt=krc") != std::string::npos) {
                    if (krcFails) return false; // 主域名失败 -> 走备用域名, 备用也失败 -> 退回 lrc
                    body = std::string(R"({"status":200,"info":"OK","charset":"utf8","fmt":"krc","content":")") + Base64(blob) + "\"}";
                    return true;
                }
                body = std::string(R"({"status":200,"fmt":"lrc","content":")") + Base64("[ti:梦良衣]\n[00:04.00]作词：ilem\n") + "\"}";
                return true;
            }
            return false;
        };
        LyricManager mgr;
        mgr.SetCacheDirectory((work / "cache").wstring());
        mgr.SetHttpGetter(http);
        ParsedLyrics l;
        CHECK(mgr.FetchOnline(L"/music/a.flac", L"梦良衣", L"ilem", 184600, l));
        CHECK(lastSearch.find("keyword=ilem%20-%20%E6%A2%A6%E8%89%AF%E8%A1%A3") != std::string::npos);
        CHECK(lastSearch.find("duration=184600") != std::string::npos);
        CHECK(l.lines.size() == 2 && l.HasTranslation());
        CHECK(fs::exists(work / "cache" / "ilem - 梦良衣.krc"));
        // 缓存的 .krc 之后由本地匹配直接命中, 不再联网
        int before = calls;
        ParsedLyrics again;
        CHECK(mgr.LoadLyricsForTrack(L"/music/b.flac", L"梦良衣", L"ilem", again) && again.HasTranslation());
        CHECK(calls == before);

        krcFails = true;
        ParsedLyrics lrc;
        CHECK(mgr.FetchOnline(L"/music/c.flac", L"梦良衣", L"", 185000, lrc));
        CHECK(lrc.lines.size() == 1 && lrc.lines[0].full_text == L"作词：ilem");

        // 时长都对不上 -> 不采用, 并在本次运行中不重复请求
        ParsedLyrics none;
        CHECK(!mgr.FetchOnline(L"/music/d.flac", L"梦良衣", L"ilem", 60000, none));
        before = calls;
        CHECK(!mgr.FetchOnline(L"/music/d.flac", L"梦良衣", L"ilem", 60000, none));
        CHECK(calls == before);

        // 没有标签、文件名带曲目序号:按去掉序号的歌名搜索;以数字开头的歌名保持原样
        krcFails = false;
        ParsedLyrics numbered;
        mgr.FetchOnline(L"/music/01梦良衣.flac", L"", L"", 184600, numbered);
        CHECK(lastSearch.find("keyword=%E6%A2%A6%E8%89%AF%E8%A1%A3&") != std::string::npos);
        mgr.FetchOnline(L"/music/03. 梦良衣 (2).flac", L"", L"", 184000, numbered);
        CHECK(lastSearch.find("keyword=%E6%A2%A6%E8%89%AF%E8%A1%A3&") != std::string::npos);
        mgr.FetchOnline(L"/music/2002年的第一场雪.flac", L"", L"", 184000, numbered);
        CHECK(lastSearch.find("keyword=2002%E5%B9%B4") != std::string::npos);
        // 本地 .krc 匹配同样忽略序号 (上面缓存了「ilem - 梦良衣.krc」)
        ParsedLyrics local;
        CHECK(mgr.LoadLyricsForTrack(L"/music/x.flac", L"07 梦良衣", L"ilem", local));
    }

    std::printf("[4] 候选挑选\n");
    {
        std::vector<KuGouLyricCandidate> c(3);
        c[0].duration_ms = 200000; c[1].duration_ms = 185500; c[2].duration_ms = 185100;
        CHECK(KuGouOnlineLyrics::PickCandidate(c, 185000) == 2);
        CHECK(KuGouOnlineLyrics::PickCandidate(c, 0) == 0);
        CHECK(KuGouOnlineLyrics::PickCandidate(c, 100000) == -1);
    }

    std::printf(g_fail ? "lyric online host: %d FAILED\n" : "lyric online host: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
