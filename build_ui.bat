@echo off
rem ============================================================
rem  构建 QML 新前端 (Qt 6.4+ / MSVC 2022 x64 / CMake + Ninja)
rem  用法: build_ui.bat [Qt 安装路径]
rem  例:   build_ui.bat C:\Qt\6.7.2\msvc2019_64
rem ============================================================
setlocal
cd /d "%~dp0"

set QT_DIR=%~1
if "%QT_DIR%"=="" set QT_DIR=%QTDIR%
if "%QT_DIR%"=="" (
    echo [ERROR] 请传入 Qt 路径,例如: build_ui.bat C:\Qt\6.7.2\msvc2019_64
    exit /b 1
)

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul || (echo [ERROR] 找不到 MSVC 环境 & exit /b 1)

cmake -S . -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" || exit /b 1
cmake --build build-ui --config Release || exit /b 1

rem 部署 Qt 运行库与 QML 模块到 exe 旁边
"%QT_DIR%\bin\windeployqt.exe" --release --qmldir player_ui\qml build-ui\player_ui\KugouPlayer.exe || exit /b 1
if exist tools\kgm\kgm.mask (
    if not exist build-ui\player_ui\tools\kgm mkdir build-ui\player_ui\tools\kgm
    copy /y tools\kgm\kgm.mask build-ui\player_ui\tools\kgm\ >nul
)
echo.
echo [OK] build-ui\player_ui\KugouPlayer.exe
endlocal
