import QtQuick
import PlayerUi

// 悬浮播放岛 (胶囊):旋转迷你黑胶 · 歌名 · 控制 · 时间 · 信号链 · 音量
// 底边发光进度线 (悬停变粗可拖动);下方强调色光晕随低音呼吸
Item {
    id: island
    signal chainClicked()
    signal expandRequested()
    readonly property Item disc: discVinyl
    height: 72
    readonly property real r: height / 2
    readonly property bool compact: width < 760

    // ---- 呼吸光晕 (随低音) ----
    ShaderEffect {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 18
        width: parent.width * 1.15
        height: parent.height * 2.6
        property color color: Theme.accent
        property real intensity: 0.08 + 0.42 * player.breath
        fragmentShader: "qrc:/shaders/glow.frag.qsb"
    }
    SoftShadow {
        target: glass
        spread: 30
        offsetY: 16
        strength: 0.9
    }

    // ---- 面板 ----
    // 不再实时模糊背后的场景 (那需要每帧把整个界面再渲染一遍);改为与背景同色系的深色半透明面板
    Rectangle {
        id: glass
        anchors.fill: parent
        radius: island.r
        color: Qt.rgba(0.075, 0.068, 0.062, 0.94)
    }
    Rectangle {
        anchors.fill: parent
        radius: island.r
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.accentAlpha(0.07) }
            GradientStop { position: 1; color: "transparent" }
        }
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
            id: discVinyl
            anchors.fill: parent
            cover: player.cover
            spinning: player.playing
            labelSize: 128
            // 第一次开始播放时,唱片从空位上淡入并轻轻放大
            opacity: player.hasTrack ? 1 : 0
            scale: player.hasTrack ? 1 : 0.8
            visible: opacity > 0
            Behavior on opacity { OpacityAnimator { duration: Theme.slow; easing.type: Easing.OutCubic } }
            Behavior on scale { ScaleAnimator { duration: Theme.slow; easing.type: Easing.OutBack } }
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            opacity: player.hasTrack ? 0 : 1
            visible: opacity > 0
            Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
            color: Theme.hover
            Icon { anchors.centerIn: parent; name: "disc"; size: 24; opacity: 0.45 }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (player.hasTrack) island.expandRequested()
        }
    }
    // 切歌:旧歌名向左淡出,新歌名从右侧滑入
    property string shownTitle
    property string shownArtist
    Component.onCompleted: { shownTitle = player.title; shownArtist = player.artist }
    Connections {
        target: player
        function onTrackChanged() { swap.restart() }
    }
    SequentialAnimation {
        id: swap
        ParallelAnimation {
            OpacityAnimator { target: titleCol; to: 0; duration: 130; easing.type: Easing.InCubic }
            NumberAnimation { target: titleShift; property: "x"; to: -10; duration: 130; easing.type: Easing.InCubic }
        }
        ScriptAction { script: { island.shownTitle = player.title; island.shownArtist = player.artist; titleShift.x = 14 } }
        ParallelAnimation {
            OpacityAnimator { target: titleCol; to: 1; duration: 320; easing.type: Easing.OutCubic }
            NumberAnimation { target: titleShift; property: "x"; to: 0; duration: 420; easing.type: Easing.OutQuint }
        }
    }
    Column {
        id: titleCol
        transform: Translate { id: titleShift }
        anchors.left: discBox.right
        anchors.leftMargin: 14
        anchors.right: controls.left
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            width: parent.width
            text: player.hasTrack ? island.shownTitle : ""
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        // 歌手,后面跟着播放时间 (时间常驻,歌手名过长时先省略歌手)
        Row {
            width: parent.width
            spacing: 8
            Text {
                id: artistText
                width: Math.min(implicitWidth, parent.width - timeText.implicitWidth - 8)
                text: player.hasTrack ? island.shownArtist : ""
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            Text {
                id: timeText
                visible: player.hasTrack
                text: edge.fmt(edge.dragging ? edge.dragValue : player.position) + " / " + edge.fmt(player.duration)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
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
                scale: 1.0 + 0.2 * player.breath
                opacity: Math.min(1, player.breath * 2.2)
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

        // 小频谱:随音乐跳动,暂停后自然回落
        Spectrum {
            anchors.verticalCenter: parent.verticalCenter
            visible: player.hasTrack && !island.compact
            width: 46
            height: 22
            bars: 10
            gap: 2.6
            opacity: 0.9
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
        VolumeControl { anchors.verticalCenter: parent.verticalCenter; pinned: !island.compact }
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
