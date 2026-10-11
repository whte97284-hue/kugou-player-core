import QtQuick
import PlayerUi

// 悬浮播放岛:真·毛玻璃 (实时采样岛后方画面 → 1/4 降采样 → 着色器模糊压暗)
// 左:封面 + 歌名;中:控制 + 进度;右:信号链胶囊 + 音量
Item {
    id: island
    property Item glassSource
    signal chainClicked()
    signal titleClicked()
    height: Theme.islandHeight

    // ---- 玻璃底 ----
    ShaderEffectSource {
        id: behind
        sourceItem: island.glassSource
        sourceRect: island.glassSource ? Qt.rect(island.x, island.y, island.width, island.height) : Qt.rect(0, 0, 0, 0)
        // 1/8 降采样 + 9 点模糊 ≈ 大半径高斯,开销只有全分辨率模糊的几十分之一
        textureSize: Qt.size(Math.max(1, Math.round(island.width / 8)), Math.max(1, Math.round(island.height / 8)))
        live: true
        hideSource: false
        visible: false
        smooth: true
    }
    ShaderEffect {
        anchors.fill: parent
        property variant source: behind
        property size size: Qt.size(width, height)
        property real radius: 28
        property size texel: Qt.size(1.2 / behind.textureSize.width, 1.2 / behind.textureSize.height)
        property color tint: Qt.rgba(0.05 * 0.62, 0.05 * 0.62, 0.06 * 0.62, 0.62)
        fragmentShader: "qrc:/shaders/frosted.frag.qsb"
    }
    // 顶部高光描边 + 强调色微光
    Rectangle {
        anchors.fill: parent
        radius: 28
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        width: parent.width * 0.6
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.accentAlpha(0.55) }
            GradientStop { position: 1; color: "transparent" }
        }
    }

    // ---- 左:封面 + 歌名 ----
    Item {
        id: left
        x: 14
        width: Math.min(300, island.width * 0.28)
        height: parent.height
        RoundedImage {
            id: cover
            anchors.verticalCenter: parent.verticalCenter
            width: 58; height: 58
            radius: 12
            source: player.cover
            decodeSize: 160
            visible: player.hasTrack
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 58; height: 58; radius: 12
            visible: !player.hasTrack
            color: Theme.hover
            Icon { anchors.centerIn: parent; name: "disc"; size: 26; opacity: 0.4 }
        }
        Column {
            anchors.left: cover.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Text {
                width: parent.width
                text: player.hasTrack ? player.title : qsTr("未在播放")
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold
                MouseArea {
                    anchors.fill: parent
                    cursorShape: player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (player.hasTrack) island.titleClicked()
                }
            }
            Text {
                width: parent.width
                text: player.hasTrack ? player.artist : qsTr("选择一张专辑开始")
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }
    }

    // ---- 中:控制 + 进度 ----
    Item {
        id: center
        anchors.left: left.right
        anchors.leftMargin: 20
        anchors.right: right.left
        anchors.rightMargin: 20
        height: parent.height

        Row {
            id: controls
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            spacing: 14
            IconButton { icon: "prev"; iconSize: 18; width: 38; height: 38; anchors.verticalCenter: parent.verticalCenter; onClicked: player.previous() }
            IconButton {
                icon: player.playing ? "pause" : "play"
                iconSize: 20
                width: 44; height: 44
                filled: true
                onClicked: player.toggle()
            }
            IconButton { icon: "next"; iconSize: 18; width: 38; height: 38; anchors.verticalCenter: parent.verticalCenter; onClicked: player.next() }
        }
        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: controls.bottom
            anchors.topMargin: 2
            spacing: 12
            Text {
                id: tPos
                width: 40
                horizontalAlignment: Text.AlignRight
                anchors.verticalCenter: parent.verticalCenter
                text: seek.fmt(seek.dragging ? seek.dragValue : player.position)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
            SeekBar {
                id: seek
                width: parent.width - 104
                anchors.verticalCenter: parent.verticalCenter
                position: player.position
                duration: player.duration
                onSeekRequested: (s) => player.seek(s)
            }
            Text {
                width: 40
                anchors.verticalCenter: parent.verticalCenter
                text: seek.fmt(player.duration)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
        }
    }

    // ---- 右:信号链胶囊 + 音量 ----
    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        Rectangle {
            id: chip
            visible: player.hasTrack
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            width: chipRow.implicitWidth + 24
            radius: 15
            color: chipMouse.containsMouse ? Theme.pressed : Theme.hover
            border.width: 1
            border.color: player.bitPerfect ? Qt.rgba(0.5, 0.86, 0.68, 0.45) : Theme.hairline
            Behavior on color { ColorAnimation { duration: Theme.fast } }
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
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: island.width > 900
                    text: {
                        const c = player.signalChain
                        if (!c || c.length === 0) return ""
                        return "→ " + c[c.length - 1].label
                    }
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
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

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            visible: island.width > 760
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: player.volume < 0.01 ? "mute" : "volume"
                iconSize: 18
                width: 34; height: 34
                property real lastVolume: 0.85
                onClicked: {
                    if (player.volume > 0.01) { lastVolume = player.volume; player.volume = 0 }
                    else player.volume = lastVolume
                }
            }
            SeekBar {
                anchors.verticalCenter: parent.verticalCenter
                width: 86
                showTip: false
                position: player.volume
                duration: 1
                onSeekRequested: (v) => player.volume = v
                onDragValueChanged: if (dragging) player.volume = dragValue
            }
        }
    }
}
