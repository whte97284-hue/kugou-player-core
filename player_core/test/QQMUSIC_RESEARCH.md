# QQ音乐 支持可行性调研（给 Claude）

调研日期：2026-10-16
调研范围：① 加密音频格式 ② 歌单导入 ③ 落地建议
**标注 [实测] 的结论本机已跑通，可直接采信；[调研] 来自公开资料，需自行复核。**

---

## 一、结论速览

| 议题 | 结论 | 可行性 |
|---|---|---|
| **歌单导入** | 有**匿名免签名**接口，`uin:""` 即可取公开歌单 | ✅ **推荐做，[实测] 已通** |
| **`.qmc*` 解密**（qmc0/qmc2/qmc3/qmcflac/qmcogg/tkm） | 纯 XOR + 常量表，算法完全公开 | ✅ 可自研（注意法律） |
| **`.mflac/.mgg` 旧版**（QTag/V1） | 密钥内嵌文件尾部，可离线提取 | ⚠️ 中等，可做 |
| **`.mflac/.mgg` 新版**（musicex） | 密钥**不内嵌**，须联网调 API + 客户端凭据 | ❌ **本地播放器不可行** |

**一句话**：**歌单导入做着划算**（复用现有 `playlist_provider` 架构，无签名、无第三方依赖）；**解密则只有老格式 `.qmc*` 值得做**，新版 `.mflac/.mgg` 拿不到密钥。

---

## 二、歌单导入协议 [实测 已通]

### 2.1 URL 形态

| 形态 | 示例 |
|---|---|
| PC 歌单页 | `https://y.qq.com/n/ryqq/playlist/{id}` |
| 新版 PC 页 | `https://y.qq.com/n/ryqq_v2/playlist/{id}` |
| H5 分享页 | `https://i.y.qq.com/n2/m/share/details/taoge.html?id={id}` |
| **短链** | `https://c6.y.qq.com/base/fcgi-bin/u?__=lawcqar2MNu0` |

- **歌单 ID 是纯数字**（约 10 位，如 `9713180394`）
- `dissid` / `disstid` / `dirid` 是同一 ID 的不同字段名，**接口参数统一用 `disstid`**
- **短链展开**：`c6.y.qq.com/base/fcgi-bin/u?__=xxx` → **[实测] 302 × 2 跳** → `y.qq.com/n/ryqq_v2/playlist/{id}`。正则抓 `playlist/(\d+)` 即可
- **分享文案**：`分享歌单 《烛龙心经》https://c6.y.qq.com/base/fcgi-bin/u?__=xxx @QQ音乐`
  → 和酷狗一样，**先抠出第一个 URL 再识别**（防尾部中文干扰）

### 2.2 主接口（推荐）[实测]

```
POST https://u.y.qq.com/cgi-bin/musicu.fcg
Content-Type: application/json
Referer: https://y.qq.com/
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) ... Chrome/120.0 Safari/537.36
```

Body（`song_begin` 从 0 递增，每页 `song_num=30`）：

```json
{
  "comm": {
    "g_tk": 5381, "uin": "", "format": "json",
    "inCharset": "utf-8", "outCharset": "utf-8",
    "notice": 0, "platform": "h5", "needNewCode": 1, "ct": 24, "cv": 0
  },
  "req_0": {
    "module": "music.srfDissInfo.aiDissInfo",
    "method": "uniform_get_Dissinfo",
    "param": {
      "disstid": 9713180394, "enc_host_uin": "",
      "tag": 1, "userinfo": 1,
      "song_begin": 0, "song_num": 30
    }
  }
}
```

**关键结论**：
- **[实测] `uin:""` 匿名即可，返回 `code:0`** —— 公开歌单**不需要 cookie、不需要签名**
- **[实测] 不需要任何签名**（`musicu.fcg` 是明文 JSON 网关；只有新版 `musics.fcg` 才强制 `sign`）
- 返回解析路径：
  - `req_0.data.dirinfo.title` → 歌单名（实测「烛龙心经」）
  - `req_0.data.dirinfo.songnum` → 总数（实测 20）
  - `req_0.data.dirinfo.picurl` / `host_nick` → 封面 / 创建者
  - `req_0.data.songlist[]` → 曲目数组
- **分页**：`song_begin` 每次 += 30，直到累计 ≥ `dirinfo.songnum`

