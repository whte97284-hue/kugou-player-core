import QtQuick
import PlayerUi

// 黑胶唱片:封面作中心标签,33⅓ 转/分真实转速;暂停时停在当前角度,继续时无跳变;
// 光泽层不随唱片旋转 (真实灯光下的反射是固定的)
Item {
    id: v
    property string cover
    property bool spinning: false
    property int labelSize: 256

    Image {
        id: labelImg
        source: v.cover
        sourceSize: Qt.size(v.labelSize, v.labelSize)
        asynchronous: true
        visible: false
        smooth: true
    }
    ShaderEffect {
        id: disc
        anchors.fill: parent
        property variant label: labelImg
        property real pixel: 1.0 / Math.max(1, width)
        property color tint: Theme.accent
        fragmentShader: "qrc:/shaders/vinyl.frag.qsb"
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 1800
            loops: Animation.Infinite
            running: true
            paused: !v.spinning
        }
    }
    ShaderEffect {
        anchors.fill: parent
        property color tint: Theme.accent
        fragmentShader: "qrc:/shaders/sheen.frag.qsb"
    }
}
