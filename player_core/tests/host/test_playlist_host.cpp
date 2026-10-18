// 歌单导入宿主测试: 链接 / ID 归一化 / 酷狗与网易云解析 (假 HTTP) / 歌单曲目 → 本地曲库匹配
// 用法: test_playlist_host
#include "../../include/playlist_matcher.hpp"
#include "../../include/playlist_provider.hpp"
#include <cstdio>
#include <string>
#include <vector>

using namespace playlist_core;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static void TestInput() {
    std::printf("[1] 链接 / ID 归一化\n");
    struct Case { const char* in; Platform plat; const char* id; };
    const Case cases[] = {
        {"https://www.kugou.com/yy/special/single/1067062.html", Platform::Kugou, "1067062"},
        {"https://m.kugou.com/plist/list/6222311?json=true", Platform::Kugou, "6222311"},
        {"https://m.kugou.com/share/zlist.html?listid=4761867", Platform::Kugou, "4761867"},
        {"kugou:1067062", Platform::Kugou, "1067062"},
        {"https://music.163.com/playlist?id=7480897649", Platform::NetEase, "7480897649"},
        {"https://music.163.com/#/playlist?id=7480897649", Platform::NetEase, "7480897649"},
        {"https://y.music.163.com/m/playlist?id=7480897649&userid=1", Platform::NetEase, "7480897649"},
        {"https://music.163.com/playlist/7480897649/1/", Platform::NetEase, "7480897649"},
        {"分享歌单「随便听听」: https://music.163.com/playlist?id=7480897649 (来自网易云)", Platform::NetEase, "7480897649"},
        {"netease:7480897649", Platform::NetEase, "7480897649"},
        {"  7480897649 ", Platform::Unknown, "7480897649"},
        // 酷狗新版:global_collection_id 优先于 listid (listid=3 只是列表类型)
        {"https://m.kugou.com/share/zlist.html?type=1&global_collection_id=collection_3_1016644002_3_0&listid=3&uid=1",
         Platform::Kugou, "gcid:collection_3_1016644002_3_0"},
        {"https://t1.kugou.com/3RB9v52G6V2", Platform::Kugou, "short:3RB9v52G6V2"},
        // App 的整段分享文案:取链接,不能把文案里的酷狗码 40717681 当成歌单号
        {"https://t1.kugou.com/3RB9v52G6V2  复制整段信息，去酷狗搜索此码 #40717681# 即可看到我的歌曲。\n"
         "如未安装酷狗，请点此下载安装  http://www.kugou.com", Platform::Kugou, "short:3RB9v52G6V2"},
    };
    for (const auto& c : cases) {
        Platform p = Platform::Unknown;
        std::string id;
        const bool ok = PlaylistProvider::ParseInput(c.in, p, id);
        if (!ok || p != c.plat || id != c.id)
            std::printf("  输入 %s → ok=%d plat=%s id=%s\n", c.in, ok, PlatformName(p), id.c_str());
        CHECK(ok && p == c.plat && id == c.id);
    }
    Platform p;
    std::string id;
    CHECK(!PlaylistProvider::ParseInput("周杰伦 晴天", p, id));
    CHECK(!PlaylistProvider::ParseInput("#40717681#", p, id));

    // 解析整段分享文案 / 酷狗码:绝不能落到老版页面 (www2 ... special/single/40717681)
    std::vector<std::string> urls;
    PlaylistProvider rec([&](const std::string& url, std::string& body) { urls.push_back(url); body.clear(); return false; });
    PlaylistResult r = rec.Parse("https://t1.kugou.com/3RB9v52G6V2  复制整段信息，去酷狗搜索此码 #40717681# 即可看到我的歌曲。");
    CHECK(!r.ok);
    for (const auto& u : urls) CHECK(u.find("40717681") == std::string::npos && u.find("www2.kugou") == std::string::npos);
    r = rec.Parse("复制整段信息，去酷狗搜索此码 #40717681# 即可看到我的歌曲。");
    CHECK(!r.ok && r.error.find("酷狗码") != std::string::npos);
    // gcid 走新版网关,不走老页面
    urls.clear();
    rec.ParseAs(Platform::Kugou, "gcid:collection_3_1016644002_3_0");
    CHECK(!urls.empty() && urls[0].find("mobiles.kugou.com") != std::string::npos);
}

