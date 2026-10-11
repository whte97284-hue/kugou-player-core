import os
import sys
import shutil
import subprocess
import logging
import time
import re
import json
import hashlib
from datetime import datetime
from flask import Flask, render_template, request, jsonify, send_from_directory

# PyInstaller 打包检测
if getattr(sys, 'frozen', False):
    # 打包后的路径
    BASE_DIR = sys._MEIPASS
    # 可写目录使用 exe 所在目录
    WRITABLE_DIR = os.path.dirname(sys.executable)
else:
    # 开发模式
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))
    WRITABLE_DIR = BASE_DIR

app = Flask(__name__, 
            template_folder=os.path.join(BASE_DIR, 'templates'),
            static_folder=os.path.join(BASE_DIR, 'static'))

# 配置路径
TOOLS_DIR = os.path.join(BASE_DIR, 'tools')
UPLOAD_FOLDER = os.path.join(WRITABLE_DIR, 'uploads')
OUTPUT_FOLDER = os.path.join(WRITABLE_DIR, 'output')
CONFIG_FILE = os.path.join(WRITABLE_DIR, 'config.json')


# 创建必要目录
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
os.makedirs(OUTPUT_FOLDER, exist_ok=True)
os.makedirs(TOOLS_DIR, exist_ok=True)

# 配置日志
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# ==================== 配置管理 ====================
DEFAULT_CONFIG = {
    'output_folder': OUTPUT_FOLDER,
    'watch_enabled': False,
    'watch_folder': '',
    'processed_files': {},  # {文件哈希: {name, time, result}}
    'auto_process_log': []  # 自动处理日志
}

def load_config():
    """加载配置文件"""
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
                config = json.load(f)
                # 合并默认配置
                for key in DEFAULT_CONFIG:
                    if key not in config:
                        config[key] = DEFAULT_CONFIG[key]
                return config
        except Exception as e:
            logger.error(f"加载配置失败: {e}")
    return DEFAULT_CONFIG.copy()

def save_config(config):
    """保存配置文件"""
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            json.dump(config, f, ensure_ascii=False, indent=2)
    except Exception as e:
        logger.error(f"保存配置失败: {e}")

def get_file_hash(filepath):
    """生成文件唯一标识（基于文件名+大小+修改时间）"""
    try:
        stat = os.stat(filepath)
        name = os.path.basename(filepath)
        identifier = f"{name}_{stat.st_size}_{int(stat.st_mtime)}"
        return hashlib.md5(identifier.encode()).hexdigest()
    except:
        return None

# 加载配置
app_config = load_config()
OUTPUT_FOLDER = app_config.get('output_folder', OUTPUT_FOLDER)

# 尝试自动定位工具路径 (如果 tools 目录没有，可以尝试去上一级目录找)
# 这里假设用户会把工具复制到 tools 目录，或者我们通过代码去调度上一级的工具
# 为了方便，我们尽量使用绝对路径引用
PARENT_DIR = os.path.dirname(BASE_DIR)

# 工具路径映射
TOOL_PATHS = {
    'kgg': {
        'exe': os.path.join(TOOLS_DIR, 'kgg', 'kgg-dec.exe'),
        'cwd': os.path.join(TOOLS_DIR, 'kgg')
    },
    'kgm': {
        # KGM 解码工具 - 统一放在 tools/kgm 目录
        'exe': os.path.join(TOOLS_DIR, 'kgm', 'kgm-dec.exe'),
        'mask': os.path.join(TOOLS_DIR, 'kgm', 'kgm.mask'),
        'cwd': os.path.join(TOOLS_DIR, 'kgm')
    },
    'ncm': {
        # NCM 解码工具 - 网易云音乐格式
        'exe': os.path.join(TOOLS_DIR, 'ncm', 'ncmdump.exe'),
        'cwd': os.path.join(TOOLS_DIR, 'ncm')
    }
}

# ----------------- 辅助函数 -----------------

