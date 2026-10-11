import QtQuick
import PlayerUi

// 隐形标题栏:整条可拖动,双击最大化/还原;右上角极简窗口按钮
Item {
    id: bar
    property var window
    height: 44

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: bar.window.startSystemMove()
        onDoubleClicked: bar.toggleMaximize()
    }

    function toggleMaximize() {
        if (window.visibility === Window.Maximized) window.showNormal()
        else window.showMaximized()
    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        Repeater {
            model: [
                { icon: "minimize", action: "min" },
                { icon: bar.window && bar.window.visibility === Window.Maximized ? "restore" : "maximize", action: "max" },
                { icon: "close", action: "close" }
            ]
            delegate: Item {
                width: 46
                height: 34
                Rectangle {
                    anchors.fill: parent
                    color: modelData.action === "close" ? "#e5484d" : Theme.hover
                    opacity: hover.containsMouse ? 1 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                }
                Icon {
                    anchors.centerIn: parent
                    name: modelData.icon
                    size: 14
                    opacity: hover.containsMouse ? 1 : 0.62
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (modelData.action === "min") bar.window.showMinimized()
                        else if (modelData.action === "max") bar.toggleMaximize()
                        else bar.window.close()
                    }
                }
            }
        }
    }
}
