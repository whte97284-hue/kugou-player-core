import QtQuick
import PlayerUi

// 专辑卡片:悬停封面上浮放大 + 浮出播放键;正在播放的专辑显示律动角标
Item {
    id: card
    property string title
    property string artist
    property string badge
    property string cover
    property int albumIndex: -1
    property bool isPlaying: player.albumIndex === albumIndex && player.hasTrack
    readonly property alias coverItem: coverBox
    signal open()
    signal play()

    readonly property bool hovered: mouse.containsMouse || playMouse.containsMouse

    Item {
        id: coverBox
        width: parent.width
        height: width
        scale: card.hovered ? 1.035 : 1
        Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }

        // 悬停时的强调色柔光 (只动 opacity)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            radius: Theme.radiusM + 6
            color: Theme.accentAlpha(0.22)
            opacity: card.hovered ? 1 : 0
            Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        }
        RoundedImage {
            anchors.fill: parent
            source: card.cover
            decodeSize: Math.ceil(card.width * 1.5)
        }

        // 播放按钮
        Rectangle {
            id: playBtn
            width: 46; height: 46; radius: 23
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            color: Theme.accent
            opacity: card.hovered ? 1 : 0
            scale: card.hovered ? (playMouse.pressed ? 0.92 : 1) : 0.7
            Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
            Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutBack } }
            Icon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 1
                name: Qt.colorEqual(Theme.onAccent, "#15120e") ? "play-dark" : "play"
                size: 20
            }
            MouseArea {
                id: playMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: card.play()
            }
        }

        // 正在播放角标
        Rectangle {
            visible: card.isPlaying
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 10
            width: 30; height: 24; radius: 6
            color: Qt.rgba(0, 0, 0, 0.55)
            Row {
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: 4
                    Rectangle {
                        width: 3
                        radius: 1.5
                        anchors.bottom: parent.bottom
                        height: 4 + 10 * (player.playing ? player.spectrum[4 + index * 5] : 0.15)
                        color: Theme.accent
                    }
                }
                height: 14
            }
        }
    }

    Column {
        anchors.top: coverBox.bottom
        anchors.topMargin: 12
        width: parent.width
        spacing: 3
        Text {
            width: parent.width
            text: card.title
            elide: Text.ElideRight
            color: card.isPlaying ? Theme.accent : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        Row {
            width: parent.width
            spacing: 6
            Text {
                width: Math.min(implicitWidth, parent.width - (badgeItem.visible ? badgeItem.width + 6 : 0))
                text: card.artist
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
            QualityBadge {
                id: badgeItem
                text: card.badge
                fontSize: 9
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    MouseArea {
        id: mouse
        width: coverBox.width
        height: coverBox.height + 52
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.open()
        z: -1
    }
}
