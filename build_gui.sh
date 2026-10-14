#!/usr/bin/env bash
# ============================================================================
#  构建 KugouPlayer GUI (Qt6 QML) —— Windows + MSVC + Ninja
#
#  为什么需要这个脚本(踩过的坑,别绕过):
#   1. 工程位于中文路径下(F:\软件\...)。cmake 在中文 cwd 下会静默退出(码 127),
#      且 Qt 的 qmlimportscanner 会把中文路径按 GBK 解码成乱码 -> 构建失败。
#      所以本脚本先把源码同步到纯 ASCII 目录再构建。
#   2. 本机 Qt 6.8.3 是"瘦身版",缺 Qt6ShaderTools / Qt6Svg / qsb.exe。
#      补齐方法见脚本末尾注释(用 aqtinstall 从官方源拉)。
#   3. windeployqt **不会**抓 Qt6Svg.dll(间接依赖),要手工补,否则运行时报
#      "找不到 Qt6Svg.dll"。SVG 图标(icons/*.svg)靠它渲染。
#   4. CPU 是缩缸的 i7-13600KF,构建并发必须限到 4,默认并行度会蓝屏。
#   5. 若 KugouPlayer.exe 正在运行,链接会报 LNK1104(文件被占用),先关掉。
#
#  用法:
#     ./build_gui.sh                    # 构建工作区源码到 /f/kpgui
#     ./build_gui.sh /c/kpgui           # 指定 ASCII 构建目录
#     ./build_gui.sh /f/kpgui a950970   # 构建指定提交/PR head(用 git archive 抽出,不动工作区)
#     JOBS=6 ./build_gui.sh             # 改并发(默认 4)
# ============================================================================
set -euo pipefail

# 注意:必须转成 Windows 风格路径(F:/...)。原生 git.exe 不认 MSYS 的 /f/... 形式,
# 会报 "fatal: cannot change to '/f/...': No such file or directory"。
SRC_ROOT="$(cygpath -m "$(cd "$(dirname "$0")" && pwd)")"
BUILD_ROOT="${1:-/f/kpgui}"
GIT_REF="${2:-}"
JOBS="${JOBS:-4}"

# ---- 工具链路径(按需修改) --------------------------------------------------
MSVC_VER="14.44.35207"
MSVC_ROOT="C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\$MSVC_VER"
SDK_VER="10.0.26100.0"
SDK_ROOT="C:\\Program Files (x86)\\Windows Kits\\10"
VS_ROOT="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
CMAKE_BIN="$VS_ROOT/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"
NINJA_BIN="$VS_ROOT/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
QT_ROOT="${QT_ROOT:-/d/Qt/6.8.3/msvc2022_64}"

for p in "$CMAKE_BIN/cmake.exe" "$NINJA_BIN/ninja.exe" "$QT_ROOT/bin/windeployqt.exe"; do
    [ -x "$p" ] || { echo "[ERROR] 缺少工具: $p" >&2; exit 1; }
done

# ---- 关掉正在运行的实例(否则链接报 LNK1104) --------------------------------
# 注意: tasklist //FI 的双斜杠会被 MSYS 转义坏 (tasklist 报"无效参数 - '//FI'"), 且 stderr 被
# 2>/dev/null 吞掉 -> 判断恒为假 -> 旧实例没被杀掉 -> 链接报 LNK1104。改用 MSYS_NO_PATHCONV=1 + 单斜杠。
if MSYS_NO_PATHCONV=1 tasklist /FI "IMAGENAME eq KugouPlayer.exe" 2>/dev/null | grep -qi KugouPlayer; then
    echo "[1/6] 结束正在运行的 KugouPlayer.exe ..."
    MSYS_NO_PATHCONV=1 taskkill /F /IM KugouPlayer.exe >/dev/null 2>&1 || true
    sleep 2
else
    echo "[1/6] 没有运行中的实例"
fi

# ---- 同步源码到 ASCII 目录 ------------------------------------------------
# 注意:这里用"覆盖式同步"(不删目录),因为本机安全策略会拦批量删除。
# 若上游**删除或改名**了源文件,旧副本会残留 —— 此时手工删掉整个 $BUILD_ROOT 再跑一次。
if [ -n "$GIT_REF" ]; then
    echo "[2/6] 从 git 提交 $GIT_REF 抽源码 -> $BUILD_ROOT"
    TMP_TREE="$BUILD_ROOT/.tree"
    mkdir -p "$TMP_TREE"
    git -C "$SRC_ROOT" archive "$GIT_REF" CMakeLists.txt player_core player_ui | tar -x -C "$TMP_TREE" \
        || { echo "[ERROR] git archive $GIT_REF 失败(提交不存在?)" >&2; exit 1; }
    mkdir -p "$BUILD_ROOT"
    cp -f "$TMP_TREE/CMakeLists.txt" "$BUILD_ROOT/"
    for d in player_core player_ui; do
        mkdir -p "$BUILD_ROOT/$d"
        cp -rf "$TMP_TREE/$d/." "$BUILD_ROOT/$d/"
    done
