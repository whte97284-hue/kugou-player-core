import QtQuick
import QtQuick.Controls.Basic
import PlayerUi

// 专辑墙:列数随宽度自适应,卡片宽度精确铺满;代理复用,缓冲区预加载封面
Item {
    id: wall
    signal openAlbum(int index, Item coverItem)
    property int bottomInset: 140

    readonly property int sidePadding: width > 1400 ? 56 : 40
    readonly property int minCell: 176
    readonly property int columns: Math.max(2, Math.floor((width - sidePadding * 2 + 24) / (minCell + 24)))
    readonly property real cellW: (width - sidePadding * 2) / columns

    // 与用户手动滚到底完全一致:最大 contentY 包含底部留白
    function scrollToEnd() { grid.contentY = grid.originY + grid.contentHeight - grid.height + grid.bottomMargin }
    function coverOf(index) {
        if (grid.headerItem && grid.headerItem.albumIndex === index && grid.headerItem.coverItem
                && grid.contentY < grid.headerItem.height * 0.6)
            return grid.headerItem.coverItem
        for (let i = 0; i < grid.contentItem.children.length; ++i) {
            const c = grid.contentItem.children[i]
            if (c.albumIndex === index && c.coverItem) return c.coverItem
        }
        return null
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: wall.sidePadding
        anchors.rightMargin: wall.sidePadding
        model: albumModel
        cellWidth: wall.cellW
        cellHeight: wall.cellW - 28 + 84
        cacheBuffer: 1200
        property bool userScrolled: false
        onMovementStarted: userScrolled = true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        maximumFlickVelocity: 5200
        flickDeceleration: 2600
        topMargin: 0
        bottomMargin: wall.bottomInset

        header: HeroBanner {
            width: grid.width
            onOpenAlbum: (index, coverItem) => wall.openAlbum(index, coverItem)
            // 扫描完成后横幅变高:用户尚未滚动时保持停在顶部,避免横幅被推出视野
            onHeightChanged: if (!grid.userScrolled) Qt.callLater(grid.positionViewAtBeginning)
        }

        delegate: Item {
            id: cell
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property int albumIndex: model.albumIndex
            readonly property Item coverItem: cardItem.coverItem
            AlbumCard {
                id: cardItem
                width: cell.width - 28
                height: cell.height
                title: model.title
                artist: model.artist
                badge: model.badge
                cover: model.cover
                albumIndex: model.albumIndex
                onOpen: wall.openAlbum(model.albumIndex, cardItem.coverItem)
                onPlay: player.playAlbum(model.albumIndex, 0)
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            contentItem: Rectangle {
                implicitWidth: 4
                radius: 2
                color: Qt.rgba(1, 1, 1, parent.pressed ? 0.4 : 0.18)
            }
        }
    }

    // 空曲库引导
    Column {
        visible: library.albumCount === 0 && !library.scanning
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -40
        spacing: 18
        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "disc"; size: 64; opacity: 0.35 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("你的母带库还是空的")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("添加一个音乐文件夹,FLAC / WAV / DSD / APE / M4A / CUE 都会被识别")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }
}
