# 内核封面补齐 —— 实现与性能测试报告

**日期**：2026-10-17
**目标**：KGG 解密出的 FLAC 没有封面 → 解密完成后自动补内嵌封面
**结论**：✅ 已实现并集成，端到端实测 3/3 成功，附加耗时 250~450 ms（主要在网络）

---

## 一、实现

### 1.1 新增文件

| 文件 | 行数 | 说明 |
|---|---|---|
| `player_core/include/cover_fetcher.hpp` | 110 | 接口 |
| `player_core/src/cover_fetcher.cpp` | ~560 | 实现（零第三方依赖） |
| `player_core/test/covertest.cpp` | ~150 | 分阶段性能测试 |
| `player_core/test/dtest.cpp` | ~90 | 端到端测试（走真实解密器） |

已注册到 `player_core/CMakeLists.txt`。

### 1.2 核心接口

```cpp
namespace cover_core {
bool IsFlac(const std::wstring& path);
bool FlacHasCover(const std::wstring& path);
bool ReadFlacTags(const std::wstring& path, FlacTags& out);
std::string Md5File(const std::wstring& path);          // 流式,1MB 缓冲
bool EmbedFlacPicture(const std::wstring& path, const std::string& jpeg, int w=0, int h=0, std::string* err=nullptr);
bool FetchOnline(const FlacTags&, const std::wstring& flac_path, CoverResult&, std::string* err=nullptr);
bool EnsureCover(const std::wstring& flac_path, std::string* err=nullptr, std::string* source=nullptr);
}
```

### 1.3 取封面流程（三级匹配，逐级降级）

```
1. 扫块:有 PICTURE → 直接返回(零成本,不联网)
2. 读 Vorbis Comment 取 TITLE / ARTIST
3. 酷狗搜索 (mobilecdn v3, 无需签名/特殊头)
   ├─ 一阶段 size: 本地文件大小 == 候选 sqfilesize   ← 命中率最高,零额外计算
   ├─ 二阶段 md5 : md5(整个FLAC) == 候选 sqhash      ← 酷狗的无损 hash 就是文件 MD5
   └─ 三阶段 歌名: 规范化后全等                       ← 版本不同(码率/母带)时兜底
4. album/info → imgurl(替换 {size}=1080) → 下载 JPEG
5. 酷狗全失败 → 网易云(搜索→详情→picUrl?param=1080y1080,同样按歌名校验)
6. 以 PICTURE 块(type=3 Cover front) 写入 FLAC
```

**为什么必须校验**：同一首歌在酷狗/网易云常有多个版本（原曲 / remix / 单曲EP）。
不做校验会拿到《Something About Us (Love Theme)》的封面而不是《Discovery》的。
`size` 与 `md5` 都能精确锁定同一份文件。

### 1.4 集成点

`player_core/src/decryptor.cpp` → `KggDecryptor::Decrypt()`，在 `AtomicReplaceFile` 落盘之后：

```cpp
cleanup();
// KGG 解出的 FLAC 没有 PICTURE 块 (kgg-dec 只做纯字节流解密)。
// 这里补一次封面:本地算大小/MD5 对酷狗 sqhash,严格匹配优先,歌名兜底。
// 失败**不影响**解密结果 —— 拿不到封面就算了,音频本身是好的。
{
    std::string coverErr, coverSrc;
    cover_core::EnsureCover(r.output, &coverErr, &coverSrc);
}
r.ok = true;
```

**只对 KGG 生效**（KGM / NCM 是 `Native()==true` 的内置解密，产出的文件本来就带封面；
且它们是快速路径，不该被网络请求拖慢）。

---

## 二、性能测试

