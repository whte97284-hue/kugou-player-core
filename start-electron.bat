@echo off
chcp 65001 >nul
title 酷狗音频解码终端 - 桌面版

echo.
echo ╔══════════════════════════════════════════════════════════╗
echo ║          酷狗音频解码终端 v2.1 (桌面版)                   ║
echo ╚══════════════════════════════════════════════════════════╝
echo.

cd /d "%~dp0"

:: 检查端口是否被占用
netstat -ano | findstr ":5000" >nul 2>&1
if %errorlevel% equ 0 (
    echo [!] 端口 5000 已被占用，请先关闭其他服务
    echo [*] 尝试强制关闭占用进程...
    for /f "tokens=5" %%a in ('netstat -ano ^| findstr ":5000" ^| findstr "LISTENING"') do (
        taskkill /PID %%a /F >nul 2>&1
    )
    timeout /t 2 >nul
)

:: 后台启动 Flask
echo [*] 正在启动 Flask 服务...
start /B py app.py >nul 2>&1

:: 等待服务启动
echo [*] 等待服务就绪...
timeout /t 3 >nul

:: 检查服务是否启动成功
curl -s http://127.0.0.1:5000 >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Flask 服务启动失败，尝试使用 python...
    start /B python app.py >nul 2>&1
    timeout /t 3 >nul
)

:: 启动 Electron
echo [*] 正在启动桌面窗口...
cd electron

:: 检查是否有全局 electron
where electron >nul 2>&1
if %errorlevel% equ 0 (
    electron .
) else (
    :: 检查本地 node_modules
    if exist "node_modules\electron" (
        npx electron .
    ) else (
        echo [*] 正在安装 Electron...
        call npm install
        npx electron .
    )
)

:: 关闭 Flask 服务
echo [*] 正在关闭服务...
taskkill /IM python.exe /F >nul 2>&1
taskkill /IM py.exe /F >nul 2>&1

echo [√] 已退出
pause
