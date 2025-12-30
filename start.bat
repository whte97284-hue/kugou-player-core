@echo off
chcp 65001 >nul
title 酷狗音频解码终端

echo.
echo ╔══════════════════════════════════════════════════════════╗
echo ║          酷狗音频解码终端 v2.1                            ║
echo ║          KuGou Audio Decoder Terminal                    ║
echo ╚══════════════════════════════════════════════════════════╝
echo.

:: 切换到脚本所在目录
cd /d "%~dp0"

:: 检查 Python
echo [*] 正在检查 Python 环境...
py --version >nul 2>&1
if %errorlevel% neq 0 (
    python --version >nul 2>&1
    if %errorlevel% neq 0 (
        echo [X] 错误: 未检测到 Python，请先安装 Python！
        echo     下载地址: https://www.python.org/downloads/
        pause
        exit /b 1
    )
)
echo [√] Python 环境正常

:: 检查 Flask
echo [*] 正在检查 Flask...
py -c "import flask" >nul 2>&1
if %errorlevel% neq 0 (
    echo [*] 正在安装 Flask...
    pip install flask -q
)
echo [√] Flask 已就绪

:: 启动服务
echo.
echo ════════════════════════════════════════════════════════════
echo [*] 正在启动 Web 服务...
echo [*] 请在浏览器中访问: http://127.0.0.1:5000
echo [*] 按 Ctrl+C 可以停止服务
echo ════════════════════════════════════════════════════════════
echo.

py app.py

pause
