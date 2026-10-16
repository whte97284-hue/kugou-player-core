# QQ音乐 `.mgg` / `.mflac`（QMC2）在有 VIP 账号前提下能否转换？

调研日期：2026-10-16
**结论：✅ 能。技术链路完整可行，且有 2026-08 仍在更新的开源实现（Windows/macOS 双平台）。**
但有一个**关键前提**：需要**本机运行 QQ音乐客户端并已登录**（Windows 上凭据只在进程内存里）。

---

## 一、先回答核心问题

| 问题 | 答案 |
|---|---|
| 能转吗？ | **能。** 全链路（取 ekey → 派生密钥 → 解密）已完全公开且可复刻 |
| 必须有 VIP 吗？ | **是。** 加密资源（`.mgg`/`.mflac`）本身就需要付费账号；普通账号取 ekey 恒空 |
| 能纯离线吗？ | **旧版格式能**（QTag/V1，ekey 内嵌文件尾部）；**新版 musicex 不能**，必须联网取 ekey |
| 需要签名吗？ | **不需要`sign`/`zz`**。GetEVkey 走 `authst` 鉴权，不是 sign 鉴权 |
| 最脏的一步是什么？ | **Windows 上从 `QQMusic.exe` 进程内存里抠 `authst`** |

**⚠ 关键前置条件的三种获取 ekey 方式（按推荐度）**：

1. **方案 1（推荐）**：本机 QQ音乐客户端已登录 → 读凭据 → 调 API 自动取 ekey
   - **Windows**：QQ音乐**必须正在运行**（`authst` 仅存在于运行内存中）
   - **macOS**：读 plist 文件即可（**不需要客户端运行**）
2. **方案 2（最省事）**：用 **QQ音乐 ≤ 19.51 老版本**重新下载 → ekey 内嵌在文件尾部 → **纯离线解密**
3. **方案 3**：让用户手动粘贴 ekey（base64）

---

## 二、musicex 尾部格式（新版，✅ 源码验证）

文件尾部 192 字节，**不含 ekey**，只有元数据（这些元数据正是请求 ekey 的入参）：

```
┌─────────────────────────────────────────┐
│   QMC2 加密音频数据（以 "#!Qk" 开头）     │
├─────────────────────────────────────────┤  ← 末尾 192 字节
│  +0x00  [4B]  song_id        (u32 LE)   │
│  +0x04  [4B]  quality_type1  (u32 LE)   │
│  +0x08  [4B]  quality_type2  (u32 LE)   │
│  +0x0C  [60B] media_mid      (UTF-16LE) │  ← 请求 ekey 的 songmid
│  +0x48  [68B] filename       (UTF-16LE) │  ← 如 "O4M0XXXX.mgg"（需带后缀）
│  +0x8C  [48B] padding        (零填充)    │
│  +0xC0  [4B]  footer_size = 0xC0 (LE)   │
│  +0xC4  [4B]  version = 1     (LE)      │
│  +0xC8  [8B]  magic = "musicex\0"       │
└─────────────────────────────────────────┘
```

**检测**：读末尾 8 字节，若为 `"musicex\0"` 即新版 musicex。

**旧版对比**：
- **QTag**：末尾 4 字节 `"QTag"`，前 4 字节（**大端**）是元数据长度，元数据为 `ekey,songID,version,`（逗号分隔）。ekey 是 Base64。
- **V1**：末尾 4 字节（**小端**）= 密钥长度，密钥字节紧邻其前。密钥是明文原始字节。

---

## 三、GetEVkey 接口（✅ 源码验证）

```
POST https://u.y.qq.com/cgi-bin/musicu.fcg
Content-Type: application/json
```

