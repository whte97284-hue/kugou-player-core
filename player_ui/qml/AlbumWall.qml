import QtQuick
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
    // 曲库出现或刷新后的短暂窗口内创建的卡片依次浮现;之后滚动复用的卡片不再播放
    property bool entering: false
    Timer { id: enterWindow; interval: 900; onTriggered: wall.entering = false }
    Connections {
        target: library
        function onLibraryChanged() { wall.entering = true; enterWindow.restart() }
    }
    onVisibleChanged: if (visible && grid.count > 0 && !grid.userScrolled) { entering = true; enterWindow.restart() }

    function scrollToEnd() { grid.contentY = grid.originY + grid.contentHeight - grid.height + grid.bottomMargin }
    // 封面在网格视口里露出了多少 (0..1);完全滚出视野的不当作飞行落点
    function shownPart(item) {
        const p = item.mapToItem(grid, 0, 0)
        const top = Math.max(0, p.y), bottom = Math.min(grid.height, p.y + item.height)
        return item.height > 0 ? Math.max(0, bottom - top) / item.height : 0
    }
    // 关闭专辑时封面飞回的位置:优先网格里露出来的那张卡片,其次是顶部横幅;都不在视野内就不飞
    // (横幅显示的是刚打开过的专辑,滚下去之后它已在视野之外,飞向它看起来像往上跳走)
    function coverOf(index, preferred) {
        // 优先飞回打开时点的那一张 (它所在的格子 / 横幅仍是这张专辑,且露在视野里)
        if (preferred && shownPart(preferred) > 0.3) {
            let p = preferred
            while (p && p.albumIndex === undefined) p = p.parent
            if (p && p.albumIndex === index) return preferred
        }
        for (let i = 0; i < grid.contentItem.children.length; ++i) {
            const c = grid.contentItem.children[i]
            if (c.albumIndex === index && c.coverItem && c.visible && shownPart(c.coverItem) > 0.3) return c.coverItem
        }
        const h = grid.headerItem
        if (h && h.albumIndex === index && h.coverItem && shownPart(h.coverItem) > 0.3) return h.coverItem
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
        cacheBuffer: 600
        property bool userScrolled: false
        onMovementStarted: userScrolled = true
        SmoothScroll { flickable: grid; onScrolled: grid.userScrolled = true }
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
            Component.onCompleted: if (wall.entering) rise.start()
            transform: Translate { id: lift }
            SequentialAnimation {
                id: rise
                ScriptAction { script: { cell.opacity = 0; lift.y = 22 } }
                PauseAnimation { duration: Math.max(0, Math.min(420, (index % wall.columns) * 45 + Math.floor(index / wall.columns) * 70)) }
                ParallelAnimation {
                    OpacityAnimator { target: cell; to: 1; duration: 420; easing.type: Easing.OutCubic }
                    NumberAnimation { target: lift; property: "y"; to: 0; duration: 560; easing.type: Easing.OutQuint }
                }
            }
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
    }
}
