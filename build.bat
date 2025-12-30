@echo off
chcp 65001 >nul
title 酷狗音频解码器 - 打包工具
color 0A

echo.
echo  ╔════════════════════════════════════════════════════╗
echo  ║       酷狗音频解码终端 - EXE 打包工具              ║
echo  ║                   v2.1                             ║
echo  ╚════════════════════════════════════════════════════╝
echo.

:: 检查 PyInstaller
echo [1/5] 检查 PyInstaller...
pip show pyinstaller >nul 2>&1
if errorlevel 1 (
    echo      PyInstaller 未安装，正在安装...
    pip install pyinstaller
)
echo      √ PyInstaller 已就绪

:: 清理旧的打包文件
echo.
echo [2/5] 清理旧的打包文件...
if exist "dist" rmdir /s /q dist
if exist "build" rmdir /s /q build
if exist "*.spec" del /q *.spec
echo      √ 清理完成

:: 开始打包
echo.
echo [3/5] 开始打包 Flask 应用...
echo      这可能需要几分钟，请耐心等待...
echo.

pyinstaller --noconfirm ^
    --onedir ^
    --console ^
    --name "KugouDecoder" ^
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
    echo.
    echo [错误] 打包失败！
    pause
    exit /b 1
)

:: 复制额外文件
echo.
echo [4/5] 复制配置文件和目录...
mkdir "dist\KugouDecoder\output" 2>nul
xcopy /E /I /Y "tools" "dist\KugouDecoder\tools" >nul 2>&1
echo      √ 文件复制完成

:: 创建启动脚本
echo.
echo [5/5] 创建启动脚本...
(
echo @echo off
echo chcp 65001 ^>nul
echo title 酷狗音频解码终端 v2.1
echo cd /d "%%~dp0"
echo echo 正在启动酷狗音频解码器...
echo echo 请勿关闭此窗口
echo echo.
echo start "" http://127.0.0.1:5000
echo KugouDecoder.exe
echo pause
) > "dist\KugouDecoder\启动酷狗解码器.bat"
echo      √ 启动脚本创建完成

echo.
echo  ╔════════════════════════════════════════════════════╗
echo  ║                  打包完成！                        ║
echo  ╠════════════════════════════════════════════════════╣
echo  ║  输出目录: dist\KugouDecoder\                      ║
echo  ║  运行方式: 双击 启动酷狗解码器.bat                 ║
echo  ╚════════════════════════════════════════════════════╝
echo.

pause
