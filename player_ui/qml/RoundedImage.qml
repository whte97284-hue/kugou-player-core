import QtQuick
import PlayerUi

// 圆角封面:Image 作为纹理源 + 圆角 SDF 着色器,边缘 1px 抗锯齿;加载完成后淡入
Item {
    id: root
    property alias source: img.source
    property real radius: Theme.radiusM
    property int decodeSize: 512
    property alias status: img.status

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Qt.rgba(1, 1, 1, 0.05)
        visible: img.status !== Image.Ready
    }
    Image {
        id: img
        anchors.fill: parent
        visible: false
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(root.decodeSize, root.decodeSize)
        smooth: true
    }
    ShaderEffect {
        id: fx
        anchors.fill: parent
        property variant source: img
        property size size: Qt.size(width, height)
        property real radius: root.radius
        fragmentShader: "qrc:/shaders/rounded.frag.qsb"
        opacity: img.status === Image.Ready ? 1 : 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
    }
    // 细微内描边,让深色封面在深色背景上也有边界
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.07)
    }
}