def safe_filename(filename):
    """
    保留中文字符的安全文件名处理
    只移除危险字符，保留中文、日文、字母、数字等
    """
    # 移除路径分隔符和其他危险字符
    dangerous_chars = r'[<>:"/\\|?*]'
    filename = re.sub(dangerous_chars, '_', filename)
    # 移除开头结尾的空格和点
    filename = filename.strip('. ')
    return filename if filename else 'unnamed'

def move_file_with_retry(src, dst, max_retries=5, delay=0.5):
    """
    带重试的文件移动，处理 Windows 文件锁定问题
    """
    for i in range(max_retries):
        try:
            if os.path.exists(dst):
                os.remove(dst)
            shutil.move(src, dst)
            return True
        except PermissionError:
            if i < max_retries - 1:
                time.sleep(delay)
            else:
                raise
    return False

# ----------------- 核心转换逻辑 -----------------

def process_kgm(input_path, output_dir):
    """
    使用 IXarea Unlock Music (酷狗kgm转换.exe) 进行 KGM/KGMA 解密
    该工具会扫描当前工作目录下的所有 kgm/kgma 文件，输出到 kgm-vpr-out 子目录
    注意：该工具处理完成后会等待用户按 Enter 退出，需要通过 stdin 发送换行符
    """
    tool = TOOL_PATHS['kgm']
    
    if not os.path.exists(tool['exe']):
        return False, f"KGM转换工具未找到: {tool['exe']}"
    
    if not os.path.exists(tool['mask']):
        return False, f"kgm.mask 未找到: {tool['mask']}"
    
    # 创建临时工作目录
    temp_dir = os.path.join(BASE_DIR, 'temp_kgm_process')
    temp_out_dir = os.path.join(temp_dir, 'kgm-vpr-out')
    
    try:
        # 清理并创建目录
        if os.path.exists(temp_dir):
            shutil.rmtree(temp_dir)
        os.makedirs(temp_dir, exist_ok=True)
        
        # 复制 mask 文件和待转换文件到临时目录
        shutil.copy2(tool['mask'], os.path.join(temp_dir, 'kgm.mask'))
        
        filename = os.path.basename(input_path)
        temp_input = os.path.join(temp_dir, filename)
        shutil.copy2(input_path, temp_input)
        
        logger.info(f"开始处理 KGM/KGMA 文件: {filename}")
        
        # 运行转换工具 (使用 Popen 以便控制 stdin)
        # 该工具处理完成后会显示 "按Enter退出……"，需要发送换行符
        process = subprocess.Popen(
            [tool['exe']],
            cwd=temp_dir,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        
        # 等待处理完成（给一些时间让它处理文件）
        # 然后发送 Enter 让程序退出
        time.sleep(2)  # 等待处理
        process.stdin.write(b'\n')
        process.stdin.flush()
        
        try:
            stdout, stderr = process.communicate(timeout=120)
            logger.info(f"工具输出: {stdout.decode('gbk', errors='ignore')}")
            if stderr:
                logger.warning(f"工具错误: {stderr.decode('gbk', errors='ignore')}")
        except subprocess.TimeoutExpired:
            process.kill()
            return False, "转换超时"
        
        # 等待一下确保文件被释放
        time.sleep(0.5)
        
        # 检查输出目录
        success = False
        if os.path.exists(temp_out_dir):
            for f in os.listdir(temp_out_dir):
                src = os.path.join(temp_out_dir, f)
                dst = os.path.join(output_dir, f)
                try:
                    move_file_with_retry(src, dst)
                    logger.info(f"输出文件: {f}")
                    success = True
                except Exception as e:
                    logger.error(f"移动文件失败: {e}")
        
        # 清理临时目录
        if os.path.exists(temp_dir):
            shutil.rmtree(temp_dir)
        
        if success:
            return True, "解密成功"
        else:
            return False, "未生成输出文件，可能文件格式不支持"
            
    except Exception as e:
        logger.error(f"KGM处理异常: {str(e)}")
        # 尝试清理
        if os.path.exists(temp_dir):
            try:
                shutil.rmtree(temp_dir)
            except:
                pass
        return False, str(e)

# KGG 处理 - 需要本机下载并播放过的文件才能解密
def process_kgg(input_path, output_dir):
    """
    使用 kgg-dec.exe 解密 KGG 文件
    注意：KGG 文件必须是在本机下载并至少播放过一次才能解密
    因为解密依赖于酷狗在本地生成的 ekey 缓存
    """
    # 使用原始工具目录（确保有正确的 infra.dll）
    original_tool_dir = os.path.join(PARENT_DIR, 'kgg 转ogg mp3 flac')
    exe_path = os.path.join(original_tool_dir, 'kgg-dec.exe')
    
    if not os.path.exists(exe_path):
        # 回退到 tools 目录
        exe_path = TOOL_PATHS['kgg']['exe']
        original_tool_dir = TOOL_PATHS['kgg']['cwd']
    
    if not os.path.exists(exe_path):
        return False, "kgg-dec.exe 未找到"
    
    # 创建临时处理目录
    temp_dir = os.path.join(BASE_DIR, 'temp_kgg_process')
    
    try:
        # 清理并创建目录
        if os.path.exists(temp_dir):
            shutil.rmtree(temp_dir)
        os.makedirs(temp_dir, exist_ok=True)
        
        # 复制必需文件到临时目录
        infra_dll = os.path.join(original_tool_dir, 'infra.dll')
        if os.path.exists(infra_dll):
            shutil.copy2(infra_dll, os.path.join(temp_dir, 'infra.dll'))
        
        shutil.copy2(exe_path, os.path.join(temp_dir, 'kgg-dec.exe'))
        
        filename = os.path.basename(input_path)
        temp_input = os.path.join(temp_dir, filename)
        shutil.copy2(input_path, temp_input)
        
        logger.info(f"开始处理 KGG 文件: {filename}")
        
        # 运行解密工具
        result = subprocess.run(
            [os.path.join(temp_dir, 'kgg-dec.exe'), '.'],
            cwd=temp_dir,
            capture_output=True,
            text=True,
            timeout=60
        )
        
        output_text = (result.stdout or '') + (result.stderr or '')
        logger.info(f"KGG工具输出: {output_text}")
        
        # 检查是否有 ekey 错误
        if 'ekey not found' in output_text:
            shutil.rmtree(temp_dir)
            return False, "ekey 未找到：该文件可能未在本机播放过，或 infra.dll 不正确"
        
        # 寻找生成的文件 (以 _kgg-dec 结尾)
        success = False
        time.sleep(0.5)  # 等待文件写入完成
        
        for f in os.listdir(temp_dir):
            if '_kgg-dec' in f:
                src = os.path.join(temp_dir, f)
                dst = os.path.join(output_dir, f)
                try:
                    move_file_with_retry(src, dst)
                    logger.info(f"输出文件: {f}")
                    success = True
                except Exception as e:
                    logger.error(f"移动文件失败: {e}")
        
        # 清理临时目录
        if os.path.exists(temp_dir):
            shutil.rmtree(temp_dir)
        
        if success:
            return True, "解密成功"
        else:
            return False, "未生成输出文件，请确保：1) 文件在本机下载 2) 已在酷狗播放过"
            
    except subprocess.TimeoutExpired:
        return False, "转换超时"
    except Exception as e:
        logger.error(f"KGG处理异常: {str(e)}")
        if os.path.exists(temp_dir):
            try:
                shutil.rmtree(temp_dir)
            except:
                pass
        return False, str(e)

# NCM 处理 - 网易云音乐格式
def process_ncm(input_path, output_dir):
    """
    使用 ncmdump.exe 解密 NCM 文件
    NCM 是网易云音乐的加密格式
    """
    tool = TOOL_PATHS['ncm']
    
    if not os.path.exists(tool['exe']):
        return False, f"ncmdump.exe 未找到: {tool['exe']}"
    
    try:
        filename = os.path.basename(input_path)
        logger.info(f"开始处理 NCM 文件: {filename}")
        
        # ncmdump 用法: ncmdump.exe -o <输出目录> <输入文件>
        result = subprocess.run(
            [tool['exe'], '-o', output_dir, input_path],
            cwd=tool['cwd'],
            capture_output=True,
            text=True,
            timeout=120
        )
        
        output_text = (result.stdout or '') + (result.stderr or '')
        logger.info(f"NCM工具输出: {output_text}")
        
        # 检查是否成功生成了文件
        # ncmdump 会生成同名但扩展名为 .mp3 或 .flac 的文件
        base_name = os.path.splitext(filename)[0]
        success = False
        
        for ext in ['.mp3', '.flac']:
            output_file = os.path.join(output_dir, base_name + ext)
            if os.path.exists(output_file):
                logger.info(f"输出文件: {base_name + ext}")
                success = True
                break
        
        if success:
            return True, "解密成功"
        else:
            return False, "未生成输出文件，可能文件格式不支持或已损坏"
            
    except subprocess.TimeoutExpired:
        return False, "转换超时"
    except Exception as e:
        logger.error(f"NCM处理异常: {str(e)}")
        return False, str(e)


# ----------------- Flask Routes -----------------

# ==================== 设置 API ====================
@app.route('/api/settings', methods=['GET'])
def get_settings():
    """获取当前设置"""
    config = load_config()
    return jsonify({
        'output_folder': config.get('output_folder', OUTPUT_FOLDER),
        'watch_enabled': config.get('watch_enabled', False),
        'watch_folder': config.get('watch_folder', '')
    })

@app.route('/api/settings/output_folder', methods=['POST'])
def set_output_folder():
    """设置输出文件夹"""
    global OUTPUT_FOLDER
    data = request.get_json()
    new_path = data.get('path', '').strip()
    
    if not new_path:
        return jsonify({'success': False, 'message': '路径不能为空'})
    
    # 尝试创建目录
    try:
        os.makedirs(new_path, exist_ok=True)
        OUTPUT_FOLDER = new_path
        
        # 保存到配置
        config = load_config()
        config['output_folder'] = new_path
        save_config(config)
        
        logger.info(f"输出目录已更新: {new_path}")
        return jsonify({'success': True, 'path': new_path})
    except Exception as e:
        logger.error(f"设置输出目录失败: {e}")
        return jsonify({'success': False, 'message': str(e)})

@app.route('/api/settings/watch_folder', methods=['POST'])
def set_watch_folder():
    """设置监控文件夹"""
    data = request.get_json()
    watch_enabled = data.get('enabled', False)
    watch_path = data.get('path', '').strip()
    
    config = load_config()
    config['watch_enabled'] = watch_enabled
    config['watch_folder'] = watch_path
    save_config(config)
    
    logger.info(f"监控设置已更新: enabled={watch_enabled}, path={watch_path}")
    return jsonify({'success': True})

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/convert', methods=['POST'])
def convert():
    if 'file' not in request.files:
        return jsonify({'success': False, 'message': 'No file part'})
    
    file = request.files['file']
    if file.filename == '':
        return jsonify({'success': False, 'message': 'No selected file'})

    # 使用自定义的安全文件名函数，保留中文
    filename = safe_filename(file.filename)
    input_path = os.path.join(UPLOAD_FOLDER, filename)
    file.save(input_path)
    
    logger.info(f"接收文件: {filename}")
    
    name, ext = os.path.splitext(filename)
    ext = ext.lower().replace('.', '')
    
    success = False
    message = "Unknown format"
    
    # 初步判断格式
    if ext in ['kgg']:
        success, message = process_kgg(input_path, OUTPUT_FOLDER)
    elif ext in ['kgm', 'kgma']:
        # 使用 Unlock Music 工具进行解密
        success, message = process_kgm(input_path, OUTPUT_FOLDER)
    elif ext in ['ncm']:
        # 网易云音乐 NCM 格式
        success, message = process_ncm(input_path, OUTPUT_FOLDER)
    elif ext in ['flac', 'ogg', 'mp3']:
        # 已经是通用格式，直接复制到输出目录
        shutil.copy(input_path, os.path.join(OUTPUT_FOLDER, filename))
        success = True
        message = "Copied (Already decoded)"
        
    # TODO: 如果用户选择了 MP3 格式，这里应该再调用 ffmpeg 转换 output_path
    
    # 清理上传的临时文件
    if os.path.exists(input_path):
        os.remove(input_path)

    return jsonify({'success': success, 'message': message})

@app.route('/api/open_folder', methods=['POST'])
def open_folder():
    os.startfile(OUTPUT_FOLDER)
    return jsonify({'success': True})

@app.route('/api/archive', methods=['GET'])
def get_archive():
    """获取已转换文件列表"""
    files = []
    total_size = 0
    
    if os.path.exists(OUTPUT_FOLDER):
        for filename in os.listdir(OUTPUT_FOLDER):
            filepath = os.path.join(OUTPUT_FOLDER, filename)
            if os.path.isfile(filepath):
                stat = os.stat(filepath)
                files.append({
                    'name': filename,
                    'size': stat.st_size,
                    'modified': time.strftime('%Y-%m-%d %H:%M', time.localtime(stat.st_mtime))
                })
                total_size += stat.st_size
    
    # 按修改时间倒序排列
    files.sort(key=lambda x: x['modified'], reverse=True)
    
    return jsonify({
        'files': files,
        'total_size': total_size
    })

@app.route('/api/archive/delete', methods=['POST'])
def delete_archive_file():
    """删除归档文件"""
    global OUTPUT_FOLDER
    data = request.get_json()
    filename = data.get('filename', '')
    
    if not filename:
        return jsonify({'success': False, 'message': '文件名为空'})
    
    # 安全检查：防止路径遍历攻击
    filename = safe_filename(filename)
    filepath = os.path.join(OUTPUT_FOLDER, filename)
    
    if not os.path.exists(filepath):
        return jsonify({'success': False, 'message': '文件不存在'})
    
    try:
        os.remove(filepath)
        logger.info(f"删除文件: {filename}")
        return jsonify({'success': True})
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)})


