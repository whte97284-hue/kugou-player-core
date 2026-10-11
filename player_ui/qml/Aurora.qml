import QtQuick
import PlayerUi

// 极光背景:封面 4 主色在 GPU 上缓慢流动融合;切歌时颜色 1.6 秒平滑过渡;
// 低音能量让色团轻微膨胀变亮;窗口失去焦点时暂停动画;低分辨率渲染,滚动时不与列表争抢 GPU
Item {
    id: root
    property var colors: player.palette
    property color c0: colors && colors.length > 0 ? colors[0] : "#2e2619"
    property color c1: colors && colors.length > 1 ? colors[1] : "#231b16"
    property color c2: colors && colors.length > 2 ? colors[2] : "#3a3020"
    property color c3: colors && colors.length > 3 ? colors[3] : "#1b1816"
    Behavior on c0 { ColorAnimation { duration: 1600; easing.type: Easing.InOutQuad } }
    Behavior on c1 { ColorAnimation { duration: 1600; easing.type: Easing.InOutQuad } }
    Behavior on c2 { ColorAnimation { duration: 1600; easing.type: Easing.InOutQuad } }
    Behavior on c3 { ColorAnimation { duration: 1600; easing.type: Easing.InOutQuad } }

    // 色团本身就是极柔和的渐变:以 1/4 分辨率渲染再放大,观感不变,填充开销降到 1/16
    ShaderEffect {
        id: fx
        anchors.fill: parent
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(1, Math.round(width / 4)), Math.max(1, Math.round(height / 4)))
        property size size: Qt.size(width, height)
        property real time: 0
        property real energy: player.playing ? Math.min(1, player.bass * 1.4) : 0
        Behavior on energy { NumberAnimation { duration: 120 } }
        property color c0: root.c0
        property color c1: root.c1
        property color c2: root.c2
        property color c3: root.c3
        property color base: Theme.base
        fragmentShader: "qrc:/shaders/aurora.frag.qsb"

        UniformAnimator {
            target: fx
            uniform: "time"
            from: 0
            to: 3600
            duration: 3600 * 1000
            loops: Animation.Infinite
            running: true
            paused: Qt.application.state !== Qt.ApplicationActive
        }
    }
}
