import QtQuick
import PlayerUi

// 播放列表抽屉:从右侧滑出,停在播放条上方。标题带曲目数,右上角「清空」;
// 每行封面 · 歌名 / 歌手 · 时长,正在播放的一行用强调色;悬停出现移除按钮,双击 / 单击播放
Item {
    id: panel
    property bool shown: false
    signal closeRequested()

    visible: slide.x < width
    clip: false

    // 点面板外关闭 (不拦截播放条:面板与遮罩都只覆盖播放条以上的区域)
    MouseArea {
        parent: panel.parent
        x: 0; y: 0
        width: panel.x
        height: panel.height
        z: panel.z - 1
        enabled: panel.shown
        visible: panel.shown
        onClicked: panel.closeRequested()
        onWheel: (w) => w.accepted = false
    }

    Item {
        id: slide
        width: parent.width
        height: parent.height
        x: panel.shown ? 0 : width + 40
        Behavior on x { NumberAnimation { duration: panel.shown ? 460 : 300; easing.type: panel.shown ? Easing.OutQuint : Easing.InCubic } }

        SoftShadow {
            target: sheet
            spread: 40
            offsetY: 0
            strength: 0.85
        }
        Rectangle {
            id: sheet
            // 右边与底边伸出窗口 / 藏到播放条下面,只露出左上角的圆角
            anchors.fill: parent
            anchors.rightMargin: -20
            anchors.bottomMargin: -20
            radius: 16
            color: Qt.rgba(0.082, 0.075, 0.068, 0.985)
            border.width: 1
            border.color: Theme.hairline
            MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = false }
        }

        // ---- 标题 ----
        Row {
            id: head
            x: 24
            y: 22
            spacing: 6
            Text {
                text: qsTr("播放列表")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 20
                font.weight: Font.Bold
            }
            Text {
                y: 2
                text: player.queueLength
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }
        // 清空:只保留正在播放的这一首
        Rectangle {
            id: clearBtn
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: head.verticalCenter
            height: 30
            width: clearRow.implicitWidth + 20
            radius: 15
            color: clearMouse.containsMouse ? Theme.hover : "transparent"
            opacity: player.queueLength > 1 ? 1 : 0.35
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            Row {
                id: clearRow
                anchors.centerIn: parent
                spacing: 6
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "trash"; size: 14; opacity: 0.7 }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("清空")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
            }
            MouseArea {
                id: clearMouse
                anchors.fill: parent
                hoverEnabled: true
                enabled: player.queueLength > 1
                cursorShape: Qt.PointingHandCursor
                onClicked: player.clearQueue()
            }
        }

        ListView {
            id: list
            anchors.top: head.bottom
            anchors.topMargin: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            x: 10
            width: parent.width - 20
            clip: true
            model: player.queueModel
            spacing: 2
            cacheBuffer: 600
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            SmoothScroll { flickable: list }
            // 打开时把正在播放的那首放在视野里 (上方留一行已播放的作参照)
            Connections {
                target: panel
                function onShownChanged() { if (panel.shown) list.positionViewAtIndex(Math.max(0, player.queuePosition - 1), ListView.Beginning) }
            }
            // 新增 / 移除的行轻轻滑动,而不是瞬间跳位
            displaced: Transition { NumberAnimation { property: "y"; duration: 280; easing.type: Easing.OutCubic } }

            delegate: Item {
                id: row
                width: list.width
                height: 62
                readonly property bool current: model.queueIndex === player.queuePosition
                readonly property bool played: model.queueIndex < player.queuePosition
                readonly property bool hovered: rowMouse.containsMouse || rm.hovered
                // 抽屉打开时,可见的行自上而下依次滑入淡入
                transform: Translate { id: rowShift }
                Connections {
                    target: panel
                    function onShownChanged() {
                        if (!panel.shown || Theme.reduced) return
                        const rel = Math.max(0, Math.min(10, Math.round((row.y - list.contentY) / row.height)))
                        rowIn.delay = 60 + rel * 28
                        rowIn.restart()
                    }
                }
                SequentialAnimation {
                    id: rowIn
                    property int delay: 0
                    ScriptAction { script: { row.opacity = 0; rowShift.x = 28 } }
                    PauseAnimation { duration: rowIn.delay }
                    ParallelAnimation {
                        NumberAnimation { target: row; property: "opacity"; to: 1; duration: 320; easing.type: Easing.OutCubic }
                        NumberAnimation { target: rowShift; property: "x"; to: 0; duration: 520; easing.type: Easing.OutQuint }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusM
                    color: row.current ? Theme.accentAlpha(0.10) : (row.hovered ? Theme.hover : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (e) => {
                        if (e.button === Qt.RightButton) {
                            const p = mapToItem(null, e.x, e.y)
                            Nav.trackMenu(model.trackId, p.x, p.y, {})
                        } else if (!row.current) player.playQueueIndex(model.queueIndex)
                    }
                }
                RoundedImage {
                    id: qc
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 44
                    radius: 7
                    source: model.cover
                    decodeSize: 88
                    opacity: row.played ? 0.55 : 1
                }
                Loader {
                    anchors.centerIn: qc
                    active: row.current
                    sourceComponent: Rectangle {
                        width: 44; height: 44; radius: 7
                        color: Qt.rgba(0, 0, 0, 0.5)
                        PlayingBars { anchors.centerIn: parent; maxHeight: 14 }
                    }
                }
                Column {
                    anchors.left: qc.right
                    anchors.leftMargin: 14
                    anchors.right: dur.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        width: parent.width
                        text: model.title
                        elide: Text.ElideRight
                        color: row.current ? Theme.accent : (row.played ? Theme.textSecondary : Theme.text)
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        font.weight: row.current ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        width: parent.width
                        text: model.artist
                        elide: Text.ElideRight
                        color: row.current ? Theme.accentAlpha(0.75) : Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
                Text {
                    id: dur
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.duration
                    opacity: row.hovered && !row.current ? 0 : 1
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                }
                IconButton {
                    id: rm
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 30
                    icon: "close"
                    iconSize: 12
                    enabled: !row.current
                    opacity: row.hovered && !row.current ? 1 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                    onClicked: player.removeFromQueue(model.queueIndex)
                }
            }
        }
        Text {
            anchors.centerIn: list
            visible: player.queueLength === 0
            text: qsTr("播放列表是空的")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }
}