@app.route('/api/archive/copy', methods=['POST'])
def copy_archive_file():
    """将文件复制到剪贴板（可直接粘贴）"""
    data = request.get_json()
    filename = data.get('filename')
    
    if not filename:
        return jsonify({'success': False, 'message': '文件名不能为空'})
    
    filepath = os.path.join(OUTPUT_FOLDER, filename)
    
    if not os.path.exists(filepath):
        return jsonify({'success': False, 'message': '文件不存在'})
    
    try:
        # 使用 PowerShell 将文件复制到剪贴板
        # 这样用户可以直接 Ctrl+V 粘贴文件到任意位置
        ps_command = f'Set-Clipboard -Path "{filepath}"'
        subprocess.run(['powershell', '-Command', ps_command], check=True)
        return jsonify({'success': True, 'message': '文件已复制到剪贴板'})
    except Exception as e:
        logger.error(f"复制文件失败: {e}")
        return jsonify({'success': False, 'message': str(e)})

@app.route('/api/archive/locate', methods=['POST'])
def locate_archive_file():
    """在文件资源管理器中定位文件"""
    data = request.get_json()
    filename = data.get('filename')
    
    if not filename:
        return jsonify({'success': False, 'message': '文件名不能为空'})
    
    filepath = os.path.join(OUTPUT_FOLDER, filename)
    
    if not os.path.exists(filepath):
        return jsonify({'success': False, 'message': '文件不存在'})
    
    try:
        # Windows: 使用 explorer /select 命令定位并选中文件
        subprocess.Popen(['explorer', '/select,', filepath])
        return jsonify({'success': True})
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)})

