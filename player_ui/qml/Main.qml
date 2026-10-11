import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import PlayerUi

Window {
    id: root
    width: 1320
    height: 860
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: player.hasTrack ? player.title + " — " + player.artist : "Hi-Fi Player"
    color: Theme.base
    flags: Qt.Window | Qt.FramelessWindowHint

    Component.onCompleted: windowHelper.applyNativeFrame(root)

    // 强调色跟随当前专辑
    Binding { target: Theme; property: "accent"; value: player.accent }

    property bool detailShown: false
    property int pendingAlbum: -1

    // ======================= 场景层 (同时作为毛玻璃的采样源) =======================
    Item {
        id: scene
        anchors.fill: parent

        Aurora {
            anchors.fill: parent
        }

        Item {
            id: pages
            anchors.fill: parent
            anchors.leftMargin: Theme.navWidth

            AlbumWall {
                id: wall
                anchors.fill: parent
                bottomInset: Theme.islandHeight + 60
                opacity: root.detailShown || library.albumCount === 0 ? 0 : 1
                visible: opacity > 0
                scale: root.detailShown ? 0.985 : 1
                Behavior on opacity { OpacityAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
                Behavior on scale { ScaleAnimator { duration: Theme.slow; easing.type: Easing.OutQuint } }
                onOpenAlbum: (index, coverItem) => root.openAlbum(index, coverItem)
            }

            AlbumDetail {
                id: detail
                anchors.fill: parent
                bottomInset: Theme.islandHeight + 60
                opacity: root.detailShown ? 1 : 0
                visible: opacity > 0
                scale: root.detailShown ? 1 : 1.015
                Behavior on opacity { OpacityAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
                Behavior on scale { ScaleAnimator { duration: Theme.slow; easing.type: Easing.OutQuint } }
                onBack: root.closeAlbum()
            }

            // 曲库为空:引导选择文件夹;首次导入时显示识别进度
            Onboarding {
                anchors.fill: parent
                anchors.bottomMargin: 60
                visible: opacity > 0
                opacity: library.albumCount === 0 && !root.detailShown ? 1 : 0
                Behavior on opacity { OpacityAnimator { duration: Theme.slow; easing.type: Easing.OutCubic } }
                onPickFolder: folderDialog.open()
            }
        }

        NowPlaying {
            id: nowPlaying
            anchors.fill: parent
            z: 5
            onCloseRequested: shown = false
            onChainClicked: chain.shown = !chain.shown
        }

        SideNav {
            id: nav
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            current: root.detailShown ? "" : "albums"
            onNavigate: (target) => {
                if (target === "albums") root.closeAlbum()
                else if (target === "search") palette.open("")
                else if (target === "folder") library.folders.length ? libraryPanel.open() : folderDialog.open()
                else if (target === "rescan") library.rescan()
            }
        }
    }

    // ======================= 共享元素:飞行的封面 =======================
    RoundedImage {
        id: flying
        visible: false
        transformOrigin: Item.TopLeft
        radius: 16
        decodeSize: 640
        z: 50

        ParallelAnimation {
            id: flight
            property real toX
            property real toY
            property real toScale: 1
            property bool reverse: false
            XAnimator { target: flying; to: flight.toX; duration: 460; easing.type: Easing.OutQuint }
            YAnimator { target: flying; to: flight.toY; duration: 460; easing.type: Easing.OutQuint }
            ScaleAnimator { target: flying; to: flight.toScale; duration: 460; easing.type: Easing.OutQuint }
            onFinished: {
                flying.visible = false
                if (detail.heroCover) detail.heroCover.opacity = 1
            }
        }
    }

    function openAlbum(index, coverItem) {
        albumTracks.albumIndex = index
        detail.resetScroll()
        root.pendingAlbum = index
        const fromItem = coverItem
        detailShown = true
        // 等详情页完成布局后再计算目标位置
        Qt.callLater(function() {
            const hero = detail.heroCover
            if (!fromItem || !hero) return
            const from = fromItem.mapToItem(root.contentItem, 0, 0)
            const to = hero.mapToItem(root.contentItem, 0, 0)
            flying.source = albumTracks.cover
            flying.width = hero.width
            flying.height = hero.height
            flying.x = from.x
            flying.y = from.y
            flying.scale = fromItem.width / hero.width
            hero.opacity = 0
            flying.visible = true
            flight.toX = to.x
            flight.toY = to.y
            flight.toScale = 1
            flight.restart()
        })
    }

    function closeAlbum() {
        if (!detailShown) return
        const hero = detail.heroCover
        const card = wall.coverOf(albumTracks.albumIndex)
        if (hero && card) {
            const from = hero.mapToItem(root.contentItem, 0, 0)
            const to = card.mapToItem(root.contentItem, 0, 0)
            flying.source = albumTracks.cover
            flying.width = hero.width
            flying.height = hero.height
            flying.x = from.x
            flying.y = from.y
            flying.scale = 1
            flying.visible = true
            hero.opacity = 0
            flight.toX = to.x
            flight.toY = to.y
            flight.toScale = card.width / hero.width
            flight.restart()
        }
        detailShown = false
    }

    // 顶部渐暗:内容滚到窗口顶端时,保证窗口按钮在浅色封面上依然可见
    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: Theme.navWidth
        anchors.right: parent.right
        height: 56
        z: 9
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.38) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0) }
        }
    }

    // ======================= 隐形标题栏 =======================
    TitleBar {
        anchors.left: parent.left
        anchors.leftMargin: Theme.navWidth
        anchors.right: parent.right
        anchors.top: parent.top
        window: root
        z: 10
    }

    // ======================= 悬浮播放岛 =======================
    PlayIsland {
        id: island
        glassSource: scene
        width: Math.min(920, root.width - Theme.navWidth - 96)
        x: Theme.navWidth + (root.width - Theme.navWidth - width) / 2
        y: root.height - height - 24
        z: 20
        // 沉浸页有自己的控制区,岛下沉隐去
        opacity: nowPlaying.shown || (library.albumCount === 0 && !player.hasTrack) ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        onChainClicked: chain.shown = !chain.shown
        onExpandRequested: nowPlaying.shown = true
    }

    SignalChain {
        id: chain
        z: 30
        // 沉浸页中从左栏信号链行展开,其余情况从播放岛上方展开
        x: nowPlaying.shown ? Math.max(24, (root.width - width) / 2) : Math.min(root.width - width - 24, island.x + island.width - width - 10)
        y: nowPlaying.shown ? (root.height - height) / 2 : island.y - height - 12
    }
    MouseArea {
        anchors.fill: parent
        z: 29
        visible: chain.shown
        onClicked: chain.shown = false
    }

    ScanToast {
        // 首次导入时由引导页展示完整进度,不重复显示
        enabled: library.albumCount > 0
        anchors.horizontalCenter: island.horizontalCenter
        y: 18
        z: 25
    }

    // 播放失败提示
    Rectangle {
        z: 25
        anchors.horizontalCenter: island.horizontalCenter
        y: island.y - height - 12
        width: errText.implicitWidth + 32
        height: 34
        radius: 17
        color: Qt.rgba(0.25, 0.1, 0.08, 0.92)
        opacity: errTimer.running ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        Text {
            id: errText
            anchors.centerIn: parent
            text: player.errorText
            color: "#ffd2c4"
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
        Timer { id: errTimer; interval: 3500 }
        Connections { target: player; function onErrorChanged() { if (player.errorText.length) errTimer.restart() } }
    }

    CommandPalette {
        id: palette
        z: 40
        onOpenAlbum: (index) => root.openAlbum(index, null)
    }

    LibraryPanel {
        id: libraryPanel
        z: 40
        onPickFolder: folderDialog.open()
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("选择音乐文件夹")
        onAccepted: {
            library.addFolderUrls([selectedFolder])
            if (root.detailShown) root.closeAlbum()
        }
    }

    // ======================= 拖放添加文件夹 =======================
    DropArea {
        id: drop
        anchors.fill: parent
        z: 60
        keys: ["text/uri-list"]
        onDropped: (d) => {
            if (!d.hasUrls) return
            const urls = []
            for (let i = 0; i < d.urls.length; ++i) urls.push(d.urls[i])
            library.addFolderUrls(urls)
            d.acceptProposedAction()
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 14
            radius: Theme.radiusL
            color: Theme.accentAlpha(0.07)
            border.width: 1.5
            border.color: Theme.accentAlpha(0.6)
            opacity: drop.containsDrag ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
            Column {
                anchors.centerIn: parent
                spacing: 12
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "folder"; size: 40 }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("松开以添加到曲库")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    // ======================= 键盘 =======================
    Shortcut { sequence: "Ctrl+K"; onActivated: palette.shown ? palette.close() : palette.open("") }
    Shortcut { sequence: "Ctrl+F"; onActivated: palette.open("") }
    Shortcut { sequence: "Space"; enabled: !palette.shown; onActivated: player.toggle() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: player.next() }
    Shortcut { sequence: "Ctrl+Left"; onActivated: player.previous() }
    Shortcut {
        sequence: "Escape"
        enabled: !palette.shown
        onActivated: libraryPanel.shown ? libraryPanel.close()
                     : chain.shown ? (chain.shown = false) : (nowPlaying.shown ? (nowPlaying.shown = false) : root.closeAlbum())
    }

    // 在任意位置直接打字即唤出搜索
    Item {
        id: typeCatcher
        focus: !palette.shown
        Keys.onPressed: (e) => {
            if (palette.shown || e.modifiers & (Qt.ControlModifier | Qt.AltModifier)) return
            if (e.text.length === 1 && /\S/.test(e.text)) {
                palette.open(e.text)
                e.accepted = true
            }
        }
    }

    // ======================= 无边框缩放手柄 =======================
    Repeater {
        model: [
            { e: Qt.LeftEdge, c: Qt.SizeHorCursor }, { e: Qt.RightEdge, c: Qt.SizeHorCursor },
            { e: Qt.TopEdge, c: Qt.SizeVerCursor }, { e: Qt.BottomEdge, c: Qt.SizeVerCursor },
            { e: Qt.LeftEdge | Qt.TopEdge, c: Qt.SizeFDiagCursor }, { e: Qt.RightEdge | Qt.BottomEdge, c: Qt.SizeFDiagCursor },
            { e: Qt.RightEdge | Qt.TopEdge, c: Qt.SizeBDiagCursor }, { e: Qt.LeftEdge | Qt.BottomEdge, c: Qt.SizeBDiagCursor }
        ]
        delegate: MouseArea {
            readonly property int edges: modelData.e
            readonly property int grip: 6
            z: 100
            enabled: root.visibility !== Window.Maximized
            cursorShape: modelData.c
            x: (edges & Qt.RightEdge) ? root.width - grip : 0
            y: (edges & Qt.BottomEdge) ? root.height - grip : 0
            width: (edges & (Qt.LeftEdge | Qt.RightEdge)) && !(edges & (Qt.TopEdge | Qt.BottomEdge)) ? grip
                   : ((edges & (Qt.TopEdge | Qt.BottomEdge)) && !(edges & (Qt.LeftEdge | Qt.RightEdge)) ? root.width : grip * 2)
            height: (edges & (Qt.TopEdge | Qt.BottomEdge)) && !(edges & (Qt.LeftEdge | Qt.RightEdge)) ? grip
                    : ((edges & (Qt.LeftEdge | Qt.RightEdge)) && !(edges & (Qt.TopEdge | Qt.BottomEdge)) ? root.height : grip * 2)
            onPressed: root.startSystemResize(edges)
        }
    }

    // ======================= 自动化 (离屏截图验证用) =======================
    function automationOpenAlbum(i) { root.openAlbum(Number(i), wall.coverOf(Number(i))) }
    function automationBack() { root.closeAlbum() }
    function automationScrollEnd() { wall.scrollToEnd() }
    function automationSearch(q) { palette.open(String(q)) }
    function automationCloseSearch() { palette.close() }
    function automationShowChain() { chain.shown = true }
    function automationNowPlaying(on) { nowPlaying.shown = !!on }
    function automationPlayTitle(t) {
        for (let i = 0; i < library.albumCount; ++i)
            if (library.albumInfo(i).title === String(t)) { player.playAlbum(i, 0); return }
    }
    function automationHideChain() { chain.shown = false }
    function automationLibraryPanel(on) { on ? libraryPanel.open() : libraryPanel.close() }
}
