import QtQuick
import PlayerUi

// 侧边导航条:固定宽度、纯图标,不再悬停展开 (不遮挡内容、不抖动)
//  · 选中项背后一块圆角底板随切换平滑滑动,左缘一道强调色短线同步移动
//  · 悬停图标提亮并轻微放大,按下回弹;停留片刻在右侧浮出名称提示
Item {
    id: nav
    property string current: "albums"
    signal navigate(string target)
    width: Theme.navWidth

    readonly property var items: [
        { key: "albums", icon: "albums", tip: qsTr("专辑") },
        { key: "artists", icon: "artist", tip: qsTr("歌手") },
        { key: "songs", icon: "note", tip: qsTr("歌曲") },
        { key: "playlists", icon: "playlist", tip: qsTr("歌单") },
        { key: "search", icon: "search", tip: qsTr("搜索  Ctrl K") }
    ]
    readonly property int itemSize: 48
    readonly property int itemGap: 8
    readonly property int listTop: 56
    readonly property int currentIndex: {
        for (let i = 0; i < items.length; ++i) if (items[i].key === current) return i
        return -1
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.16)
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.hairline }
    }

    // 滑动的选中底板
    Rectangle {
        id: pill
        visible: nav.currentIndex >= 0
        x: (nav.width - nav.itemSize) / 2
        y: nav.listTop + Math.max(0, nav.currentIndex) * (nav.itemSize + nav.itemGap)
        width: nav.itemSize
        height: nav.itemSize
        radius: 14
        color: Theme.accentAlpha(0.15)
        Behavior on y { NumberAnimation { duration: 340; easing.type: Easing.OutQuint } }
    }
    Rectangle {
        visible: pill.visible
        x: 0
        y: pill.y + (nav.itemSize - 20) / 2
        width: 3
        height: 20
        radius: 1.5
        color: Theme.accent
    }

    Column {
        y: nav.listTop
        x: (nav.width - nav.itemSize) / 2
        spacing: nav.itemGap
        Repeater {
            model: nav.items
            delegate: navButton
        }
    }

    // 底部:解密工具 · 音乐文件夹
    Column {
        x: (nav.width - nav.itemSize) / 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        spacing: nav.itemGap
        Loader {
            sourceComponent: navButton
            property var modelData: ({ key: "tools", icon: "tools", tip: qsTr("转换") })
            // 曲库里有待解密文件时,图标右上角一个强调色小点
            Rectangle {
                x: 31; y: 11
                width: 7; height: 7; radius: 3.5
                color: Theme.accent
                visible: (decrypt.pending.total || 0) > 0 && nav.current !== "tools"
                SequentialAnimation on scale {
                    running: parent.visible
                    loops: Animation.Infinite
                    ScaleAnimator { from: 1; to: 1.35; duration: 900; easing.type: Easing.InOutSine }
                    ScaleAnimator { from: 1.35; to: 1; duration: 900; easing.type: Easing.InOutSine }
                }
            }
        }
        Loader {
            sourceComponent: navButton
            property var modelData: ({ key: "folder", icon: "folder", tip: qsTr("音乐文件夹") })
        }
    }

    Component {
        id: navButton
        Item {
            id: btn
            width: nav.itemSize
            height: nav.itemSize
            readonly property bool active: nav.current === modelData.key
            Icon {
                anchors.centerIn: parent
                name: modelData.icon
                size: 22
                tint: btn.active ? Theme.accent : "transparent"
                opacity: btn.active || m.containsMouse ? 1 : 0.5
                scale: m.pressed ? 0.88 : (m.containsMouse ? 1.08 : 1)
                Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                Behavior on scale { ScaleAnimator { duration: 180; easing.type: Easing.OutBack } }
            }
            Rectangle {
                anchors.fill: parent
                radius: 14
                color: Theme.accentAlpha(0.15)
                visible: btn.active && nav.currentIndex < 0
            }
            Rectangle {
                anchors.fill: parent
                radius: 14
                color: Theme.hover
                opacity: m.containsMouse && !btn.active ? 1 : 0
                Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
            }
            MouseArea {
                id: m
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: nav.navigate(modelData.key)
                onContainsMouseChanged: {
                    if (containsMouse) { tip.target = btn; tip.text = modelData.tip; tipDelay.restart() }
                    else if (tip.target === btn) { tipDelay.stop(); tip.target = null }
                }
            }
        }
    }

    // 名称提示:悬停 450ms 后在图标右侧浮出
    Timer { id: tipDelay; interval: 450 }
    Rectangle {
        id: tip
        property Item target: null
        property string text
        parent: nav.parent
        z: 200
        x: nav.width + 6
        y: target ? target.mapToItem(nav.parent, 0, 0).y + (target.height - height) / 2 : 0
        width: tipText.implicitWidth + 22
        height: 30
        radius: 8
        color: Theme.tooltip
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.08)
        opacity: target && !tipDelay.running ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
        Text {
            id: tipText
            anchors.centerIn: parent
            text: tip.text
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }
}
