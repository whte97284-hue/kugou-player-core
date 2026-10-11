pragma Singleton
import QtQuick

// 设计系统:颜色 / 字体 / 圆角 / 动效时长。强调色由 Main.qml 绑定到当前专辑封面提取色
QtObject {
    id: theme

    // ---- 强调色 (随专辑封面,已在 C++ 侧驯化:限饱和、定亮度、蓝紫品红降饱和) ----
    // 不对 accent 做颜色动画:上百处绑定会在动画期间每帧重算,切歌瞬间掉帧;
    // 背景极光自带 1.6 秒色彩过渡,整体观感依然是平滑变色
    property color accent: "#c8a46e"

    // ---- 暖中性色:暖炭黑底、暖白文字,像胶片与高端音响面板,而非冷蓝科技感 ----
    readonly property color base: "#100f0e"
    readonly property color panel: Qt.rgba(0.082, 0.075, 0.068, 0.96)   // 弹出面板
    readonly property color tooltip: Qt.rgba(0.11, 0.10, 0.09, 0.96)
    readonly property color text: "#f3efe9"
    readonly property color textSecondary: Qt.rgba(0.953, 0.937, 0.914, 0.64)
    readonly property color textTertiary: Qt.rgba(0.953, 0.937, 0.914, 0.40)
    readonly property color hairline: Qt.rgba(1, 0.96, 0.9, 0.08)
    readonly property color hover: Qt.rgba(1, 0.96, 0.9, 0.055)
    readonly property color pressed: Qt.rgba(1, 0.96, 0.9, 0.09)

    // ---- 功能色 (低饱和,与强调色并存不抢戏) ----
    readonly property color perfect: "#d6b77e"   // Hi-Res / DSD 品质标签:香槟金
    readonly property color ok: "#9fbf9a"        // 信号链无损直通:鼠尾草绿
    readonly property color warn: "#d9a066"      // 信号链存在转换:琥珀
    function okAlpha(a) { return Qt.rgba(ok.r, ok.g, ok.b, a) }
    function warnAlpha(a) { return Qt.rgba(warn.r, warn.g, warn.b, a) }
    function perfectAlpha(a) { return Qt.rgba(perfect.r, perfect.g, perfect.b, a) }

    // 强调色的派生色:用于按钮底色、柔光
    function accentAlpha(a) { return Qt.rgba(accent.r, accent.g, accent.b, a) }
    // 强调色偏亮时,其上的文字与图标改用深色 (驯化后的强调色大多属于此类,深字更显精致)
    readonly property bool accentIsLight: (accent.r * 0.299 + accent.g * 0.587 + accent.b * 0.114) > 0.46
    readonly property color onAccent: accentIsLight ? "#15120e" : "#ffffff"

    readonly property string fontFamily: {
        const preferred = ["Microsoft YaHei UI", "PingFang SC", "HarmonyOS Sans SC", "Noto Sans CJK SC", "Source Han Sans SC", "Noto Sans SC"]
        const all = Qt.fontFamilies()
        for (let i = 0; i < preferred.length; ++i)
            if (all.indexOf(preferred[i]) >= 0) return preferred[i]
        return Qt.application.font.family
    }

    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 22

    readonly property int fast: 140
    readonly property int normal: 260
    readonly property int slow: 520

    readonly property int navWidth: 72
    readonly property int islandHeight: 84

    // 字节数 → 人类可读
    function formatBytes(b) {
        const u = ["B", "KB", "MB", "GB", "TB"]
        let i = 0
        let v = Number(b)
        while (v >= 1024 && i < u.length - 1) { v /= 1024; ++i }
        return (i === 0 ? v : v.toFixed(v >= 100 ? 0 : 1)) + " " + u[i]
    }
}
