import QtQuick
import PlayerUi

// 字母索引条:贴在列表右缘,只列出实际存在的字母;点按或上下拖动即跳转
Item {
    id: rail
    property var letters: []
    property string current
    signal jump(string letter)
    width: 22
    height: col.height
    visible: letters.length > 1
    opacity: area.containsMouse || area.pressed ? 1 : 0.55
    Behavior on opacity { OpacityAnimator { duration: Theme.fast } }

    readonly property real cell: Math.min(18, Math.max(12, (parent ? parent.height * 0.7 : 400) / Math.max(1, letters.length)))

    Column {
        id: col
        width: parent.width
        Repeater {
            model: rail.letters
            Text {
                width: col.width
                height: rail.cell
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: modelData
                color: modelData === rail.current ? Theme.accent : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.weight: modelData === rail.current ? Font.Bold : Font.DemiBold
            }
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        function pick(y) {
            const i = Math.max(0, Math.min(rail.letters.length - 1, Math.floor((y - 6) / rail.cell)))
            if (rail.letters[i] !== undefined && rail.letters[i] !== rail.current) {
                rail.current = rail.letters[i]
                rail.jump(rail.letters[i])
            }
        }
        onPressed: (e) => pick(e.y)
        onPositionChanged: (e) => { if (pressed) pick(e.y) }
    }
}