# ==================== 音频播放 ====================

@app.route('/api/audio/<filename>')
def stream_audio(filename):
    """流式传输音频文件"""
    return send_from_directory(OUTPUT_FOLDER, filename)

# ==================== 文件重命名 ====================

@app.route('/api/archive/rename', methods=['POST'])
def rename_archive_file():
    """重命名归档文件"""
    data = request.get_json()
    old_name = data.get('old_name')
    new_name = data.get('new_name')
    
    if not old_name or not new_name:
        return jsonify({'success': False, 'message': '文件名不能为空'})
    
    # 获取扩展名
    ext = os.path.splitext(old_name)[1]
    new_filename = new_name + ext
    
    old_path = os.path.join(OUTPUT_FOLDER, old_name)
    new_path = os.path.join(OUTPUT_FOLDER, new_filename)
    
    if not os.path.exists(old_path):
        return jsonify({'success': False, 'message': '原文件不存在'})
    
    if os.path.exists(new_path):
        return jsonify({'success': False, 'message': '目标文件名已存在'})
    
    try:
        os.rename(old_path, new_path)
        return jsonify({'success': True, 'new_name': new_filename})
    except Exception as e:
        return jsonify({'success': False, 'message': str(e)})

# ==================== 批量删除 ====================

@app.route('/api/archive/batch_delete', methods=['POST'])
def batch_delete_archive():
    """批量删除归档文件"""
    data = request.get_json()
    filenames = data.get('filenames', [])
    
    if not filenames:
        return jsonify({'success': False, 'message': '未选择文件'})
    
    deleted = 0
    failed = 0
    
    for filename in filenames:
        filepath = os.path.join(OUTPUT_FOLDER, filename)
        try:
            if os.path.exists(filepath):
                os.remove(filepath)
                deleted += 1
        except:
            failed += 1
    
    return jsonify({
        'success': True,
        'deleted': deleted,
        'failed': failed,
        'message': f'已删除 {deleted} 个文件' + (f'，{failed} 个失败' if failed else '')
    })
