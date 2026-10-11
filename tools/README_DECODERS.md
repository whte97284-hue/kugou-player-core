# 音频解密与转换核心工具集说明 (Decryption Cores Architecture)

本目录包含转换器涉及的 **三大加密格式** 的底层解码与逆向内核：

---

## 1. 酷狗 KGM / KGMA 解密内核 (KuGou Music)

* **涉及文件**：
  * 原型/工具：`tools/kgm/kgm-dec.exe`，`tools/kgm/kgm.mask`
  * 纯 C++ 翻译落地源文件：`tools/kgm/kgm_decoder.cpp`
  * 流水线集成位置：`player_core/src/audio_ingestion_pipeline.cpp` (`DecryptKgm`)
* **格式魔数与算法机制**：
  * 文件头魔数：前 16 字节为特征识别区（如 `4A F8 46 A5 5C AB A2 32...`），包含 `header_len`、`crypto_ver` 等字段；
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
  * 调度接口：命令行传入待解密文件夹或文件路径：
    ```cmd
    kgg-dec.exe <target_directory>
    ```

---

## 3. 网易云 NCM 解密内核 (NetEase Cloud Music)

* **涉及文件**：
  * 解密工具：`tools/ncm/ncmdump.exe`
  * 纯 C++ 标签解析落地：`player_core/src/tag_mp4_ape_ncm.cpp` (`ParseNcm`)
* **格式结构与算法机制**：
  * 文件头魔数：`CTENFDAM` (8 字节)；
  * 密钥块结构：
    1. **AES 密钥混淆**：使用固定核心密钥 `core_key` 解密出 RC4 密钥串，初始化 256 字节的 RC4 S-Box；
    2. **元数据标签块**：使用 `meta_key` 进行 AES-128 解密，获取包含歌曲名、歌手、专辑、bitrate、duration 的 JSON 字符串；
    3. **封面图片块**：跳过 CRC32(4 字节)与 5 字节 Gap 后，提取内嵌的 JPEG/PNG 封面字节流；
    4. **音频数据流还原**：跳过上述头部后的所有剩余字节，使用之前生成的 RC4 S-Box 进行流式按位异或即可还原为原盘 FLAC 或 MP3。
