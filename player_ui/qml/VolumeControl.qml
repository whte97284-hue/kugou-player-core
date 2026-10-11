import QtQuick
import PlayerUi

// 音量:图标点击静音/恢复;悬停时向左展开滑条
Item {
    id: vol
    width: row.width
    height: 34
    property real lastVolume: 0.85
    readonly property bool open: hover.containsMouse || slider.dragging

    Row {
        id: row
        height: parent.height
        spacing: 4
        layoutDirection: Qt.RightToLeft
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: player.volume < 0.01 ? "mute" : "volume"
            iconSize: 17
            width: 34; height: 34
            onClicked: {
                if (player.volume > 0.01) { vol.lastVolume = player.volume; player.volume = 0 }
                else player.volume = vol.lastVolume
            }
        }
        Item {
            width: vol.open ? 84 : 0
            height: parent.height
            clip: true
            Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
            SeekBar {
                id: slider
                anchors.verticalCenter: parent.verticalCenter
                x: 4
                width: 76
                showTip: false
                position: player.volume
                duration: 1
                onSeekRequested: (v) => player.volume = v
                onDragValueChanged: if (dragging) player.volume = dragValue
            }
        }
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        anchors.margins: -6
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