# ==================== 监控文件夹功能 ====================

SUPPORTED_EXTENSIONS = {'.kgg', '.kgm', '.kgma', '.ncm'}

def scan_watch_folder():
    """扫描监控文件夹，返回新文件列表"""
    global app_config
    
    watch_folder = app_config.get('watch_folder', '')
    if not watch_folder or not os.path.exists(watch_folder):
        return []
    
    processed = app_config.get('processed_files', {})
    new_files = []
    
    try:
        for filename in os.listdir(watch_folder):
            filepath = os.path.join(watch_folder, filename)
            if not os.path.isfile(filepath):
                continue
            
            ext = os.path.splitext(filename)[1].lower()
            if ext not in SUPPORTED_EXTENSIONS:
                continue
            
            # 检查是否已处理
            file_hash = get_file_hash(filepath)
            if file_hash and file_hash not in processed:
                new_files.append({
                    'path': filepath,
                    'name': filename,
                    'hash': file_hash,
                    'size': os.path.getsize(filepath)
                })
    except Exception as e:
        logger.error(f"扫描监控文件夹失败: {e}")
    
    return new_files

def check_duplicate_in_output(filename):
    """检查输出目录是否已有同名文件（去重）"""
    # 获取不带扩展名的文件名
    base_name = os.path.splitext(filename)[0]
    
    # 检查各种可能的输出格式
    for ext in ['.flac', '.ogg', '.mp3']:
        # 检查直接输出
        if os.path.exists(os.path.join(OUTPUT_FOLDER, base_name + ext)):
            return True
        # 检查带 _kgg-dec 后缀
        if os.path.exists(os.path.join(OUTPUT_FOLDER, base_name + '_kgg-dec' + ext)):
            return True
    
    return False

