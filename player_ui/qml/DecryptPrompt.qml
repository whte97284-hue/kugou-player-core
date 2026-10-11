import QtQuick
import PlayerUi

// 扫描发现未解密文件时的询问卡片:锁 + 数量 + 各格式计数;解锁键开始解密,× 表示这一批不再询问
// 悬停解锁键时锁扣弹开,点下后卡片收起,进度转到顶部的小胶囊
Item {
    id: prompt
    anchors.fill: parent
    visible: card.opacity > 0
    property bool shown: false
    function open() { shown = true }
    function close() { shown = false }

    readonly property var info: decrypt.pending
    readonly property bool hotUnlock: go.containsMouse

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.45)
        opacity: prompt.shown ? 1 : 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        MouseArea { anchors.fill: parent; enabled: prompt.shown; onClicked: prompt.close(); onWheel: (w) => w.accepted = true }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 420
        height: col.implicitHeight + 64
        radius: 26
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.09)
        opacity: prompt.shown ? 1 : 0
        scale: prompt.shown ? 1 : 0.92
        Behavior on opacity { OpacityAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on scale { ScaleAnimator { duration: 420; easing.type: Easing.OutBack } }
        MouseArea { anchors.fill: parent }

        Column {
            id: col
            anchors.horizontalCenter: parent.horizontalCenter
            y: 34
            width: parent.width - 64
            spacing: 0

            // 锁:强调色圆底,悬停解锁键时变为打开的锁并轻轻弹起
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 76; height: 76
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.accentAlpha(prompt.hotUnlock ? 0.22 : 0.12)
                    Behavior on color { ColorAnimation { duration: Theme.normal } }
                }
                Icon {
                    anchors.centerIn: parent
                    name: prompt.hotUnlock ? "unlock" : "lock"
                    size: 32
                    tint: Theme.accent
                    y: prompt.hotUnlock ? -3 : 0
                    scale: prompt.hotUnlock ? 1.08 : 1
                    Behavior on scale { ScaleAnimator { duration: 260; easing.type: Easing.OutBack } }
                }
            }
            Item { width: 1; height: 18 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: (prompt.info.total || 0).toLocaleString(Qt.locale(), "f", 0)
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 44
                font.weight: Font.Light
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Theme.formatBytes(prompt.info.bytes || 0)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            Item { width: 1; height: 18 }
            // 各格式计数;工具不可用的格式置灰并带琥珀色小点
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8
                Repeater {
                    model: prompt.info.kinds || []
                    Rectangle {
                        height: 30
                        width: chipRow.implicitWidth + 24
                        radius: 15
                        color: Theme.hover
                        border.width: 1
                        border.color: Theme.hairline
                        opacity: modelData.available ? 1 : 0.55
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 8
                            Rectangle {
                                visible: !modelData.available
                                anchors.verticalCenter: parent.verticalCenter
                                width: 6; height: 6; radius: 3
                                color: Theme.warn
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.id.toUpperCase()
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.8
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.count
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
            Item { width: 1; height: 28 }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14
                // 这一批不再询问
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48; height: 48
                    outlined: true
                    icon: "close"
                    iconSize: 16
                    onClicked: { decrypt.dismissPending(); prompt.close() }
                }
                // 解密
                Rectangle {
                    width: 168; height: 52
                    radius: 26
                    color: Theme.accent
                    scale: go.pressed ? 0.96 : (go.containsMouse ? 1.03 : 1)
                    Behavior on scale { ScaleAnimator { duration: Theme.fast; easing.type: Easing.OutCubic } }
                    Icon {
                        anchors.centerIn: parent
                        name: Theme.accentIsLight ? "unlock-dark" : "unlock"
                        size: 22
                    }
                    MouseArea {
                        id: go
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { decrypt.decryptPending(); prompt.close() }
                    }
                }
            }
            Item { width: 1; height: 20 }
            // 以后自动解密
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: autoRow.implicitWidth
                height: 26
                Row {
                    id: autoRow
                    anchors.centerIn: parent
                    spacing: 8
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "auto"
                        size: 14
                        tint: decrypt.policy === 1 ? Theme.accent : "transparent"
                        opacity: decrypt.policy === 1 ? 1 : 0.5
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("以后自动解密")
                        color: decrypt.policy === 1 ? Theme.accent : Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: decrypt.policy = decrypt.policy === 1 ? 0 : 1
                }
            }
        }
    }
}