```json
{
  "comm": {
    "authst": "<strAuthst>",
    "ct": "19",
    "cv": "1859",
    "uin": "<QQ_UIN>",
    "tmeLoginType": "3"
  },
  "req_1": {
    "module": "music.vkey.GetEVkey",
    "method": "CgiGetEVkey",
    "param": {
      "filename": ["O4M0XXXXXXXXXXXXXX.mgg"],
      "guid": "10000",
      "songmid": ["<media_mid>"],
      "songtype": [1],
      "uin": "<QQ_UIN>",
      "loginflag": 1,
      "platform": "27",
      "ctx": 1
    }
  }
}
```

**响应**：`req_1.data.midurlinfo[0].ekey`（Base64），`expiration: 80400` 秒（≈22.3 小时）。

### ⚠ 四个硬性约束（踩了就是 `result:104005`）

1. **`filename` 必须带 `.mgg` / `.mflac` 后缀** —— 不带后缀一律 `104005` 且 ekey 空
2. **`songtype` 必须为 `1`** —— 为 0 只回 vkey 不回 ekey
3. **必须用 `GetEVkey` 不能用 `GetVkey`** —— `GetVkey`（`vkey.GetVkeyServer`）只返回播放地址，**不含 ekey**
4. **`platform` 平台区分**：macOS 用 `"20"`，**Windows 用 `"27"`**

### 错误码

| result | 含义 |
|---|---|
| `0` | 成功 |
| `104005` | 需 VIP / 参数错（检查 filename 后缀 + songtype=1） |
| 空响应 | 无权限（未登录 / 非 VIP） |

### ekey 的归属与复用规则（关键，直接影响产品设计）

| 问题 | 答案 | 说明 |
|---|---|---|
| **每首歌的 ekey 一样吗？** | **不一样** | ekey 按 `filename` 逐条签发。请求时传的是 `filename[]` 数组，响应是 `midurlinfo[]` 数组，**一条对一个文件**。（同专辑/同批次偶尔算出相同值，但不可假设） |
| **ekey 会实时刷新吗？** | **不会实时，按小时过期** | 响应里 `expiration: 80400` 秒 ≈ **22.3 小时**。有效期内随便用，过期重新请求一次即可。 |
| **抓一次能复用吗？** | **同文件可复用** | ① 同一批 `filename`：22 小时内反复用同一个 ekey 解同一文件，**结果完全相同**；② 换成新文件：必须重新请求（ekey 与 filename 绑定）。 |
| **别人的加密文件能解吗？** | **取决于「你」而非「文件」** | 见下方专节。 |

### ⚠ ekey 是否绑定「账号」——别人发来的文件能不能解

**准确结论：能不能解，不取决于文件是谁的，取决于你账号对这首歌有没有权限。**

- ekey 的请求入参是 `filename` + `songmid`，但这**两个值就藏在加密文件自己的尾部**（`+0x0C` / `+0x48`），与发送者无关
- 真正的门禁在**服务端签发时的鉴权**：你带着**自己的 `uin` + `authst`** 去请求，服务端会校验**你的账号**对该 `song_id`/`songmid` 是否具备 VIP 播放权限
- 因此：
  - ✅ 别人发你的文件，**你自己账号能播这首歌** → 能签出 ekey → 能解
  - ❌ 你不能播（不是你 VIP 范围内、或是别人的独家/已下架曲目）→ 空响应 → 解不了
- **推论**：QMC2 本质是「**账号锁 + 文件指纹锁**」双重绑定，不是纯文件锁。想做「导入他人分享的加密文件」功能，**前提是你的用户自己也是 VIP 且对该曲有权限**——否则无论文件从哪来都解不开。

**产品化建议**：不要宣传成「任意 mgg 都能转」，应表述为「**用你自己的 VIP 账号解密你能听的歌**」，避免法律和预期双重落差。

**批量化最优姿势**：下载时把整批 `filename` 一次性丢给 `GetEVkey`（一次可传多条），把 `filename → ekey` 存成缓存表，22 小时内反复用，**不要每首歌单独请求**。

---

## 四、凭据从哪来（⚠ 全链路最脏的一步）

### Windows（本项目目标平台）

