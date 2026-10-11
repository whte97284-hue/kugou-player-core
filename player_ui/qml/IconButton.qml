import QtQuick
import PlayerUi

// 圆形图标按钮:悬停出现底色,按下轻微缩小 (渲染线程动画)
Item {
    id: btn
    property string icon
    property int iconSize: 20
    property real restOpacity: 0.78
    property color hoverColor: Theme.hover
    property bool filled: false
    property color fillColor: Theme.accent
    property string tip
    signal clicked()

    width: 40
    height: 40

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: width / 2
        color: btn.filled ? btn.fillColor : (mouse.containsMouse ? btn.hoverColor : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Icon {
        anchors.centerIn: parent
        // 填充按钮在浅色强调色上自动切换为深色图标,保证对比度
        name: btn.filled && Qt.colorEqual(Theme.onAccent, "#15120e") ? btn.icon + "-dark" : btn.icon
        size: btn.iconSize
        opacity: btn.filled ? 1 : (mouse.containsMouse ? 1 : btn.restOpacity)
        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
    }
    scale: mouse.pressed ? 0.9 : 1
    Behavior on scale { ScaleAnimator { duration: Theme.fast; easing.type: Easing.OutCubic } }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