### 2.3 曲目字段 [实测]

```json
{
  "name": "高维回声",
  "title": "高维回声",
  "mid": "001nFCKq3qM9GH",          // songmid
  "interval": 285,                   // ⚠ 单位是【秒】
  "singer": [ { "name": "大渔&海棠", "mid": "...", "id": 0 } ],
  "album": { "name": "《烛龙之睛》主题曲", "mid": "...", "id": 0 },
  "file": {
    "media_mid": "003Q2mIH3K6gi9",   // strMediaMid
    "size_128mp3": ..., "size_320mp3": 11441844,
    "size_flac": 26589839, "size_192ogg": ..., "size_hires": ...
  }
}
```

**「歌名 + 歌手 + 时长 + 指纹」所需字段全齐** ✅
对比酷狗：QQ 的 `interval` 是**秒**（酷狗老接口是毫秒、新接口 song_v2 是秒）。

### 2.4 兜底接口（老，不推荐）

```
GET https://c.y.qq.com/qzone/fcg-bin/fcg_ucc_getcdinfo_byids_cp.fcg
    ?type=1&json=1&utf8=1&onlysong=0&disstid={id}&g_tk=5381
    &loginUin=0&hostUin=0&format=json&inCharset=utf8&outCharset=utf-8
    &notice=0&platform=yqq.json&needNewCode=0
```
- **⚠ 必带 `Referer: https://y.qq.com/`**，否则被拒
- **⚠ 只返回前 30 首**（已知坑），全量必须换 2.2 的分页
- 路径：`cdlist[0].songlist[]`，歌单名 `cdlist[0].dissname`，总数 `cdlist[0].songnum`

### 2.5 WinHTTP 兼容性 [实测]

- `u.y.qq.com` / `c.y.qq.com` / `i.y.qq.com` / `c6.y.qq.com` **共用同一张证书**
  （SAN 含 `*.y.qq.com`，`CN=y.qq.com`）→ **hostname 匹配，不会触发 12175** ✅
- **必须 https**（公网接口无 http 明文）
- Referer 需在 `WinHttpOpenRequest` 第 5 参直接传（**不能**先传 `WINHTTP_NO_REFERER` 再 AddRequestHeaders —— 已知无效）
- **不需要 MD5/SHA**（免签名）
- 短链需跟随 302（复用现成的 `FollowRedirect`）

### 2.6 参考项目（均 MIT）

| 项目 | 语言 | 说明 |
|---|---|---|
| jsososo/QQMusicApi | Node | 全套 musicu.fcg 封装 |
| Rain120/qq-music-api | Node | `getSongListDetail?disstid=` |
| mos9527/pyncm | Python | QQ + 网易统一网关 |
| Bistutu/GoMusic | Go | 用 legacy fcg，issue #35 记录了 30 首分页坑 |

---

## 三、加密格式

### 3.1 格式清单

| 后缀 | 原始容器 | 代际 |
|---|---|---|
| `.qmc0` / `.qmc3` | MP3 | QMC1 |
| `.qmcflac` | FLAC | QMC1 |
| `.qmcogg` / `.qmc2` | OGG | QMC1/QMC2 |
| `.tkm` | MP3 | QMC1 变体 |
| `.mflac` / `.mflac0` | FLAC | QMC2 |
| `.mgg` / `.mgg1` / `.mggl` | OGG | QMC2 |

`.mflac0` / `.mgg1` 只是 **QMC2 的容器变体**，加密机制与 `.mflac`/`.mgg` 相同。

### 3.2 QMC1（`.qmc*`）—— 容易，可自研

纯静态算法，**每字节 XOR**：

```
v = offset + i
if (v > 0x7FFF) v %= 0x7FFF
buf[i] ^= key[(v * v + 80923) % 256]
```

- `key` = 256 字节**硬编码表**（首 8 字节 `77 48 32 73 DE F2 C0 C8`）
- 无头部魔数，整个文件即密文流
- 不同后缀用不同表（map0/map2/map3/map6…）

### 3.3 QMC2 旧版（QTag / V1）—— 中等

1. **EKey 内嵌文件尾部**，可离线提取
   - QTag：`[密文][EKey(Base64)][song_id]["QTag"]`
   - V1：`[密文][EKey 原始字节][key_size(4B)]`