else
    echo "[2/6] 同步工作区源码 -> $BUILD_ROOT"
    mkdir -p "$BUILD_ROOT"
    cp -f "$SRC_ROOT/CMakeLists.txt" "$BUILD_ROOT/"
    for d in player_core player_ui; do
        mkdir -p "$BUILD_ROOT/$d"
        cp -rf "$SRC_ROOT/$d/." "$BUILD_ROOT/$d/"
    done
fi

# ---- 环境:MSVC 的 INCLUDE/LIB 必须手工拼(本机禁止调 vcvars64.bat) ----------
export MSYS_NO_PATHCONV=1
export MSYS2_ENV_CONV_EXCL='*'   # 否则 Git Bash 会把 INCLUDE/LIB 当路径列表转换 -> C1083
export PATH="$CMAKE_BIN:$NINJA_BIN:$QT_ROOT/bin:$VS_ROOT/VC/Tools/MSVC/$MSVC_VER/bin/Hostx64/x64:$SDK_ROOT/bin/$SDK_VER/x64:$PATH"
export INCLUDE="$MSVC_ROOT\\include;$SDK_ROOT\\Include\\$SDK_VER\\ucrt;$SDK_ROOT\\Include\\$SDK_VER\\shared;$SDK_ROOT\\Include\\$SDK_VER\\um"
export LIB="$MSVC_ROOT\\lib\\x64;$SDK_ROOT\\Lib\\$SDK_VER\\ucrt\\x64;$SDK_ROOT\\Lib\\$SDK_VER\\um\\x64"

# ---- 配置 + 构建 ----------------------------------------------------------
echo "[3/6] cmake configure"
cd "$BUILD_ROOT"
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(cygpath -m "$QT_ROOT")" \
    > configure.log 2>&1 || { echo "[ERROR] configure 失败,见 $BUILD_ROOT/configure.log" >&2; tail -20 configure.log; exit 1; }
grep -q "Configuring done" configure.log || { echo "[ERROR] configure 未完成" >&2; tail -20 configure.log; exit 1; }

echo "[4/6] cmake build (-j$JOBS)"
cmake --build b -j"$JOBS" > build.log 2>&1 || { echo "[ERROR] 构建失败,见 $BUILD_ROOT/build.log" >&2; grep -aiE "error|FAILED" build.log | head -20; exit 1; }

EXE="$BUILD_ROOT/b/player_ui/KugouPlayer.exe"
[ -f "$EXE" ] || { echo "[ERROR] 未生成 $EXE" >&2; exit 1; }

# ---- 部署 Qt 运行库 -------------------------------------------------------
echo "[5/6] windeployqt 部署运行库"
cd "$(dirname "$EXE")"
windeployqt.exe --release --no-translations --qmldir "$BUILD_ROOT/player_ui/qml" KugouPlayer.exe \
    > deploy.log 2>&1 || true

# windeployqt 抓不到间接依赖,手工补(SVG 图标必需)
[ -f Qt6Svg.dll ] || cp -n "$QT_ROOT/bin/Qt6Svg.dll" . 2>/dev/null || true
[ -f Qt6ShaderTools.dll ] || cp -n "$QT_ROOT/bin/Qt6ShaderTools.dll" . 2>/dev/null || true

# KGG 外部解码器:程序启动时从 <exe>\tools\kgg 查找 (KGM 掩码已内置,不再需要 kgm.mask)
for f in kgg/kgg-dec.exe kgg/infra.dll; do
    [ -f "$SRC_ROOT/tools/$f" ] || continue
    mkdir -p "tools/$(dirname "$f")"
    if [ ! -f "tools/$f" ] || [ "$(stat -c%s "tools/$f")" != "$(stat -c%s "$SRC_ROOT/tools/$f")" ]; then
        cp -f "$SRC_ROOT/tools/$f" "tools/$f"
    fi
done

echo "[6/6] 完成"
echo
echo "  可执行文件: $(cygpath -w "$EXE")"
echo "  大小: $(stat -c%s "$EXE") 字节"
echo "  依赖 DLL: $(ls *.dll 2>/dev/null | wc -l) 个"
echo
echo "直接运行:"
echo "  $(cygpath -w "$EXE")"
echo

# ============================================================================
#  如果 configure 报 "Failed to find required Qt component ShaderTools/Svg":
#  本机 Qt 是瘦身版,需要补装这两个模块(需要网络 + python):
#
#    PY=<python.exe>
#    $PY -m pip install --index-url https://pypi.org/simple aqtinstall
#    $PY -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 \
#          --archives qtsvg -O /tmp/qtfull
#    $PY -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 \
#          --noarchives -m qtshadertools -O /tmp/qtfull
#    # 然后把 /tmp/qtfull/6.8.3/msvc2022_64 下这几项"只增不覆盖"地并进 Qt:
#    #   lib/cmake/{Qt6Svg,Qt6SvgWidgets,Qt6ShaderTools,Qt6ShaderToolsTools}
#    #   include/{QtSvg,QtSvgWidgets,QtShaderTools}
#    #   bin/{Qt6Svg*.dll,Qt6ShaderTools*.dll,qsb.exe}
#    #   lib/{Qt6Svg*,Qt6ShaderTools*},plugins/{imageformats/qsvg*.dll,iconengines/qsvgicon*.dll}
#    #   另:plugins/iconengines 在瘦身版里可能是个文件而非目录,需改名后重建目录
# ============================================================================