static const char* kKugouPage = R"(<html><script>
var global = { name: "夏夜", pic: "http://img/x.jpg", specialid: 1 };
global.data = [{"songname":"晴天","singername":"周杰伦","duration":269000,"hash":"AAA","hash_320":"B","album_name":"叶惠美"},
 {"songname":"<em>说好不哭</em>","singername":"周杰伦、五月天阿信","duration":222000,"hash":"CCC","album_name":""},
 {"songname":"","singername":"x","hash":"D"},
 {"songname":"稻香 \"Live\" 版","singername":"周杰伦","duration":"230000","hash":"EEE"}];
</script></html>)";

static void TestFetch() {
    std::printf("[2] 解析 (假 HTTP)\n");
    std::vector<std::string> urls;
    PlaylistProvider kg([&](const std::string& url, std::string& body) {
        urls.push_back(url);
        body = kKugouPage;
        return true;
    });
    PlaylistResult r = kg.Parse("https://www.kugou.com/yy/special/single/1067062.html");
    CHECK(r.ok);
    CHECK(!urls.empty() && urls[0].rfind("http://www2.kugou.kugou.com/", 0) == 0);
    CHECK(r.meta.name == L"夏夜");
    CHECK(r.tracks.size() == 3);           // 空歌名的那条丢掉
    if (r.tracks.size() == 3) {
        CHECK(r.tracks[0].title == L"晴天" && r.tracks[0].artist == L"周杰伦" && r.tracks[0].duration_ms == 269000);
        CHECK(r.tracks[0].id == "AAA" && r.tracks[0].kugou_hash_320 == "B" && r.tracks[0].album == L"叶惠美");
        CHECK(r.tracks[1].title == L"说好不哭");  // <em> 剥掉
        CHECK(r.tracks[2].duration_ms == 230000 && r.tracks[2].title == L"稻香 \"Live\" 版");
    }

    // 网易云:两步式,trackIds 是对象,超过 200 首分批
    int detailCalls = 0;
    PlaylistProvider ne([&](const std::string& url, std::string& body) {
        if (url.find("/playlist/detail") != std::string::npos) {
            std::string ids;
            for (int i = 1; i <= 205; ++i) ids += (i > 1 ? "," : "") + std::string("{\"id\":") + std::to_string(i) + "}";
            body = "{\"code\":200,\"playlist\":{\"name\":\"跑步\",\"trackCount\":205,\"creator\":{\"nickname\":\"阿丙\"},"
                   "\"trackIds\":[" + ids + "]}}";
            return true;
        }
        if (url.find("/song/detail") != std::string::npos) {
            ++detailCalls;
            const size_t a = url.find("?id=") + 4, b = url.find('&', a);
            std::string csv = url.substr(a, b - a), songs;
            size_t s = 0;
            while (s < csv.size()) {
                size_t e = csv.find(',', s);
                if (e == std::string::npos) e = csv.size();
                const std::string one = csv.substr(s, e - s);
                songs += (songs.empty() ? "" : ",") + std::string("{\"id\":") + one + ",\"name\":\"歌" + one +
                         "\",\"ar\":[{\"name\":\"甲\"},{\"name\":\"乙\"}],\"al\":{\"id\":9,\"name\":\"专辑\"},\"dt\":200000}";
                s = e + 1;
            }
            body = "{\"code\":200,\"songs\":[" + songs + "]}";
            return true;
        }
        return false;
    });
    r = ne.ParseAs(Platform::NetEase, "7480897649");
    CHECK(r.ok);
    CHECK(detailCalls == 2);
    CHECK(r.tracks.size() == 205);
    CHECK(r.meta.name == L"跑步" && r.meta.author == L"阿丙" && r.meta.total == 205);
    if (!r.tracks.empty()) CHECK(r.tracks[0].artist == L"甲 / 乙" && r.tracks[0].duration_ms == 200000);

    // 失败:网络 / 纯数字没指定平台
    PlaylistProvider down([](const std::string&, std::string&) { return false; });
    r = down.Parse("kugou:1");
    CHECK(!r.ok && !r.error.empty());
    r = down.Parse("123456");
    CHECK(!r.ok && !r.error.empty());
}

static PlaylistTrack T(const wchar_t* title, const wchar_t* artist, uint32_t ms) {
    PlaylistTrack t;
    t.title = title;
    t.artist = artist;
    t.duration_ms = ms;
    return t;
}
static LocalTrack L(uint64_t id, const wchar_t* title, const wchar_t* artist, uint32_t ms, bool lossless = false, int kbps = 320) {
    LocalTrack t;
    t.id = id;
    t.title = title;
    t.artist = artist;
    t.duration_ms = ms;
    t.lossless = lossless;
    t.bitrate_kbps = kbps;
    if (lossless) { t.bits_per_sample = 16; t.sample_rate = 44100; }
    return t;
}

