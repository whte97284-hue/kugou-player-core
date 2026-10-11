#!/usr/bin/env bash
# 跨平台 (Linux/macOS/WSL) 内核自测:真实样本解析对照 + 入库/检索集成测试 + Sanitizer 模糊测试
# 依赖: g++ (C++17), ffmpeg/ffprobe, python3 + pip install mutagen pillow cryptography
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=${1:-build/host}
mkdir -p "$OUT"
CORE="src/platform_io.cpp src/tag_common.cpp src/tag_id3_mpeg.cpp src/tag_flac_ogg.cpp src/tag_riff_dsd.cpp
      src/tag_mp4_ape_ncm.cpp src/audio_tag_parser.cpp src/cue_sheet.cpp src/music_catalog.cpp src/song_cleaner.cpp
      src/pinyin_table.cpp src/audio_ingestion_pipeline.cpp src/scan_cache.cpp"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"

echo "== 生成真实样本 (ffmpeg + mutagen)"
python3 tests/host/make_fixtures.py "$OUT/fx"

echo "== 编译 (ASan + UBSan)"
g++ -std=c++17 $SAN -pthread tests/host/tag_probe.cpp $CORE -o "$OUT/tag_probe"
g++ -std=c++17 $SAN -pthread tests/host/test_core_host.cpp $CORE -o "$OUT/core_host"
g++ -std=c++17 $SAN -pthread tests/host/fuzz_parser.cpp $CORE -o "$OUT/fuzz"
g++ -std=c++17 -O2 -pthread tests/host/test_core_host.cpp $CORE -o "$OUT/core_host_o2"

echo "== 1. 解析对照 (ffprobe 为基准)"
python3 tests/host/check_fixtures.py "$OUT/tag_probe" "$OUT/fx"
echo "== 2. 入库 / 曲库 / 检索 / CUE / KGM 集成测试"
"$OUT/core_host" "$OUT/fx" "$OUT/work"
echo "== 3. 吞吐压测 (O2)"
"$OUT/core_host_o2" "$OUT/fx" "$OUT/work" 165 | sed -n '/\[5\]/,$p'
echo "== 4. 变异模糊测试"
"$OUT/fuzz" "${FUZZ_ITERS:-500}" $(ls "$OUT"/fx/* | grep -v manifest)
