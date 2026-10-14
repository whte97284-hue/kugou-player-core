import QtQuick
import PlayerUi

// 轻提示:图标 + 一句话,1.8 秒后淡出
Rectangle {
    id: toast
    property string icon
    property string text
    function show(i, t) { icon = i; text = t; hold.restart() }
    width: row.implicitWidth + 36
    height: 40
    radius: 20
    color: Theme.panel
    border.width: 1
    border.color: Qt.rgba(1, 0.96, 0.9, 0.09)
    opacity: hold.running ? 1 : 0
    visible: opacity > 0
    scale: hold.running ? 1 : 0.94
    Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
    Timer { id: hold; interval: 1800 }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: toast.text
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }
}
