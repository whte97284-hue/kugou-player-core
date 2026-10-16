# PlaylistProvider —— 歌单分享链接/ID → 明文曲目清单

> 职责边界：**只做「链接或 ID → 明文曲目清单」这一步**。
> 播放器识别、资源指纹、查重、去重由上层负责。

---

## 1. 文件

| 文件 | 说明 |
|---|---|
| `playlist_provider.hpp` | 对外接口（放 `player_core/include/`） |
| `playlist_provider.cpp` | 实现（放 `player_core/src/`，加入 CMakeLists） |
| `pltest.cpp` | 验证 harness（放 `player_core/test/`） |

**依赖**：仅 `winhttp.lib`（已在 `lyric_provider` 的链接里）。零第三方库，自带轻量 JSON 解析。

---

## 2. 最小用法

```cpp
#include "playlist_provider.hpp"
using namespace playlist_core;

PlaylistProvider pp;              // 默认走 WinHTTP；可注入假实现做测试
PlaylistResult r = pp.Parse("https://music.163.com/playlist?id=7480897649");

if (r.ok) {
    printf("%s / %zu 首\n", WideToUtf8(r.meta.name).c_str(), r.tracks.size());
    for (const auto& t : r.tracks) {
        // t.title / t.artist / t.album / t.duration_ms / t.id
        // t.source = "kugou" | "netease"
        // 酷狗额外: t.kugou_hash_320 / t.kugou_hash_flac / t.kugou_audio_id / t.kugou_album_id
        // 网易云额外: t.netease_album_id
    }
} else {
    printf("失败: %s\n", r.error.c_str());
}
```

---

## 3. 数据模型

```cpp
struct PlaylistTrack {
    std::string  source;        // "kugou" / "netease"
    std::string  id;            // 平台主键：酷狗=hash / 网易云=歌曲数字id
    std::wstring title;         // 歌名（酷狗为 songname，干净无后缀）
    std::wstring artist;        // 歌手（多人已按 " / " 拼接）
    std::wstring album;         // 专辑名（可能为空）
    uint32_t     duration_ms;   // 时长毫秒（0=未知）

    // 平台扩展字段（做指纹/查重时可选用）
    std::string kugou_hash_320, kugou_hash_flac;
    std::string kugou_audio_id, kugou_album_id;
    std::string netease_album_id;
};

struct PlaylistMeta {
    std::string  source, id;
    std::wstring name, author, description;
    std::string  cover_url;
    int          total;         // 标称曲目总数
};

struct PlaylistResult {
    PlaylistMeta meta;
    std::vector<PlaylistTrack> tracks;
    std::string error;          // 失败原因（成功时为空）
    bool ok;
    int with_duration, with_album;   // 统计：带时长/带专辑的曲目数
};
```

**上层做"本地曲库匹配"建议只用三个字段**：`title` + `artist` + 平台 `id`（酷狗带 `hash` 的话更好，可直接喂歌词内核）。

---

## 4. 输入形态（13 种实测通过）

### 酷狗
```
https://www.kugou.com/yy/special/single/1067062.html
https://m.kugou.com/plist/list/6222311?json=true
https://m.kugou.com/share/zlist.html?listid=4761867
https://www.kugou.com/share/xxxxx?specialid=4761867
https://www.kugou.com/share/xxxxx?global_collection_id=collection_3_...   ← 新版
https://t1.kugou.com/3RB9v52G6V2      ← 短链（需联网跟随 302，已支持）
kugou:1067062                         ← 自定义 scheme，显式指定平台
#40717681#                            ← 酷狗码，给明确报错（无公开接口，见 §8）
```

### 网易云
```
https://music.163.com/playlist?id=7480897649
https://music.163.com/#/playlist?id=7480897649
https://y.music.163.com/m/playlist?id=7480897649
https://music.163.com/playlist/7480897649/1/       ← 旧版路径
https://music.163.com/#/my/m/music/playlist?id=7480897649
netease:7480897649                 ← 自定义 scheme
```

### 纯数字 ID
```
7480897649
```
⚠ **纯数字无法判断平台**。此时 `ParseInput` 返回 `Platform::Unknown` + 填入 id。
前端需要**让用户选平台**，然后调 `ParseAs(Platform::NetEase, id)`。

---

## 5. 接口一览

```cpp
// —— 归一化（静态，可单独测试）——
static Platform DetectPlatform(const std::string& input);
static bool ParseInput(const std::string& input, Platform& plat, std::string& id);
bool ResolveInput(const std::string& input, Platform& plat, std::string& id,
                  std::string* error = nullptr) const;   // 短链会联网跟随重定向

// —— 主入口 ——
PlaylistResult Parse(const std::string& input) const;            // 输入可为 ID 或链接
PlaylistResult ParseAs(Platform plat, const std::string& id) const;

// —— 分平台 ——
bool FetchKugou(const std::string& id, PlaylistResult& out) const;   // 自动分流: 数字ID / gcid
bool FetchKugouGcid(const std::string& gcid, PlaylistResult& out) const;  // 新版 collection_3_xxx
bool FetchNetease(const std::string& id, PlaylistResult& out) const;

// —— 工具 ——
static bool DefaultHttpGet(const std::string& url, std::string& body);
static bool FollowRedirect(const std::string& url, std::string& final_url,
                           std::string* error = nullptr, int max_hops = 5);
```