| 凭据 | 来源 | 说明 |
|---|---|---|
| `uin` | `%APPDATA%\Tencent\QQMusic\QQMusicServiceConfig.ini` | `[Account]` 段 `Uin=` |
| `authst` | **`QQMusic.exe` 进程内存** | 搜 JSON 串 `"authst":"<Base64URL>"` |

**必须客户端运行**。提取要点（源码级）：
- `OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ)`
- `VirtualQueryEx` 遍历地址空间，只筛 `MEM_COMMIT | MEM_PRIVATE | MEM_IMAGE`（跳过 `MEM_MAPPED`）
- `ReadProcessMemory` 以 1MB 分块搜 `"authst":"` pattern
- **⚠ 大坑**：`MEMORY_BASIC_INFORMATION` 结构在 **Windows 10 1803+ 多了 2 字节 `PartitionId`**（在 `AllocationProtect` 和 `RegionSize` 之间）。漏掉它会导致 `VirtualQueryEx` 读到的 `state` 实际是 `protect` 值（`0x04` 而非 `0x1000`）→ **所有内存区域被跳过，什么都搜不到**。
- 实测 QQMusic.exe 扫描约 **0.5 秒**

### macOS（参考）

`~/Library/Containers/com.tencent.QQMusicMac/Data/Library/Preferences/com.tencent.QQMusicMac.plist`
→ `AutoLoginUserInfo`（NSKeyedArchiver）→ `nUserId`(uin) + `strAuthst`(authst)。
**持久化，不需要客户端运行。**

---

## 五、拿到 ekey 后怎么解密（✅ 源码验证）

### 第一步：ekey 解析与派生

```
1. Base64 解码 ekey
2. 若以 "QQMusic EncV2,Key:" 开头（EncV2）:
   - TC-TEA 解密（key1 = "386ZJY!@#*$%^&)("）
   - TC-TEA 再解密（key2 = "**#!(#$%&^a1cZ,T"）
   - Base64 解码 → 得 EncV1
3. EncV1 解析：前 8 字节为头部，用它派生 TEA key（simpleMakeKey(106,8)）
   → TC-TEA 解密主体
4. 若 TC-TEA 失败（API 直接给的原始密钥）→ 直接拿整个解码字节当密钥
5. 最终密钥 = 头部 + 解密主体
```

### 第二步：按密钥长度选流密码

```
if (key.length > 300) → 改造 RC4
else                 → Map XOR
```

**Map XOR**（key ≤ 300）：
```
idx    = (i*i + 71214) % key_len
mask   = scramble(key[idx], idx)
scramble(v, idx) = (v rotate_left ((idx+4)&7)) | (v rotate_right ((idx+4)&7))
buf[i] ^= mask
```

**改造 RC4**（key > 300）：
- 前 128 字节（第一段）用直接密钥查找 + 段密钥计算
- 其余以 **5120 字节**为一块分段，**每段根据段 ID 重算丢弃计数并重新初始化 RC4 状态**
- 与标准 RC4 的差异：分段 + 每段重置

### TC-TEA 与标准 TEA 的差异

- 标准 TEA 64 轮 → **TC-TEA 是 32 轮**（16 轮 Feistel）
- delta 仍是 `0x9e3779b9`
- **CBC 模式是腾讯自定义的**（手工 IV 前向 XOR）
- 密文格式：`PadLen(1) + Padding(0~7) + Salt(2) + Body + Zero(7)`

---

## 六、工作量评估

| 组件 | 说明 | 估算行数 |
|---|---|---|
| TC-TEA（含自定义 CBC） | 自研，无第三方 | ~60 行 |
| Map XOR | 自研 | ~30 行 |
| 改造 RC4 | 自研（分段 + 重置） | ~90 行 |
| Base64 | 自研 | ~70 行 |
| musicex / QTag / V1 尾部解析 | 自研 | ~120 行 |
| GetEVkey HTTP 调用 | 复用现成 WinHTTP | ~80 行 |
| Windows 凭据提取（进程内存扫描） | **⚠ 最麻烦的一块** | ~200 行 |

