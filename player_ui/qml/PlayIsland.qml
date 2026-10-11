import QtQuick
import PlayerUi

// 悬浮播放岛 (胶囊):旋转迷你黑胶 · 歌名 · 控制 · 时间 · 信号链 · 音量
// 底边发光进度线 (悬停变粗可拖动);下方强调色光晕随低音呼吸;真·毛玻璃
Item {
    id: island
    property Item glassSource
    signal chainClicked()
    signal expandRequested()
    height: 72
    readonly property real r: height / 2
    readonly property bool compact: width < 700

    // ---- 呼吸光晕 (随低音) ----
    ShaderEffect {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 18
        width: parent.width * 1.15
        height: parent.height * 2.6
        property color color: Theme.accent
        property real intensity: player.playing ? 0.16 + 0.5 * Math.min(1, player.bass * 1.3) : 0.08
        Behavior on intensity { NumberAnimation { duration: 90 } }
        fragmentShader: "qrc:/shaders/glow.frag.qsb"
    }
    SoftShadow {
        target: glass
        spread: 30
        offsetY: 16
        strength: 0.9
    }

    // ---- 玻璃 ----
    ShaderEffectSource {
        id: behind
        sourceItem: island.glassSource
        sourceRect: island.glassSource ? Qt.rect(island.x, island.y, island.width, island.height) : Qt.rect(0, 0, 0, 0)
        textureSize: Qt.size(Math.max(1, Math.round(island.width / 8)), Math.max(1, Math.round(island.height / 8)))
        live: true
        hideSource: false
        visible: false
        smooth: true
    }
    ShaderEffect {
        id: glass
        anchors.fill: parent
        property variant source: behind
        property size size: Qt.size(width, height)
        property real radius: island.r
        property size texel: Qt.size(1.2 / behind.textureSize.width, 1.2 / behind.textureSize.height)
        property color tint: Qt.rgba(0.06 * 0.55, 0.055 * 0.55, 0.05 * 0.55, 0.55)
        fragmentShader: "qrc:/shaders/frosted.frag.qsb"
    }
    // 上沿高光 + 细描边
    Rectangle {
        anchors.fill: parent
        radius: island.r
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)
    }
    Rectangle {
        x: island.r
        y: 1
        width: parent.width - island.r * 2
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.22) }
            GradientStop { position: 1; color: "transparent" }
        }
    }

    // ---- 左:迷你黑胶 + 歌名 ----
    Item {
        id: discBox
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 54
        height: 54
        Vinyl {
            anchors.fill: parent
            cover: player.cover
            spinning: player.playing
            labelSize: 128
            visible: player.hasTrack
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            visible: !player.hasTrack
            color: Theme.hover
            Icon { anchors.centerIn: parent; name: "disc"; size: 24; opacity: 0.45 }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (player.hasTrack) island.expandRequested()
        }
    }
    Column {
        id: titleCol
        anchors.left: discBox.right
        anchors.leftMargin: 14
        anchors.right: controls.left
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            width: parent.width
            text: player.hasTrack ? player.title : qsTr("未在播放")
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            text: player.hasTrack ? player.artist : qsTr("选一张专辑,让它转起来")
            elide: Text.ElideRight
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }
    MouseArea {
        anchors.left: discBox.right
        anchors.right: controls.left
        anchors.rightMargin: 18
        height: parent.height
        cursorShape: player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (player.hasTrack) island.expandRequested()
    }

    // ---- 中:控制 ----
    Row {
        id: controls
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: island.compact ? 40 : 0
        spacing: 10
        IconButton { icon: "prev"; iconSize: 17; width: 38; height: 38; anchors.verticalCenter: parent.verticalCenter; onClicked: player.previous() }
        Item {
            width: 48
            height: 48
            // 播放键外的强调色光环,随低音轻微脉动
            Rectangle {
                anchors.centerIn: parent
                width: 48; height: 48; radius: 24
                color: Theme.accentAlpha(0.35)
                scale: player.playing ? 1.0 + 0.22 * Math.min(1, player.bass * 1.4) : 1.0
                opacity: player.playing ? 0.9 : 0
                Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
            }
            IconButton {
                anchors.centerIn: parent
                icon: player.playing ? "pause" : "play"
                iconSize: 19
                width: 46; height: 46
                filled: true
                onClicked: player.toggle()
            }
        }
        IconButton { icon: "next"; iconSize: 17; width: 38; height: 38; anchors.verticalCenter: parent.verticalCenter; onClicked: player.next() }
    }

    // ---- 右:时间 · 信号链 · 音量 ----
    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: player.hasTrack && !island.compact
            text: edge.fmt(edge.dragging ? edge.dragValue : player.position) + "  /  " + edge.fmt(player.duration)
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
        Rectangle {
            visible: player.hasTrack
            anchors.verticalCenter: parent.verticalCenter
            height: 28
            width: chipRow.implicitWidth + 22
            radius: 14
            color: chipMouse.containsMouse ? Theme.pressed : Qt.rgba(1, 1, 1, 0.05)
            border.width: 1
            border.color: player.bitPerfect ? Theme.okAlpha(0.45) : Theme.warnAlpha(0.32)
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 7
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7; height: 7; radius: 3.5
                    color: player.bitPerfect ? Theme.ok : Theme.warn
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: player.quality
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
            MouseArea {
                id: chipMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: island.chainClicked()
            }
        }
        VolumeControl { anchors.verticalCenter: parent.verticalCenter; visible: !island.compact }
    }

    // ---- 底边发光进度线 ----
    EdgeProgress {
        id: edge
        x: island.r * 0.7
        width: parent.width - island.r * 1.4
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        position: player.position
        duration: player.duration
        onSeekRequested: (s) => player.seek(s)
    }
}
