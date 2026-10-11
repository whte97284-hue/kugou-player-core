# 音频解密与转换核心工具集说明 (Decryption Cores Architecture)

本目录包含转换器涉及的 **三大加密格式** 的底层解码与逆向内核：

---

## 1. 酷狗 KGM / KGMA 解密内核 (KuGou Music)

* **涉及文件**：
  * 原型/工具：`tools/kgm/kgm-dec.exe`，`tools/kgm/kgm.mask`
  * 纯 C++ 翻译落地源文件：`tools/kgm/kgm_decoder.cpp`
  * 统一工具接口：`player_core/include/decryptor.hpp` (`DecryptorRegistry` · id `kgm`)
* **格式魔数与算法机制**：
  * 文件头魔数：前 16 字节为特征识别区：KGM/KGMA 为 `7C D5 32 EB 86 02 7F 4B A8 AF A6 8E 0F FF 99 14`，VPR 为 `05 28 BC 96 E9 E4 5A 43 91 AA BD D0 7A F5 36 31`，包含 `header_len`、`crypto_ver` 等字段；
  * 核心逆向算法：基于 `kgm.mask` 掩码文件建立的内存映射表，结合文件头中的 16 字节 `file_key`；
  * 逐字节还原公式：对密文字节先异或 `file_key`，随后将高 4 位与低 4 位分别结合掩码进行半字节混淆逆运算，还原为原生 `fLaC`、`ID3/MP3` 或 `OggS` 裸音频流。

---

## 2. 酷狗 KGG 解密内核 (KuGou Cache Audio)

* **涉及文件**：
  * 可执行程序：`tools/kgg/kgg-dec.exe`
  * 核心运行时依赖：`tools/kgg/infra.dll` (酷狗客户端原厂音频基础库)
* **格式机制与调用原理**：
  * 适用格式：酷狗 PC 端缓存文件 `.kgg`；
  * 解码机制：`kgg-dec.exe` 动态载入 `infra.dll`，调用酷狗官方的解密句柄将本地播放缓存直接转存为 FLAC/MP3/OGG；
  * 统一工具接口：`player_core/src/decryptor.cpp` (`KggDecryptor` · id `kgg`)，源文件先复制到产物目录下的临时工作目录再调用，绝不在曲库目录写入
  * 调度接口：命令行传入待解密文件夹或文件路径：
    ```cmd
    kgg-dec.exe <target_directory>
    ```

---

## 3. 网易云 NCM 解密内核 (NetEase Cloud Music)

* **涉及文件**：
  * 解密工具：`tools/ncm/ncmdump.exe`
  * 纯 C++ 标签解析落地：`player_core/src/tag_mp4_ape_ncm.cpp` (`ParseNcm`)
  * 纯 C++ 音频解密落地：`player_core/src/decryptor.cpp` (`NcmDecryptor` · id `ncm`)，产物自动写回标题 / 歌手 / 专辑 / 封面；`ncmdump.exe` 仅作参考
* **格式结构与算法机制**：
  * 文件头魔数：`CTENFDAM` (8 字节)；
  * 密钥块结构：
    1. **AES 密钥混淆**：使用固定核心密钥 `core_key` 解密出 RC4 密钥串，初始化 256 字节的 RC4 S-Box；
    2. **元数据标签块**：使用 `meta_key` 进行 AES-128 解密，获取包含歌曲名、歌手、专辑、bitrate、duration 的 JSON 字符串；
    3. **封面图片块**：跳过 CRC32(4 字节)与 5 字节 Gap 后，提取内嵌的 JPEG/PNG 封面字节流；
    4. **音频数据流还原**：跳过上述头部后的所有剩余字节，使用之前生成的 RC4 S-Box 进行流式按位异或即可还原为原盘 FLAC 或 MP3。

---

## 统一工具接口 (DecryptorRegistry)

三个内核实现同一个 `Decryptor` 接口并注册到 `DecryptorRegistry::Instance()`：

| id  | 格式               | 实现           | 可用条件                                    |
|-----|--------------------|----------------|---------------------------------------------|
| kgm | .kgm / .kgma / .vpr | 原生 C++       | `tools/kgm/kgm.mask` 存在                    |
| ncm | .ncm               | 原生 C++       | 始终可用                                    |
| kgg | .kgg               | kgg-dec.exe    | Windows + `tools/kgg/kgg-dec.exe` + `infra.dll` |

* 扫描流水线 `SetDecryptPolicy(DecryptPolicy::Ask)` 时只收集加密源 (`IngestionSummary::encrypted_pending`)，由界面询问后再解密；`Auto` 时扫描中直接解密。
* `DecryptIndex` 记录「源文件 (路径 + 大小 + 修改时间) → 产物」，下次扫描直接把产物入库，不重复解密。
* 产物先写 `.part` 临时文件，完成后改名；失败或取消不留半截文件，同名文件存在时自动追加 (2)、(3)。