2. 密钥推导：**TC-TEA**（腾讯 TEA 变体，16 轮 CBC）
3. 应用解密：按 **key 长度 300 字节阈值**分流
   - `≤300` → Map XOR（`(v<<r)|(v>>r)` 非循环移位变体）
   - `>300` → 改造 RC4（skip 改 `%len(key)`）

### 3.4 QMC2 新版（musicex）—— 困难，**不可行**

- 尾部是 `musicex\0` + **192 字节 footer**，含 `song_id` / `media_mid` / `filename`，**但不含密钥**
- EKey 必须**联网调 `GetEVkey` API**，需要客户端 `uin` / `authst` 凭据
- EKey 有效期约 **22 小时**
- **结论：本地播放器无法离线支持**（除非要求用户提供凭据并联网，与项目「自包含零依赖」原则冲突）

### 3.5 ⚠ 法律提示

- 所有算法均源自对 QQ音乐客户端的**逆向工程**
- 原 `unlock-music` GitHub 仓库 **2022-11 遭 DMCA 下架**（现存活于 `git.unlock-music.dev`，MIT 许可证）
- 对开源播放器项目，**建议最多实现 QMC1**（争议最小）；QMC2 及 musicex 有明确版权风险

### 3.6 参考

- unlock-music / um-react（MIT）：`git.unlock-music.dev`
- qmcdump（含 mapL 源码）
- Kugo-Music-Converter（含 RC4/TEA 重写，带 6 组真实样本回归）
- 原理详解：`baijiahao.baidu.com/s?id=1873937100765265584`

---

## 四、给 Claude 的落地建议

### 4.1 歌单导入（建议做）

在 `playlist_provider` 里加 `FetchQQMusic()`，与现有 `FetchKugouGcid` 同构：

1. **`ParseInput`**：加 QQ 分支
   - 识别 `y.qq.com/n/ryqq*/playlist/{id}`、`i.y.qq.com/...taoge.html?id={id}`
   - **短链** `c6.y.qq.com/base/fcgi-bin/u?__=xxx` → 加重定向跟随（复用 `FollowRedirect`）
   - 抠 URL 逻辑已有（本轮刚加），直接复用
2. **`FetchQQMusic(id)`**：
   - POST `musicu.fcg`，`song_begin` 分页拉全量
   - `interval` **秒 → 毫秒 ×1000**
   - 先建 GET 再建 POST 通道（现有 `HttpGetFn` **只支持 GET**，需要扩展成支持 POST body）
3. **`DefaultHttpGet`**：
   - 加 `Referer: https://y.qq.com/`（QQ 也走第 5 参）
   - QQ 域名证书正常，无需特殊处理
4. **平台枚举**：加 `Platform::QQMusic`
5. **测试**：`test_playlist_host.cpp` 加 QQ 用例；参考 `9713180394`（20 首）

**待验证项**（我做调研时没跑）：
- `song_num` 上限（30 可用，更大值未测）
- 超长歌单（>1000 首）分页稳定性
- `ct`/`cv` 具体值是否影响（`ct:24, cv:0` 实测有效）

### 4.2 解密（谨慎）

- 若要做，**只做 `.qmc*`（QMC1）**：纯 XOR + 常量表，与项目「零依赖」契合
- `.mflac/.mgg` 新旧版都有法律/技术问题，**建议明确不支持**
- 若决定做，需在 UI 上说明「仅支持旧格式」，避免用户误以为全支持

---

## 五、关键差异对照（vs 已有的酷狗/网易云）

| 维度 | 酷狗 | 网易云 | **QQ音乐** |
|---|---|---|---|
| 签名 | H5 `MD5(KEY + k=v + KEY)` | 无 | **无**（musicu.fcg 明文） |
| 请求方式 | GET | GET/POST | **POST（JSON body）** ← 需扩展 |
| 密钥常量 | `NVPh5oo...` | 无 | **无** |
| 时长单位 | ms（老）/ 秒（song_v2） | ms | **秒** |
| 证书 | 部分域名 12175（用 http） | 正常 | **正常** |
| 特殊请求头 | `mid`/`dfid`/`clienttime` | `Referer` | **`Referer`**（主接口可省） |
| 分页 | page/pagesize(≤300) | 分批 200 | **song_begin/song_num(30)** |

**最大新增点：项目现有 `HttpGetFn` 只支持 GET，QQ 需要 POST（带 JSON body）。这是主要改造项。**
