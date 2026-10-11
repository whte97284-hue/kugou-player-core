import QtQuick
import PlayerUi

// 沉浸播放页:封套 + 滑出旋转的黑胶;大标题;信号链标签;大号控制;底部带倒影的宽频谱
Item {
    id: np
    property bool shown: false
    signal closeRequested()
    signal openAlbum(int index)

    opacity: shown ? 1 : 0
    visible: opacity > 0
    scale: shown ? 1 : 0.97
    Behavior on opacity { OpacityAnimator { duration: Theme.slow; easing.type: Easing.OutCubic } }
    Behavior on scale { ScaleAnimator { duration: Theme.slow; easing.type: Easing.OutQuint } }

    // 独立的极光背景,完全覆盖下层页面;再叠一层轻微压暗突出前景
    Aurora { anchors.fill: parent; visible: np.visible }
    Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.025, 0.02, 0.18) }
    MouseArea { anchors.fill: parent; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    readonly property real s: Math.min(np.height * 0.50, np.width * 0.30, 470)
    readonly property real leftX: Math.max(64, np.width * 0.08)
    readonly property real coverY: (np.height - s) / 2 - np.height * 0.06

    IconButton {
        x: 28
        y: 26
        icon: "back"
        iconSize: 20
        rotation: -90
        onClicked: np.closeRequested()
    }

    // ---- 封套 + 黑胶 ----
    Item {
        id: art
        x: np.leftX
        y: np.coverY
        width: np.s
        height: np.s

        Vinyl {
            id: disc
            width: np.s * 0.95
            height: width
            y: (np.s - height) / 2
            x: player.playing ? np.s * 0.44 : np.s * 0.05
            Behavior on x { NumberAnimation { duration: 1100; easing.type: Easing.OutCubic } }
            cover: player.cover
            spinning: player.playing
            labelSize: 384
        }
        SoftShadow { target: sleeve; spread: 44; offsetY: 26; strength: 1.0 }
        RoundedImage {
            id: sleeve
            width: np.s
            height: np.s
            radius: 10
            source: player.cover
            decodeSize: 1024
        }
        MouseArea {
            anchors.fill: sleeve
            cursorShape: Qt.PointingHandCursor
            onClicked: if (player.albumIndex >= 0) np.openAlbum(player.albumIndex)
        }
    }

    // ---- 右侧信息与控制 ----
    Column {
        id: info
        x: art.x + np.s * 1.44 + 72
        width: Math.max(320, np.width - x - np.width * 0.07)
        anchors.verticalCenter: art.verticalCenter
        spacing: 12

        Row {
            spacing: 10
            Row {
                anchors.verticalCenter: parent.verticalCenter
                height: 12
                spacing: 2
                Repeater {
                    model: 4
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: 3
                        radius: 1.5
                        height: 3 + 9 * (player.playing ? (player.spectrum[2 + index * 6] || 0) : 0.15)
                        color: Theme.accent
                    }
                }
            }
            Text {
                text: player.playing ? qsTr("正在播放") : qsTr("已暂停")
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: 2.5
            }
        }
        Text {
            width: parent.width
            text: player.title
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Math.min(56, Math.max(34, np.width * 0.034))
            font.weight: Font.Bold
            font.letterSpacing: -0.8
            lineHeight: 1.04
        }
        Text {
            width: parent.width
            text: player.artist
            elide: Text.ElideRight
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: 20
            font.weight: Font.Medium
        }
        Text {
            width: parent.width
            text: player.album
            elide: Text.ElideRight
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }
        Item { width: 1; height: 6 }

        // 信号链标签
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: player.signalChain
                Rectangle {
                    height: 26
                    width: chipText.implicitWidth + 26
                    radius: 13
                    color: Qt.rgba(1, 1, 1, 0.05)
                    border.width: 1
                    border.color: modelData.ok ? Theme.okAlpha(0.38) : Theme.warnAlpha(0.32)
                    Rectangle {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 6; height: 6; radius: 3
                        color: modelData.ok ? Theme.ok : Theme.warn
                    }
                    Text {
                        id: chipText
                        x: 21
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
            }
        }
        Item { width: 1; height: 14 }

        // 进度
        Column {
            width: Math.min(parent.width, 560)
            spacing: 4
            SeekBar {
                id: seek
                width: parent.width
                position: player.position
                duration: player.duration
                onSeekRequested: (s) => player.seek(s)
            }
            Item {
                width: parent.width
                height: 16
                Text {
                    text: seek.fmt(seek.dragging ? seek.dragValue : player.position)
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
                Text {
                    anchors.right: parent.right
                    text: "-" + seek.fmt(Math.max(0, player.duration - (seek.dragging ? seek.dragValue : player.position)))
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
            }
        }
        Item { width: 1; height: 6 }

        // 大号控制
        Row {
            spacing: 26
            IconButton { icon: "prev"; iconSize: 26; width: 56; height: 56; anchors.verticalCenter: parent.verticalCenter; onClicked: player.previous() }
            Item {
                width: 80
                height: 80
                Rectangle {
                    anchors.centerIn: parent
                    width: 80; height: 80; radius: 40
                    color: Theme.accentAlpha(0.28)
                    scale: player.playing ? 1.0 + 0.28 * Math.min(1, player.bass * 1.4) : 1.0
                    opacity: player.playing ? 1 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                }
                IconButton {
                    anchors.centerIn: parent
                    icon: player.playing ? "pause" : "play"
                    iconSize: 30
                    width: 76; height: 76
                    filled: true
                    onClicked: player.toggle()
                }
            }
            IconButton { icon: "next"; iconSize: 26; width: 56; height: 56; anchors.verticalCenter: parent.verticalCenter; onClicked: player.next() }
            Item { width: 12; height: 1 }
            VolumeControl { anchors.verticalCenter: parent.verticalCenter }
        }
    }

    // ---- 底部频谱 + 倒影 ----
    Item {
        id: specBox
        x: np.leftX
        width: np.width - np.leftX * 2
        height: 70
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 58
        opacity: 0.85
        Spectrum {
            anchors.fill: parent
            bars: 32
            gap: Math.max(4, width / 32 * 0.32)
        }
    }
    Spectrum {
        x: specBox.x
        width: specBox.width
        height: specBox.height
        y: specBox.y + specBox.height + 2
        bars: 32
        gap: Math.max(4, width / 32 * 0.32)
        transform: Scale { origin.y: specBox.height / 2; yScale: -1 }
        opacity: 0.16
    }
}
