import QtQuick
import PlayerUi

// 歌单墙:第一格是「新建」,其后是「我喜欢」与自建歌单
Item {
    id: page
    property int bottomInset: 140
    readonly property int sidePadding: width > 1400 ? 56 : 40
    readonly property int minCell: 190
    readonly property int columns: Math.max(2, Math.floor((width - sidePadding * 2 + 24) / (minCell + 24)))
    readonly property real cellW: (width - sidePadding * 2) / columns

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding
        anchors.rightMargin: page.sidePadding
        cellWidth: page.cellW
        cellHeight: page.cellW - 28 + 72
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 64
        bottomMargin: page.bottomInset
        model: [{ create: true }].concat(playlists.playlists)
        SmoothScroll { flickable: grid }

        delegate: Item {
            id: cell
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property real side: width - 28
            readonly property bool hovered: m.containsMouse

            Item {
                id: art
                width: cell.side
                height: cell.side
                y: cell.hovered ? -5 : 0
                scale: m.pressed ? 0.97 : 1
                Behavior on y { YAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
                Behavior on scale { ScaleAnimator { duration: Theme.fast } }

                SoftShadow { target: tile; spread: 20; offsetY: cell.hovered ? 18 : 10; strength: modelData.create ? 0 : (cell.hovered ? 0.9 : 0.55) }
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
                // 悬停播放
                IconButton {
                    visible: !modelData.create && (modelData.count || 0) > 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    width: 46; height: 46
                    filled: true
                    icon: "play"
                    iconSize: 19
                    opacity: cell.hovered ? 1 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                    onClicked: {
                        const ids = playlists.trackIdsOf(modelData.id)
                        if (ids.length) player.playTracks(ids, 0)
                    }
                }
            }
            Text {
                y: cell.side + 14
                width: cell.side
                visible: !modelData.create
                text: modelData.name || ""
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
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
                width: cell.side
                height: cell.side + 52
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                z: -1
                onClicked: {
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
