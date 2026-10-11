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

    // 投影:悬停时加深并下沉,配合封面上浮形成「拿起」的纵深感
    SoftShadow {
        target: coverBox
        spread: 22
        offsetY: card.hovered ? 20 : 12
        strength: card.hovered ? 1.0 : 0.62
        Behavior on strength { NumberAnimation { duration: Theme.normal } }
        Behavior on offsetY { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
    }

    Item {
        id: coverBox
        width: parent.width
        height: width
        y: card.hovered ? -6 : 0
        scale: card.hovered ? 1.02 : 1
        Behavior on y { YAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }

        RoundedImage {
            anchors.fill: parent
            radius: 10
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
                name: Theme.accentIsLight ? "play-dark" : "play"
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

        // 正在播放角标:只有正在播放的那张卡片才创建
        Loader {
            active: card.isPlaying
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 10
            sourceComponent: Rectangle {
                width: 30; height: 24; radius: 6
                color: Qt.rgba(0, 0, 0, 0.55)
                PlayingBars { anchors.centerIn: parent; maxHeight: 12 }
            }
        }
    }

    Column {
        y: coverBox.width + 14
        width: parent.width
        spacing: 3
        Text {
            width: parent.width
            text: card.title
            elide: Text.ElideRight
            color: card.isPlaying ? Theme.accent : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
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
