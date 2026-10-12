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
        visible: settings.background !== 2
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(1, Math.round(width / 4)), Math.max(1, Math.round(height / 4)))
        property size size: Qt.size(width, height)
        property real time: 0
        // breath 已在内核侧平滑过;这里再加 Behavior 会每帧重启一个动画
        property real energy: root.visible ? player.breath : 0
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
            // 背景:0 流动 / 1 静止 / 2 纯色;减弱动效时也停止流动
            running: settings.background === 0 && !Theme.reduced
            // 被沉浸播放页完全盖住、或窗口不在前台时暂停,不再白白整屏重绘
            paused: Qt.application.state !== Qt.ApplicationActive || !root.visible
        }
    }
}