---

## 6. 协议（2026-10-16 实测）

### 酷狗 —— HTTP 明文，无需签名
```
GET http://www2.kugou.kugou.com/yueku/v9/special/single/{specialId}-5-9999.html
```
页面内嵌两个 JS 变量：
```js
global.data = [ {曲目1}, {曲目2}, ... ];        // 曲目数组
global = { ... name: "歌单名" ... pic: "封面" ... };
```
单条曲目字段：
| 字段 | 含义 |
|---|---|
| `songname` | 歌名（**干净，无《》后缀**） |
| `singername` | 歌手 |
| `duration` | 时长（毫秒） |
| `hash` | 128k hash（**可直接喂歌词内核**） |
| `hash_320` / `hash_flac` / `hash_ape` | 各音质 hash |
| `audio_id` / `album_id` / `album_audio_id` | 各 id |
| `album_name` | 专辑名 |

**⚠ 必须用 `http://`，不能用 `https://`** —— `www2.kugou.kugou.com` 是 CDN 节点，
其 TLS 证书 hostname 与域名不匹配（WinHTTP 报 12175 `ERROR_WINHTTP_SECURE_FAILURE`）。
`SECURITY_FLAGS` 对发送阶段的握手失败**无效**。公开歌单数据无需加密，http 即可。

#### 酷狗 · 新版 `global_collection_id`（需 H5 签名，2026-10-16 实测）

触发条件：输入的分享链接 / 短链里带 `global_collection_id=collection_3_{uid}_{n}_0`。
**这种歌单走不了上面的 www2 yueku 路径**（页面里根本没有 `global.data`，旧实现会报
「未找到 global data」—— 这正是历史 bug②的根因）。

正确路径是酷狗移动端 API 网关，**两步 + 签名**：

```
1) 元信息
   GET https://mobiles.kugou.com/api/v5/special/info_v2
       ?appid=1058&specialid=0&global_specialid={gcid}&format=jsonp
       &srcappid=2919&clientver=20000&clienttime={ms}&mid={ms}&uuid={ms}&dfid=-
       &signature={sig}
   → data.specialname / nickname / songcount / imgurl / intro

2) 曲目（分页，单页最多 300）
   GET https://mobiles.kugou.com/api/v5/special/song_v2
       ?appid=1058&global_specialid={gcid}&specialid=0&plat=0&version=8000
       &page={n}&pagesize={<=300}
       &srcappid=2919&clientver=20000&clienttime={ms}&mid={ms}&uuid={ms}&dfid=-
       &signature={sig}
   → data.info[]  (每首: filename / hash / 320hash / sqhash / duration(秒) / album_id / audio_id)
```

**签名算法**（酷狗 web 前端 `staticssl.kugou.com/common/js/min/infSign.min.js` 里的 H5 模式）：
```
signature = MD5( KEY + join(k=v, 按 key 升序) + KEY )
KEY = "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt"     ← js 里的公开字面量
```
已在本模块内**自研实现 MD5**（RFC 1321），不引第三方库。
签名覆盖的参数**不含 `signature` 自身**，但**含空值参数**（如 `dfid=-` 的反面情形要注意）。

**⚠⚠ 三个已踩死的坑（务必照做）**：

1. **必须带三个 HTTP 请求头：`mid` / `dfid` / `clienttime`**
   缺任意一个 → 一律返回 `{"errcode":1001,"status":0}`，且**报错信息里没有任何提示**。
   实测：只带 `User-Agent` ❌、只带 `Referer` ❌、带桌面 UA ❌；三者齐全 ✅。
   值与 query 里的 `mid=uuid=clienttime={ms}` 保持一致即可。

2. **短链解析必须优先取 `global_collection_id`，绝不能先取 `listid`**
   短链 302 后的分享 URL 里同时有：
   ```
   ...&global_collection_id=collection_3_1016644002_3_0&listid=3&...
   ```
   `listid=3` 是**列表类型**（短数字，误导），不是歌单号。先取它会得到完全错误的歌单。

3. **`mobiles.kugou.com` 也必须用 `https://` 是 OK 的**（与 www2 相反，它证书正常）。
   而 `cloudlist.service.kugou.com` 反而证书不匹配，需用 http。

