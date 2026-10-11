import QtQuick
import PlayerUi

// 全窗口背景:当前专辑封面的重度模糊图 (C++ 侧 64px 盒式模糊,GPU 放大即柔和)
// 两层交替交叉淡入淡出;缓慢呼吸缩放;深色渐变压暗保证文字可读;颗粒打散色带
Item {
    id: root
    property string source
    clip: true

    Rectangle { anchors.fill: parent; color: Theme.base }

    // 无曲目时的默认氛围光
    Rectangle {
        anchors.fill: parent
        opacity: root.source.length ? 0 : 1
        Behavior on opacity { OpacityAnimator { duration: Theme.slow } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1c1a22" }
            GradientStop { position: 0.55; color: "#0f0e13" }
            GradientStop { position: 1.0; color: "#0a0a0d" }
        }
    }

    // showA = true 时 A 层可见。新封面总是加载进隐藏层,再切换可见层,形成交叉淡入淡出
    property bool showA: false
    onSourceChanged: {
        if (!source.length) return
        if (showA) layerB.source = source
        else layerA.source = source
        showA = !showA
    }

    Item {
        id: drift
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        // 极慢的呼吸缩放,让静止画面也有生命感 (渲染线程动画,不占界面线程)
        SequentialAnimation on scale {
            loops: Animation.Infinite
            ScaleAnimator { from: 1.18; to: 1.30; duration: 24000; easing.type: Easing.InOutSine }
            ScaleAnimator { from: 1.30; to: 1.18; duration: 24000; easing.type: Easing.InOutSine }
        }
        Image {
            id: layerA
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            smooth: true
            asynchronous: true
            sourceSize: Qt.size(64, 64)
            opacity: root.showA && status === Image.Ready ? 1 : 0
            Behavior on opacity { OpacityAnimator { duration: 900; easing.type: Easing.InOutQuad } }
        }
        Image {
            id: layerB
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            smooth: true
            asynchronous: true
            sourceSize: Qt.size(64, 64)
            opacity: !root.showA && status === Image.Ready ? 1 : 0
            Behavior on opacity { OpacityAnimator { duration: 900; easing.type: Easing.InOutQuad } }
        }
    }

    // 压暗:顶部略亮以保留封面色彩,底部与左侧导航区更暗
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.035, 0.035, 0.045, 0.62) }
            GradientStop { position: 0.45; color: Qt.rgba(0.035, 0.035, 0.045, 0.76) }
            GradientStop { position: 1.0; color: Qt.rgba(0.03, 0.03, 0.04, 0.90) }
        }
    }
    Rectangle {
        width: parent.width * 0.35
        height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.35) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0) }
        }
    }

    ShaderEffect {
        anchors.fill: parent
        property size size: Qt.size(width, height)
        property real strength: 0.028
        fragmentShader: "qrc:/shaders/grain.frag.qsb"
    }
}
