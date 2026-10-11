pragma Singleton
import QtQuick

// 设计系统:颜色 / 字体 / 圆角 / 动效时长。强调色由 Main.qml 绑定到当前专辑封面提取色
QtObject {
    id: theme

    property color accent: "#d8b06a"
    Behavior on accent { ColorAnimation { duration: 900; easing.type: Easing.OutCubic } }

    readonly property color base: "#0b0b0e"
    readonly property color text: "#f5f2ec"
    readonly property color textSecondary: Qt.rgba(1, 1, 1, 0.62)
    readonly property color textTertiary: Qt.rgba(1, 1, 1, 0.38)
    readonly property color hairline: Qt.rgba(1, 1, 1, 0.08)
    readonly property color hover: Qt.rgba(1, 1, 1, 0.055)
    readonly property color pressed: Qt.rgba(1, 1, 1, 0.09)
    readonly property color perfect: "#e9c46a"   // Hi-Res / DSD 品质标签 (金)
    readonly property color ok: "#7fdcae"        // 信号链:无损直通 (翡翠绿)
    readonly property color warn: "#ff9a52"      // 信号链:存在转换 (琥珀橙)

    // 强调色的派生色:用于按钮底色、柔光
    function accentAlpha(a) { return Qt.rgba(accent.r, accent.g, accent.b, a) }
    readonly property color onAccent: (accent.r * 0.299 + accent.g * 0.587 + accent.b * 0.114) > 0.62 ? "#15120e" : "#ffffff"

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
}