**环境**：Windows 10 22H2 / i5-13600KF（限 90%）/ RTX 4070 Ti / F: 盘 SSD
**样本**：从 `F:\music test\KugouMusic\` 的 KGG 解密出的 3 个 FLAC，各跑 3 轮取中位数

### 2.1 分阶段耗时（3 轮中位数）

| 文件 | 大小 | 扫块 | 读标签 | 网络 | 嵌入 | 匹配阶段 |
|---|---|---|---|---|---|---|
| small_8MB.flac | 7.98 MB | 0.10 ms | 0.13 ms | **199 ms** | **45 ms** | `size` |
| mid_20MB.flac | 19.92 MB | 0.07 ms | 0.12 ms | **230 ms** | **45 ms** | `size` |
| big_38MB.flac | 36.67 MB | 0.11 ms | 0.12 ms | **289 ms** | **45 ms** | `fuzzy` |

**读**：本地环节（扫块 + 标签）合计 **0.2 ms**，完全可忽略。
**网络占 80%+**，嵌入稳定 45 ms（与文件大小几乎无关）。

### 2.2 嵌入耗时与文件大小的关系

| 文件 | 大小 | 嵌入 |
|---|---|---|
| 8 MB | 7.98 MB | 44~45 ms |
| 20 MB | 19.92 MB | 44~45 ms |
| 38 MB | 36.67 MB | 45~46 ms |

**三者几乎相同** → 45 ms 是**固定开销**（创建临时文件 + 流式复制 + 原子替换），
不是数据搬运瓶颈。1 MB 缓冲下 38 MB 的复制本身只占几毫秒。

### 2.3 MD5 的代价（仅在 size 不中时才花）

| 文件 | 大小 | MD5 | 速率 |
|---|---|---|---|
| 8 MB | 7.98 MB | 12.3 ms | ~1.5 ms/MB |
| 20 MB | 19.92 MB | 29.5 ms | ~1.5 ms/MB |
| 38 MB | 36.67 MB | 55.6 ms | ~1.5 ms/MB |

**线性、约 650 MB/s**。因为一阶段 `size` 命中率很高（同一份文件的 size 必然相同），
**大部分情况根本不会走到 MD5**。实测 3 个样本里 2 个是 `size` 直接命中。

### 2.4 端到端：真实解密路径（`dtest`，走 `DecryptorRegistry`）

| 文件 | 纯 kgg-dec 解密 | 内核解密 + 补封面 | 增量 |
|---|---|---|---|
| t1 (8 MB) | 490 ms | 657~681 ms | **+170~190 ms** |
| dp (20 MB) | 538 ms | 805~920 ms | **+270~380 ms** |
| t2 (38 MB) | 549 ms | 654~1429 ms | **+105~880 ms**（网络波动主导） |

**增量 = 网络(150~400ms) + 嵌入(45ms) + 本地(0.2ms)**，绝对值在 **0.2~0.9 秒**。
t2 第二轮 1429 ms 是网络慢（同一文件第一轮只要 654 ms）→ **网络波动是主要不确定源**。

### 2.5 边界条件

| 场景 | 行为 | 耗时 |
|---|---|---|
| **已有封面** | 直接跳过，**不联网** | 0.05 ms |
| **非 FLAC** | 立即拒绝 | 0 ms |
| 无 TITLE 标签 | 拒绝（无法搜索） | 0.1 ms |
| 搜不到 / 匹配不上 | 返回失败，**不写文件** | 一次网络往返 |
| 封面 > 8 MB | 视为异常，拒绝 | — |
| 写入中途失败 | 删临时文件，**原文件不受损** | — |

### 2.6 安全性（不破坏原文件）

- 先写 `path + ".cover.tmp"`，成功后 `AtomicReplaceFile` 原子替换
- 任一步失败 → 删临时文件，原文件一字未改
- 实测嵌入后 **原有元数据块全部保留**（STREAMINFO / SEEKTABLE / VORBIS_COMMENT / PADDING 都在）
- 新 PICTURE 块正确置 `is_last`，原末块的 `is_last` 被清零

```
嵌入前: STREAMINFO / SEEKTABLE / VORBIS_COMMENT / PADDING(last)
嵌入后: STREAMINFO / SEEKTABLE / VORBIS_COMMENT / PADDING     / PICTURE(last)
        文件大小增量 = PICTURE 块大小 + 4 字节块头（精确相等，已验证）
```

---

## 三、实测结果

| 测试文件 | TITLE / ARTIST | 结果 | 封面来源 | 封面大小 |
|---|---|---|---|---|
| Kan Gao - Born a Stranger (生而陌路) | deep睡眠 | ✅ | kugou `album_id=204312765` (`size`) | 244 KB |
| Daft Punk - Something About Us | Daft Punk | ✅ | kugou `album_id=558653` (`size`) | 208 KB |
| 初音ミク - プレイ (PLAY) | ギガP | ✅ | kugou `album_id=146706109` (`fuzzy`) | 200 KB |

**3/3 成功**，封面均为 1080×1080 JPEG。

---

## 四、已知限制（刻意不做，避免过度设计）

1. **同步阻塞**：补封面在解密线程里同步执行，单个文件多 0.2~0.9 s。
   批量解密 100 首 → 多 20~90 s。**没做异步/队列** —— 调用方本来就在后台线程，
   且失败不影响解密，没必要引入并发复杂度。
2. **无磁盘缓存**：同一首歌重复解密会重新联网。**没做缓存**（可以考虑后续按 sqhash 缓存封面，
   但当前需求是"一次转换一次"）。
3. **只用 v3 接口**：没用 `mobiles.kugou.com`（那需要 `mid`/`dfid`/`clienttime` 三个头，
   v3 不需要）。代价是拿不到 `320hash` 等字段，但封面用不到。
4. **fuzzy 阶段可能拿错版本封面**：只在"歌名规范化全等"时启用，风险可控；
   `detail` 里标了 `/fuzzy` 便于排查。
5. **不处理 MP3/其他格式**：只对 FLAC 生效（KGG 解出来都是 FLAC/OGG）。
   OGG 也解出来过，但 OGG 的封面在 METADATA_BLOCK_PICTURE（Vorbis Comment 里的 base64）
   而不是 PICTURE 块，**当前不支持**。

---

## 五、复现

```bash
# 分阶段性能测试（含每个环节耗时）
covertest.exe "F:/tmp/bench"

# 端到端（真实解密器 + 补封面）
dtest.exe "F:/kpgui/b/player_ui/tools" "F:/tmp/t1.kgg" "F:/tmp/t2.kgg"
```

工具源码：`player_core/test/covertest.cpp` / `player_core/test/dtest.cpp`
辅助脚本：`F:\tmp\strip_cover.py`（剥离 PICTURE 块，用于把样品恢复成"无封面"状态以便重复测试）

> ⚠ **剥离脚本的坑**：删掉末块后必须把**新的末块** `is_last` 置 1，
> 否则解析器会把音频数据当元数据块继续读（第一版脚本就是这样把 16 MB 音频读成了 `t=127` 的非法块）。
