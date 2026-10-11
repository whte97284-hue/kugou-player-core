import QtQuick
import PlayerUi

// 音乐文件夹管理:从侧栏弹出的卡片。列出已添加的文件夹 (可移除),添加 / 重新扫描,显示上次扫描结果
Item {
    id: lp
    property bool shown: false
    signal pickFolder()
    anchors.fill: parent
    visible: card.opacity > 0

    function open() { shown = true }
    function close() { shown = false }

    MouseArea {
        anchors.fill: parent
        enabled: lp.shown
        onClicked: lp.close()
        onWheel: (w) => w.accepted = true
    }

    Rectangle {
        id: card
        x: Theme.navWidth + 12
        y: 84
        width: 420
        height: body.implicitHeight + 36
        radius: Theme.radiusL
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.1)
        opacity: lp.shown ? 1 : 0
        scale: lp.shown ? 1 : 0.97
        transformOrigin: Item.TopLeft
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
        // 吞掉卡片内的点击,避免穿透到背后的关闭区域
        MouseArea { anchors.fill: parent }

        Column {
            id: body
            x: 20
            y: 18
            width: parent.width - 40
            spacing: 0

            Text {
                text: qsTr("音乐文件夹")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Item { width: 1; height: 4 }
            Text {
                text: library.scanning ? library.statusText
                                       : qsTr("%1 首歌曲 · %2 张专辑").arg(library.trackCount).arg(library.albumCount)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            Item { width: 1; height: 14 }

            Repeater {
                model: library.folders
                delegate: Item {
                    width: body.width
                    height: 44
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: -8
                        anchors.rightMargin: -8
                        radius: Theme.radiusS
                        color: rowMouse.containsMouse ? Theme.hover : "transparent"
                    }
                    MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    Icon {
                        id: fIcon
                        anchors.verticalCenter: parent.verticalCenter
                        name: "folder"
                        size: 17
                        opacity: 0.5
                    }
                    Text {
                        anchors.left: fIcon.right
                        anchors.leftMargin: 12
                        anchors.right: rm.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        elide: Text.ElideMiddle
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                    IconButton {
                        id: rm
                        anchors.right: parent.right
                        anchors.rightMargin: -6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30; height: 30
                        icon: "close"
                        iconSize: 13
                        opacity: rowMouse.containsMouse ? 1 : 0.35
                        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                        onClicked: library.removeFolder(modelData)
                    }
                }
            }
            Text {
                visible: library.folders.length === 0
                height: 44
                verticalAlignment: Text.AlignVCenter
                text: qsTr("还没有添加文件夹")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }

            Item { width: 1; height: 12 }
            Rectangle { width: body.width; height: 1; color: Theme.hairline }
            Item { width: 1; height: 12 }

            Row {
                spacing: 8
                Repeater {
                    model: [
                        { key: "add", label: qsTr("添加文件夹"), primary: true },
                        { key: "rescan", label: library.scanning ? qsTr("取消扫描") : qsTr("重新扫描"), primary: false }
                    ]
                    delegate: Rectangle {
                        width: lbl.implicitWidth + 32
                        height: 36
                        radius: 18
                        color: modelData.primary ? Theme.accent : (m.containsMouse ? Theme.pressed : Theme.hover)
                        visible: modelData.key !== "rescan" || library.folders.length > 0
                        scale: m.pressed ? 0.97 : 1
                        Behavior on scale { ScaleAnimator { duration: Theme.fast } }
                        Text {
                            id: lbl
                            anchors.centerIn: parent
                            text: modelData.label
                            color: modelData.primary ? Theme.onAccent : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: modelData.primary ? Font.DemiBold : Font.Normal
                        }
                        MouseArea {
                            id: m
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.key === "add") { lp.close(); lp.pickFolder() }
                                else if (library.scanning) library.cancelScan()
                                else library.rescan()
                            }
                        }
                    }
                }
            }
        }
    }
}
