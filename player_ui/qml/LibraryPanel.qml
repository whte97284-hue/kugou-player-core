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
    function artistCount() { return library.generation >= 0 ? artistModel.rowCount() : 0 }

    MouseArea {
        anchors.fill: parent
        enabled: lp.shown
        onClicked: lp.close()
        onWheel: (w) => w.accepted = true
    }

    Rectangle {
        id: card
        x: Theme.navWidth + 12
        // 从侧栏底部的文件夹按钮旁弹出
        y: lp.height - height - 18
        width: 420
        height: body.implicitHeight + 36
        radius: Theme.radiusL
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.1)
        opacity: lp.shown ? 1 : 0
        scale: lp.shown ? 1 : 0.97
        transformOrigin: Item.BottomLeft
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

            // 概览:曲目 / 专辑数量 (图标 + 数字)
            Row {
                spacing: 18
                height: 28
                Repeater {
                    model: [{ icon: "note", n: library.trackCount }, { icon: "albums", n: library.albumCount }, { icon: "artist", n: artistModel.letters.length >= 0 ? artistCount() : 0 }]
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: modelData.icon; size: 15; opacity: 0.5 }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.n.toLocaleString(Qt.locale(), "f", 0)
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                    }
                }
            }
            Item { width: 1; height: 10 }

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


            Item { width: 1; height: 12 }
            Rectangle { width: body.width; height: 1; color: Theme.hairline }
            Item { width: 1; height: 12 }

            Row {
                spacing: 12
                IconButton {
                    width: 44; height: 44
                    filled: true
                    icon: "plus"
                    iconSize: 18
                    onClicked: { lp.close(); lp.pickFolder() }
                }
                // 重新扫描;扫描进行中变为取消
                IconButton {
                    visible: library.folders.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40
                    outlined: true
                    icon: library.scanning ? "close" : "refresh"
                    iconSize: 16
                    onClicked: library.scanning ? library.cancelScan() : library.rescan()
                }
            }
        }
    }
}
