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
    property bool outlined: false   // 次要按钮:细描边 + 极淡底色
    property color fillColor: Theme.accent
    property string tip
    property color iconTint: "transparent"
    readonly property alias hovered: mouse.containsMouse
    // 图标切换时弹跳一下 (收藏等状态型按钮)
    property bool popOnChange: false
    onIconChanged: if (popOnChange) pop.restart()
    signal clicked()

    width: 40
    height: 40

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: width / 2
        color: btn.filled ? btn.fillColor : (mouse.containsMouse ? (btn.outlined ? Theme.pressed : btn.hoverColor) : (btn.outlined ? Theme.hover : "transparent"))
        border.width: btn.outlined ? 1 : 0
        border.color: Theme.hairline
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    Icon {
        id: glyph
        anchors.centerIn: parent
        // 填充按钮在浅色强调色上自动切换为深色图标,保证对比度
        name: btn.filled && Theme.accentIsLight ? btn.icon + "-dark" : btn.icon
        size: btn.iconSize
        tint: btn.iconTint
        SequentialAnimation {
            id: pop
            ScaleAnimator { target: glyph; from: 0.6; to: 1.28; duration: 160; easing.type: Easing.OutCubic }
            ScaleAnimator { target: glyph; to: 1; duration: 260; easing.type: Easing.OutBack }
        }
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