def auto_process_file(file_info):
    """自动处理单个文件"""
    global app_config
    
    filepath = file_info['path']
    filename = file_info['name']
    file_hash = file_info['hash']
    
    result = {
        'name': filename,
        'time': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
        'success': False,
        'message': '',
        'skipped': False
    }
    
    # 检查输出目录是否已有
    if check_duplicate_in_output(filename):
        result['skipped'] = True
        result['message'] = '输出目录已存在同名文件，跳过'
        logger.info(f"跳过重复文件: {filename}")
    else:
        ext = os.path.splitext(filename)[1].lower()
        
        try:
            if ext == '.kgg':
                success, msg = process_kgg(filepath, OUTPUT_FOLDER)
            elif ext == '.ncm':
                success, msg = process_ncm(filepath, OUTPUT_FOLDER)
            else:
                success, msg = process_kgm(filepath, OUTPUT_FOLDER)
            
            result['success'] = success
            result['message'] = msg
        except Exception as e:
            result['message'] = str(e)
    
    # 记录已处理
    app_config['processed_files'][file_hash] = {
        'name': filename,
        'time': result['time'],
        'result': 'success' if result['success'] else ('skipped' if result['skipped'] else 'failed')
    }
    
    # 添加到处理日志（保留最近50条）
    app_config['auto_process_log'].insert(0, result)
    app_config['auto_process_log'] = app_config['auto_process_log'][:50]
    
    save_config(app_config)
    
    return result

