import os
import shutil
import sys

# 强制使用 UTF-8 输出
sys.stdout.reconfigure(encoding='utf-8')

base_dir = r"F:\软件\酷狗最新版转换器"
target_dir = os.path.join(base_dir, "KugouConverterWeb", "tools")

# 源文件路径
src_infra = os.path.join(base_dir, "kgg 转ogg mp3 flac", "infra.dll")
src_kgg_dec = os.path.join(base_dir, "kgg 转ogg mp3 flac", "kgg-dec.exe")
src_kgm_mask = os.path.join(base_dir, "kgm kmga转mp3  flac", "kgm转换器", "kgm.mask")

# 目标路径
dst_kgg_dir = os.path.join(target_dir, "kgg")
dst_kgm_dir = os.path.join(target_dir, "kgm")

print(f"Creating directories: {dst_kgg_dir}, {dst_kgm_dir}")
os.makedirs(dst_kgg_dir, exist_ok=True)
os.makedirs(dst_kgm_dir, exist_ok=True)

def safe_copy(src, dst_dir):
    try:
        if not os.path.exists(src):
            print(f"Source file not found: {src}")
            return
        filename = os.path.basename(src)
        dst = os.path.join(dst_dir, filename)
        shutil.copy2(src, dst)
        print(f"Copied {filename} to {dst_dir}")
    except Exception as e:
        print(f"Error copying {src}: {e}")

safe_copy(src_infra, dst_kgg_dir)
safe_copy(src_kgg_dec, dst_kgg_dir)
safe_copy(src_kgm_mask, dst_kgm_dir)

print("Initialization complete.")
