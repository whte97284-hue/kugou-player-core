import QtQuick
import PlayerUi

// 曲目行:序号 / 律动图标、标题 (+ 与专辑歌手不同时显示歌手)、品质、时长
Item {
    id: row
    property int number
    property string title
    property string artist
    property string albumArtist
    property string quality
    property string duration
    property bool current: false
    signal activated()
    height: 54

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusM
        color: row.current ? Theme.accentAlpha(0.12) : (mouse.containsMouse ? Theme.hover : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    // 序号 / 正在播放律动
    Item {
        id: lead
        x: 14
        width: 32
        height: parent.height
        Text {
            anchors.centerIn: parent
            visible: !row.current && !mouse.containsMouse
            text: row.number
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
        Icon {
            anchors.centerIn: parent
            visible: !row.current && mouse.containsMouse
            name: "play"
            size: 14
        }
        Row {
            visible: row.current
            anchors.centerIn: parent
            height: 14
            spacing: 2
            Repeater {
                model: 3
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: 3
                    radius: 1.5
                    height: 3 + 11 * (player.playing ? (player.spectrum[3 + index * 7] || 0) : 0.2)
                    color: Theme.accent
                }
            }
        }
    }

    Column {
        anchors.left: lead.right
        anchors.leftMargin: 14
        anchors.right: meta.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            width: parent.width
            text: row.title
            elide: Text.ElideRight
            color: row.current ? Theme.accent : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: row.current ? Font.DemiBold : Font.Normal
        }
        Text {
            width: parent.width
            visible: row.artist.length > 0 && row.artist !== row.albumArtist
            text: row.artist
            elide: Text.ElideRight
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }

    Row {
        id: meta
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 18
        QualityBadge {
            text: row.quality
            anchors.verticalCenter: parent.verticalCenter
            opacity: mouse.containsMouse || row.current ? 1 : 0.55
        }
        Text {
            width: 44
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text: row.duration
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.activated()
    }
}