static void TestMatch() {
    std::printf("[3] 匹配本地曲库\n");
    CHECK(StripNoteBrackets(L"Lemon (日剧《非自然死亡》主题曲)") == L"Lemon");
    CHECK(StripNoteBrackets(L"晴天 (Live)") == L"晴天 (Live)");
    CHECK(StripNoteBrackets(L"光年之外 - 电影《太空旅客》中文主题曲") == L"光年之外");

    std::vector<LocalTrack> lib = {
        L(1, L"晴天", L"周杰伦", 269500),                    // 0 MP3
        L(2, L"晴天", L"周杰伦", 269000, true),              // 1 FLAC:同一首取无损
        L(3, L"晴天 (Live)", L"周杰伦", 301000),             // 2 现场版
        L(4, L"说好不哭", L"周杰伦", 221500),                // 3 合唱只标了一位
        L(5, L"Lemon", L"米津玄師", 255000),                 // 4
        L(6, L"", L"", 240000),                              // 5 没标签,文件名「陈奕迅 - 十年.mp3」
        L(7, L"夜曲", L"", 226000),                          // 6 没歌手
        L(8, L"七里香", L"周杰伦", 299000),                  // 7 时长差很多 (加长版)
        L(9, L"Trouble Is a Friend", L"Lenka", 194000),     // 8 英文名歌手
        L(10, L"Shape of You", L"Ed Sheeran", 233000),      // 9
        L(11, L"Shape of You", L"J.Fla", 190000),           // 10 翻唱
    };
    lib[5].file_name = L"陈奕迅 - 十年.mp3";

    std::vector<PlaylistTrack> pl = {
        T(L"晴天", L"周杰伦", 269000),                                  // 0 Exact → FLAC
        T(L"晴天 (Live)", L"周杰伦", 300000),                           // 1 Exact → 现场版
        T(L"说好不哭 (with 五月天阿信)", L"周杰伦 / 五月天阿信", 222000), // 2 Exact (括号注释 + 歌手交集)
        T(L"Lemon (日剧《非自然死亡》主题曲)", L"米津玄师", 255500),     // 3 繁简不同 → 歌手不同但唯一且时长一致:可能
        T(L"十年", L"陈奕迅", 240500),                                   // 4 Exact (文件名)
        T(L"夜曲", L"周杰伦", 226500),                                   // 5 可能 (本地没歌手)
        T(L"七里香", L"周杰伦", 290000),                                 // 6 差 9 秒:不算
        T(L"七里香", L"周杰伦", 293000),                                 // 7 差 6 秒:可能
        T(L"稻香", L"周杰伦", 223000),                                   // 8 缺
        T(L"ＴＲＯＵＢＬＥ ＩＳ Ａ ＦＲＩＥＮＤ", L"lenka", 194000),     // 9 全角 + 大小写
        T(L"Shape of You", L"艾德·希兰", 233500),                        // 10 两份都歌手不同:不猜
        T(L"晴天", L"周杰伦", 0),                                        // 11 没时长也认
    };
    MatchSummary sum;
    auto m = MatchToLibrary(pl, lib, {}, &sum);
    auto is = [&](size_t i, MatchLevel lv, size_t local) {
        const bool ok = m[i].level == lv && (lv == MatchLevel::Missing || m[i].local == local);
        if (!ok) std::printf("  曲目 %zu: level=%d local=%zu\n", i, static_cast<int>(m[i].level), m[i].local);
        return ok;
    };
    CHECK(is(0, MatchLevel::Exact, 1));
    CHECK(is(1, MatchLevel::Exact, 2));
    CHECK(is(2, MatchLevel::Exact, 3));
    CHECK(is(3, MatchLevel::Likely, 4));
    CHECK(is(4, MatchLevel::Exact, 5));
    CHECK(is(5, MatchLevel::Likely, 6));
    CHECK(is(6, MatchLevel::Missing, 0));
    CHECK(is(7, MatchLevel::Likely, 7));
    CHECK(is(8, MatchLevel::Missing, 0));
    CHECK(is(9, MatchLevel::Exact, 8));
    CHECK(is(10, MatchLevel::Missing, 0));
    CHECK(is(11, MatchLevel::Exact, 1));
    CHECK(sum.exact == 6 && sum.likely == 3 && sum.missing == 3);
    CHECK(MatchToLibrary({}, lib).empty());
    CHECK(MatchToLibrary(pl, {})[0].level == MatchLevel::Missing);
}

int main() {
    TestInput();
    TestFetch();
    TestMatch();
    std::printf(g_fail ? "歌单导入: %d 项失败\n" : "歌单导入: 全部通过\n", g_fail);
    return g_fail ? 1 : 0;
}
