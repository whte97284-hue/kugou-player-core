import QtQuick
import PlayerUi

// 发现未解密文件:播放岛上方滑出一条细胶囊 —— 锁 · 数量 · 格式,× 本批不再询问,解锁键开始解密
// 不遮挡、不打断,忽略它也可以正常使用
Rectangle {
    id: prompt
    property bool shown: false
    function open() { shown = true }
    function close() { shown = false }

    readonly property var info: decrypt.pending
    width: row.implicitWidth + 24
    height: 48
    radius: 24
    color: Theme.panel
    border.width: 1
    border.color: Qt.rgba(1, 0.96, 0.9, 0.09)
    opacity: shown ? 1 : 0
    visible: opacity > 0
    transform: Translate {
        y: prompt.shown ? 0 : 18
        Behavior on y { NumberAnimation { duration: 420; easing.type: Easing.OutQuint } }
    }
    Behavior on opacity { OpacityAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }

    // 曲库里的待解密文件全部处理完 (或被移除) 后自动收起
    Connections {
        target: decrypt
        function onPendingChanged() { if ((decrypt.pending.total || 0) === 0) prompt.close() }
    }

    Row {
        id: row
        x: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        Icon { anchors.verticalCenter: parent.verticalCenter; name: go.containsMouse ? "unlock" : "lock"; size: 16; tint: Theme.accent }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: (prompt.info.total || 0).toLocaleString(Qt.locale(), "f", 0)
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: {
                const k = prompt.info.kinds || []
                return k.map(x => x.id.toUpperCase() + " " + x.count).join("  ·  ")
            }
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
        Item { width: 4; height: 1 }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 32; height: 32
            icon: "close"
            iconSize: 12
            restOpacity: 0.5
            onClicked: { decrypt.dismissPending(); prompt.close() }
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 52; height: 32
            radius: 16
            color: Theme.accent
            scale: go.pressed ? 0.94 : 1
            Behavior on scale { ScaleAnimator { duration: Theme.fast } }
            Icon { anchors.centerIn: parent; name: Theme.accentIsLight ? "unlock-dark" : "unlock"; size: 15 }
            MouseArea {
                id: go
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { decrypt.decryptPending(); prompt.close() }
            }
        }
    }
}