**字段差异（新版 vs 老版）**：
| 字段 | 老版 www2 yueku | 新版 song_v2 |
|---|---|---|
| 歌名 | `songname`（干净） | `filename`（`Artist - Title`，需拆） |
| 歌手 | `singername` | 从 filename 拆 |
| **专辑名** | ✅ `album_name` | ❌ **无**（只有 `album_id`） |
| 时长 | `duration`（毫秒） | `duration`（**秒**，需 ×1000） |
| 128k hash | `hash` | `hash` |
| 320k hash | `hash_320` | `320hash` |
| flac hash | `hash_flac` | `sqhash` |

→ 新版**拿不到专辑名**是接口固有限制。上层做指纹/查重请用 hash / album_id，别依赖专辑名。

### 网关接口汇总（备忘）
| 接口 | 用途 | 签名 | 备注 |
|---|---|---|---|
| `m3ws.kugou.com/zlist/list?chain={code}&page&pagesize` | 短链→曲目 | ❌ 免签 | 字段极少（无歌名/专辑/时长），**仅用作兜底** |
| `m.kugou.com/schain/transfer?chain={code}&su=1` | chain→gcid | ❌ 免签 | 返回 `global_collection_id`，适合做短链归一化 |
| `mobilecdn.kugou.com/api/v3/search/special?keyword=` | 搜歌单 | ❌ | 酷狗码**搜不到**（见 §8） |

**⚠** 分享页 `wwwapi.kugou.com/share/zlist.html` 的 HTML 里也内嵌 `dataFromSmarty = [{...}]`
（字段 `hash`/`timelength`/`audio_name`/`author_name`/`song_name`/`album_id`），
但**只有前 100 首**，不能当主路径（用户那个歌单实际 250 首）。

### 网易云 —— 两步式，无需登录/加密
```
1) POST http://music.163.com/api/v6/playlist/detail
   body:   s=0&id={playlistId}&n=1000&t=0
   header: UA + Referer: https://music.163.com/
   → playlist.name / trackCount / coverImgUrl / description / creator.nickname
   → playlist.trackIds[].id      ← ⚠ 元素是对象，不是裸整数

2) GET http://music.163.com/api/song/detail/?id={csv}&ids=%5B{csv}%5D
   （csv = 逗号拼接的歌曲 id，**分批 200 个**）
   → songs[].name / ar[].name / al.name / al.id / dt / id
```

---

## 7. 实测结果（全通）

| 平台 | 歌单 | 标称 | 实解析 |
|---|---|---|---|
| 酷狗 | 1067062 放松轻音 | 20 | **20** |
| 酷狗 | 6222311 随便听听就好了 | 22 | **22** |
| 酷狗 | 4761867 90年代老歌 | 56 | **56** |
| 酷狗 | 短链 `t1.kugou.com/3RB9v52G6V2` = 千寿白喜欢的音乐 | 250 | **250** |
| 网易云 | 7480897649 随便听听 | 123 | **123** |
| 网易云 | 3778678 热歌榜 | 200 | **200** |
| 网易云 | 19723756 飙升榜 | 100 | **100** |

字段完整度：歌名/歌手/时长 **100%**；专辑 98%~100%（酷狗新版无专辑名，见 §6）。

---

## 8. 已知局限

- **酷狗码 `#12345678#` 无公开接口，不支持**
  它是酷狗 **App 内搜索框私有索引**（百度经验/文库确认：App 里「搜索框输入酷狗码 → 结果显示歌单」）。
  web 侧全部通道都试过，均失败：
  - `t1.kugou.com/{code}` → `{"status":0,"data":null}`（不是短链）
  - `t1.kugou.com/api/v1/share/{code}` → 404
  - `m.kugou.com/schain/transfer?chain={code}` → `errcode 33000`
  - `mobilecdn.kugou.com/api/v3/search/special?keyword={code}` → 搜不出目标歌单
  - `mobiles.kugou.com/api/v5/special/info_v2?specialid={code}` → `data:null`（不是 specialid）
  当前行为：**返回明确中文提示**，引导用户改用「分享 → 复制链接」。
- 酷狗 `m.kugou.com/plist/list/{id}/?json=true` 的分页参数不生效（一直返前 10 首）→ **未采用**
- 酷狗分享页 `zlist.html` 的 `dataFromSmarty` 只含**前 100 首**，不能当主路径
- 网易云短链 `163cn.tv` 需联网跟随重定向（`FollowRedirect` 已实现，未用真实短链实测）
- 两家都是非公开接口，**随时可能变** —— 上层需接受「解析失败」并给用户重试入口
- 版权曲可能已下架但仍在歌单里，导入后会有「本地无源」项，建议导入时就标出

---

## 9. 编译

```bash
cl.exe /nologo /utf-8 /std:c++17 /Zc:__cplusplus /permissive- /O2 /EHsc /MD \
  pltest.cpp playlist_provider.cpp /Fe:pltest.exe /link winhttp.lib
```

加入项目：把 `playlist_provider.cpp` 加进 `player_core/CMakeLists.txt` 的源文件列表，
头文件放 `include/`。`winhttp.lib` 项目已链接。