**总计约 650 行 C++**，不含 UI。

### 难度分级

| 部分 | 难度 |
|---|---|
| QMC1（`.qmc*`） | ⭐ 白捡（40 行） |
| 旧版 QMC2（QTag/V1，离线） | ⭐⭐⭐ 中等（~350 行） |
| **musicex + 联网取 ekey** | ⭐⭐⭐⭐ 较难（**~650 行**，主要是进程内存扫描） |

### 预计工期
- 只做旧版离线：**2~3 天**
- 完整做 musicex 联网：**1~2 周**

---

## 七、风险和限制（诚实评估）

1. **必须 VIP 账号**且对目标歌曲有权限 —— 否则 ekey 恒空
2. **Windows 必须运行 QQ音乐客户端** —— 这是最大的用户体验负担。且需要跨进程读内存（杀软可能报毒）
3. **凭据约 22 小时过期** —— 需重新提取
4. **必须先有加密文件才能转** —— 用户得先用 QQ音乐下载（可能需付费购买）
5. **算法可能变更** —— 腾讯已从 QMC1 → QMC2 演进两次；`ct`/`cv` 参数绑定客户端版本，升级后可能失效
6. **⚠ 版权**：解密 VIP 加密音乐本质是绕过 DRM。原 `unlock-music` GitHub 仓库 2022-11 遭 DMCA 下架。参考项目 `ownlight6/qmc-decoder` 是 **GPL-3.0**（传染性，不能直接抄进闭源项目）

### 法律提示
本调研仅作技术可行性分析。**是否在产品中集成需自行评估法律风险。**

---

## 八、参考实现

| 项目 | 平台 | 取 ekey 方式 | 许可 | 最后更新 |
|---|---|---|---|---|
| **ownlight6/qmc-decoder** | Win+mac | 本机客户端凭据 + API | **GPL-3.0** | **2026-08** |
| unlock-music/cli | Win+mac | 读客户端 MMKV/本地 | MIT | 2025-05 |
| strelitzia-reg/qqmusic-decryptor | Win | **Frida hook 客户端**（完全绕开 ekey） | MIT | 2026-01 |
| bczhc/qmc-decrypt | 跨平台 | 用户手动传 ekey | MIT/Apache | 2026-01 |
| jixunmoe/qmc2-crypto | 库 | — | MIT | 已迁至 git.unlock-music.dev |
| MCQTSS_QQMusic | — | 含 sign 算法（本项目不需要） | 见仓库 | — |

**关键文档**：
- `github.com/ownlight6/qmc-decoder/blob/main/MGG_DECRYPTION_FLOW.md` —— **最完整的端到端流程**
- `github.com/ownlight6/qmc-decoder/blob/main/TECHNICAL_DETAILS.md` —— 算法细节

---

## 九、给 Claude 的建议

**如果要支持 `.mgg`/`.mflac`，推荐分两步：**

### 第一步（低成本高收益）：只做**旧版离线**路径
- 支持 QTag / V1（ekey 内嵌尾部）
- 纯离线，~350 行，2-3 天
- **先实现"检测尾部类型"**：如果发现是 musicex，明确提示用户「需登录 VIP + 运行客户端」或「换老版本客户端重新下载」

### 第二步（按需）：加 musicex 联网路径
- 需要实现：进程内存扫描（最麻烦）+ GetEVkey 调用 + 完整 QMC2 派生
- **强烈建议先确认产品定位**：如果播放器本身是"本地优先、零联网"，那这条路与定位冲突

### ⚠ 无论哪步，都必须注意
- `GetEVkey` 的 `platform` 参数：**Windows 用 `"27"`**
- `filename` 必须带 `.mgg`/`.mflac` 后缀
- `songtype` 必须 `1`
- 用 `GetEVkey` 不是 `GetVkey`
- 参考项目是 **GPL-3.0**，**算法可以学，代码不能抄**（本项目要求自研零依赖）
