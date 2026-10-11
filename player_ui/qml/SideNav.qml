import QtQuick
import PlayerUi

// 极窄导航条:平时只有图标,悬停展开显示文字 (覆盖在内容之上,不推动布局)
Item {
    id: nav
    property string current: "albums"
    signal navigate(string target)
    width: Theme.navWidth

    readonly property bool expanded: hoverArea.containsMouse

    Rectangle {
        id: panel
        width: nav.expanded ? 196 : Theme.navWidth
        height: parent.height
        color: nav.expanded ? Theme.panel : Qt.rgba(0, 0, 0, 0.12)
        Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.hairline }
        clip: true

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }

        // 品牌标识:强调色唱片
        Item {
            x: 18
            y: 20
            width: 36
            height: 36
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.accentAlpha(0.18)
                border.width: 1.5
                border.color: Theme.accent
            }
            Rectangle {
                anchors.centerIn: parent
                width: 8; height: 8; radius: 4
                color: Theme.accent
            }
            RotationAnimator on rotation {
                from: 0; to: 360; duration: 6000
                loops: Animation.Infinite
                running: player.playing
            }
        }
        Text {
            x: 68
            y: 26
            text: "Hi-Fi Player"
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
            opacity: nav.expanded ? 1 : 0
            Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
        }

        Column {
            y: 92
            width: parent.width
            spacing: 6
            Repeater {
                model: [
                    { key: "albums", icon: "albums", label: qsTr("专辑") },
                    { key: "search", icon: "search", label: qsTr("搜索  Ctrl K") },
                    { key: "folder", icon: "folder", label: qsTr("添加音乐文件夹") },
                    { key: "rescan", icon: "refresh", label: qsTr("重新扫描") }
                ]
                delegate: Item {
                    width: panel.width
                    height: 46
                    readonly property bool active: nav.current === modelData.key
                    Rectangle {
                        x: 12
                        width: parent.width - 24
                        height: parent.height
                        radius: Theme.radiusM
                        color: active ? Theme.accentAlpha(0.16) : (itemMouse.containsMouse ? Theme.hover : "transparent")
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    Rectangle {
                        visible: active
                        x: 0; anchors.verticalCenter: parent.verticalCenter
                        width: 3; height: 18; radius: 1.5
                        color: Theme.accent
                    }
                    Icon {
                        x: 25
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.icon
                        size: 22
                        opacity: active || itemMouse.containsMouse ? 1 : 0.6
                    }
                    Text {
                        x: 64
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: active ? Theme.text : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        opacity: nav.expanded ? 1 : 0
                        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                    }
                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: nav.navigate(modelData.key)
                    }
                }
            }
        }
    }
}
