import QtQuick
import PlayerUi

// 沉浸页背景:当前封面模糊铺满整窗。只在换封面时渲染一张小纹理 (layer),之后每帧只是拉伸贴图,几乎零开销;
// 换歌时新背景加载完成后在旧背景上淡入
Item {
    id: root
    property string source
    property Item front: null

    onSourceChanged: load()
    Component.onCompleted: load()
    function load() {
        if (front && front.src === source) return
        const back = front === a ? b : a
        back.src = source
        if (back.ready) show(back)
    }
    // 新背景放到最上层,从透明淡入;旧背景留在下面,淡入期间不会露出底色
    function show(layer) {
        if (front === layer || layer.src !== source) return
        if (front) front.z = 0
        layer.z = 1
        layer.opacity = 0
        fadeIn.target = layer
        fadeIn.restart()
        front = layer
    }
    OpacityAnimator { id: fadeIn; from: 0; to: 1; duration: 900; easing.type: Easing.InOutQuad }

    component Layer: Item {
        id: lay
        property string src
        readonly property bool ready: img.status === Image.Ready
        anchors.fill: parent
        opacity: 0
        onReadyChanged: if (ready) root.show(lay)
        Image {
            id: img
            source: lay.src
            sourceSize: Qt.size(48, 48)
            asynchronous: true
            visible: false
            smooth: true
        }
        Item {
            anchors.fill: parent
            layer.enabled: true
            layer.smooth: true
            layer.textureSize: Qt.size(Math.max(1, Math.round(width / 6)), Math.max(1, Math.round(height / 6)))
            ShaderEffect {
                anchors.fill: parent
                visible: lay.ready
                property variant source: img
                readonly property real aspect: width / Math.max(1, height)
                property size crop: aspect >= 1 ? Qt.size(1, 1 / aspect) : Qt.size(aspect, 1)
                property real spread: 0.16
                property real dim: 0.5
                fragmentShader: "qrc:/shaders/backdrop.frag.qsb"
            }
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.base }
    Layer { id: a }
    Layer { id: b }
}
