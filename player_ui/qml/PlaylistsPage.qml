import QtQuick
import PlayerUi

// 歌单墙:第一格是「新建」,其后是「我喜欢」与自建歌单;右键打开整个歌单的菜单
// 格子密度、边距与专辑墙相同 (同样宽度下列数一致)
Item {
    id: page
    // 内容滚到窗口按钮下面了:Main 据此在顶端铺一条磨砂条
    readonly property bool scrolled: grid.contentY - grid.originY > 8
    property int bottomInset: 140
    readonly property int gap: 28
    readonly property int sidePadding: Math.max(width > 1400 ? 64 : 44, Math.round((width - 2200) / 2))
    readonly property int minCell: 176
    readonly property int columns: Math.max(2, Math.floor((width - sidePadding * 2 + gap) / (minCell + gap)))
    readonly property real cellW: (width - sidePadding * 2 + gap) / columns

    function openMenu(item, px, py, pl) {
        const p = item.mapToItem(null, px, py)
        Nav.trackMenu(-1, p.x, p.y, { ids: playlists.trackIdsOf(pl.id), kind: "playlist", playlistId: pl.id,
                                      title: pl.name, tag: "playlist:" + pl.id })
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding - page.gap / 2
        anchors.rightMargin: page.sidePadding - page.gap / 2
        cellWidth: page.cellW
        cellHeight: page.cellW - page.gap + 84
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 64
        bottomMargin: page.bottomInset
        model: [{ create: true }].concat(playlists.playlists)
        SmoothScroll { flickable: grid }

        delegate: Item {
            id: cell
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property real side: width - page.gap
            readonly property bool hovered: m.containsMouse || playBtn.hovered
            // 当前队列就是这个歌单:播放键变成暂停 / 继续
            readonly property bool isQueue: !modelData.create && player.queueTag === "playlist:" + modelData.id

            Item {
                id: art
                x: page.gap / 2
                width: cell.side
                height: cell.side
                y: cell.hovered ? -5 : 0
                // 悬停略微放大 (与专辑卡片一致),按下轻收
                scale: m.pressed ? 0.97 : (cell.hovered && !modelData.create ? 1.02 : 1)
                Behavior on y { YAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
                Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }

                SoftShadow {
                    target: tile
                    spread: 20
                    offsetY: cell.hovered ? 18 : 10
                    strength: modelData.create ? 0 : (cell.hovered ? 0.9 : 0.55)
                    Behavior on offsetY { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                }
                Mosaic {
                    id: tile
                    anchors.fill: parent
                    visible: !modelData.create
                    covers: modelData.covers || []
                    favorite: !!modelData.builtin
                    decodeSize: Math.ceil(cell.side * 1.4)
                }
                // 新建
                Rectangle {
                    anchors.fill: parent
                    visible: !!modelData.create
                    radius: 10
                    color: cell.hovered ? Theme.accentAlpha(0.08) : "transparent"
                    border.width: 1.5
                    border.color: cell.hovered ? Theme.accentAlpha(0.7) : Theme.hairline
                    Behavior on color { ColorAnimation { duration: Theme.normal } }
                    Icon {
                        anchors.centerIn: parent
                        name: "plus"
                        size: 34
                        tint: cell.hovered ? Theme.accent : "transparent"
                        opacity: cell.hovered ? 1 : 0.45
                    }
                }
                // 悬停播放 (按歌单顺序,与歌单页的「播放」一致);这个歌单暂停中时常驻
                IconButton {
                    id: playBtn
                    readonly property bool shown: cell.hovered || (cell.isQueue && !player.playing)
                    enabled: !modelData.create && (modelData.count || 0) > 0
                    visible: enabled && opacity > 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    width: 46; height: 46
                    filled: true
                    icon: cell.isQueue && player.playing ? "pause" : "play"
                    iconSize: 19
                    opacity: shown ? 1 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                    onClicked: {
                        if (cell.isQueue) { player.toggle(); return }
                        const ids = playlists.trackIdsOf(modelData.id)
                        if (!ids.length) return
                        player.shuffle = false
                        Nav.picked(player.pickTracks(ids, 0, "playlist:" + modelData.id, true))
                    }
                }
            }
            Text {
                x: art.x
                y: cell.side + 14
                width: cell.side
                text: modelData.create ? qsTr("新建歌单") : (modelData.name || "")
                elide: Text.ElideRight
                color: !modelData.create ? (cell.isQueue ? Theme.accent : Theme.text) : (cell.hovered ? Theme.accent : Theme.textSecondary)
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                x: art.x
                y: cell.side + 34
                width: cell.side
                visible: !modelData.create
                text: modelData.summary || ""
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            MouseArea {
                id: m
                x: art.x
                width: cell.side
                height: cell.side + 52
                // 滚动时格子从鼠标下掠过,不逐个上浮
                hoverEnabled: !Theme.scrolling
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                z: -1
                onClicked: (e) => {
                    if (e.button === Qt.RightButton) {
                        if (!modelData.create) page.openMenu(m, e.x, e.y, modelData)
                        return
                    }
                    if (modelData.create) {
                        const id = playlists.create(playlists.suggestName())
                        Nav.renameNext = id
                        Nav.openPlaylist(id)
                    }
                    else Nav.openPlaylist(modelData.id)
                }
            }
        }
    }
}
