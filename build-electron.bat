@echo off
chcp 65001 >nul
title 酷狗音频解码器 - Electron 打包工具
color 0A

echo.
echo  ========================================================
echo        酷狗音频解码终端 - Electron 完整打包
echo                     v2.1
echo  ========================================================
echo.

:: 步骤1: 打包 Flask 后端
echo [1/4] 打包 Flask 后端...
echo.

if exist "flask-dist" rmdir /s /q flask-dist

pyinstaller --noconfirm ^
    --onedir ^
    --console ^
    --name "KugouDecoder" ^
    --distpath "flask-dist" ^
    --add-data "templates;templates" ^
    --add-data "static;static" ^
    --add-data "tools;tools" ^
    --hidden-import "flask" ^
    --hidden-import "werkzeug" ^
    --hidden-import "jinja2" ^
    --hidden-import "markupsafe" ^
    --hidden-import "click" ^
    --hidden-import "itsdangerous" ^
    app.py

if errorlevel 1 (
    echo [错误] Flask 打包失败！
    pause
    exit /b 1
)
echo      √ Flask 后端打包完成

:: 创建 output 目录
mkdir "flask-dist\KugouDecoder\output" 2>nul

:: 步骤2: 安装 Electron 依赖
echo.
echo [2/4] 检查 Electron 依赖...
cd electron
call npm install
if errorlevel 1 (
    echo [错误] npm install 失败！
    pause
    exit /b 1
)
echo      √ Electron 依赖已安装
cd ..

:: 步骤3: 打包 Electron
echo.
echo [3/4] 打包 Electron 应用...
cd electron
call npm run build
if errorlevel 1 (
    echo [错误] Electron 打包失败！
    pause
    exit /b 1
)
echo      √ Electron 打包完成
cd ..

:: 步骤4: 完成
echo.
echo [4/4] 打包完成！

echo.
echo  ========================================================
echo                    打包成功！
echo  --------------------------------------------------------
echo  输出目录: dist-electron\
echo  可执行文件: 酷狗音频解码器-2.1.0.exe
echo  ========================================================
echo.

pause