@app.route('/api/watch/config', methods=['GET'])
def get_watch_config():
    """获取监控配置"""
    return jsonify({
        'enabled': app_config.get('watch_enabled', False),
        'folder': app_config.get('watch_folder', ''),
        'log': app_config.get('auto_process_log', [])[:20]
    })

@app.route('/api/watch/config', methods=['POST'])
def set_watch_config():
    """设置监控配置"""
    global app_config
    data = request.get_json()
    
    if 'enabled' in data:
        app_config['watch_enabled'] = bool(data['enabled'])
    
    if 'folder' in data:
        folder = data['folder'].strip()
        if folder and not os.path.exists(folder):
            return jsonify({'success': False, 'message': '文件夹不存在'})
        app_config['watch_folder'] = folder
    
    save_config(app_config)
    
    return jsonify({
        'success': True,
        'enabled': app_config['watch_enabled'],
        'folder': app_config['watch_folder']
    })

@app.route('/api/watch/scan', methods=['POST'])
def scan_and_process():
    """扫描并处理新文件"""
    if not app_config.get('watch_enabled'):
        return jsonify({'success': False, 'message': '自动监控未启用'})
    
    if not app_config.get('watch_folder'):
        return jsonify({'success': False, 'message': '未设置监控文件夹'})
    
    new_files = scan_watch_folder()
    
    if not new_files:
        return jsonify({
            'success': True,
            'message': '没有发现新文件',
            'processed': 0,
            'results': []
        })
    
    results = []
    for file_info in new_files:
        result = auto_process_file(file_info)
        results.append(result)
    
    success_count = sum(1 for r in results if r['success'])
    skip_count = sum(1 for r in results if r.get('skipped'))
    
    return jsonify({
        'success': True,
        'message': f'处理完成：成功 {success_count}，跳过 {skip_count}，失败 {len(results) - success_count - skip_count}',
        'processed': len(results),
        'results': results
    })

@app.route('/api/watch/clear_history', methods=['POST'])
def clear_processed_history():
    """清除已处理记录（允许重新处理）"""
    global app_config
    app_config['processed_files'] = {}
    app_config['auto_process_log'] = []
    save_config(app_config)
    return jsonify({'success': True, 'message': '已清除处理历史'})

if __name__ == '__main__':
    print("启动酷狗格式转换器 Web 服务...")
    print(f"请在浏览器访问: http://127.0.0.1:5000")
    
    # 启动时自动扫描（如果启用了监控）
    if app_config.get('watch_enabled') and app_config.get('watch_folder'):
        print(f"监控文件夹: {app_config['watch_folder']}")
        new_files = scan_watch_folder()
        if new_files:
            print(f"发现 {len(new_files)} 个新文件，开始自动处理...")
            for file_info in new_files:
                result = auto_process_file(file_info)
                status = "✓" if result['success'] else ("⊘" if result.get('skipped') else "✗")
                print(f"  {status} {result['name']}: {result['message']}")
    
    app.run(debug=True, port=5000)
