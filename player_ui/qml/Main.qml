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
    // 强调色:跟随封面,或设置页里选的固定色
    Binding { target: Theme; property: "accent"; value: settings.accent >= 0 ? settings.accentPresets[settings.accent] : player.accent }
    Binding { target: Theme; property: "reduced"; value: settings.reducedMotion }

    // 页面加载器:后台异步预创建 (不卡当前帧),之后常驻;进场稍慢带轻微放大,退场更快,两页交叠时间短
    component PageLoader: Loader {
        property string key
        property bool allowEmpty: false
        readonly property bool current: root.view === key && (allowEmpty || !pages.libraryEmpty)
        property bool used: false
        onCurrentChanged: if (current) used = true
        active: used
        asynchronous: !current
        anchors.fill: parent
        opacity: current && status === Loader.Ready ? 1 : 0
        visible: opacity > 0
        scale: current ? 1 : 0.985
        Behavior on opacity { OpacityAnimator { duration: current ? 300 : 150; easing.type: Easing.OutCubic } }
        Behavior on scale { ScaleAnimator { duration: current ? 460 : 150; easing.type: Easing.OutQuint } }
    }

    // 导航状态:section = 侧栏分区;detail = 分区内打开的详情 (album / artist / playlist)
    property string section: "albums"
    property string detailKind: ""
    property var detailRef: -1
    readonly property string view: detailKind.length ? detailKind : section
    readonly property bool detailShown: detailKind === "album"
    property var history: []
    // 详情页各自记住最后打开的对象,切到别的详情时不跟着重载
    property int lastArtist: -1
    property string lastPlaylist: ""
    onViewChanged: trackMenu.close()
    onDetailRefChanged: syncDetail()
    onDetailKindChanged: syncDetail()
    function syncDetail() {
        if (detailKind === "artist") lastArtist = detailRef
        else if (detailKind === "playlist") lastPlaylist = detailRef
    }

    function snapshotState() { return { section: section, kind: detailKind, ref: detailRef } }
    function pushHistory() {
        const h = history.slice()
        h.push(snapshotState())
        if (h.length > 40) h.shift()
        history = h
    }
    function applyState(st) {
        section = st.section
        detailKind = st.kind
        detailRef = st.ref
        if (st.kind === "album") albumTracks.albumIndex = st.ref
    }
    function go(sec) {
        if (section === sec && !detailKind.length) return
        pushHistory()
        section = sec
        detailKind = ""
    }
    function openDetail(kind, ref) {
        if (detailKind === kind && detailRef === ref) return
        pushHistory()
        detailKind = kind
        detailRef = ref
    }
    function back() {
        if (!history.length) {
            if (detailKind.length) { closeAlbumFlight(); detailKind = "" }
            return
        }
        const h = history.slice()
        const st = h.pop()
        history = h
        if (detailKind === "album" && st.kind === "") closeAlbumFlight()
        if (detailKind === "artist" && st.kind === "" && st.section === "artists") closeArtistFlight()
        applyState(st)
    }
    function openSearch(text) {
        if (section !== "search" || detailKind.length) { pushHistory(); section = "search"; detailKind = "" }
        Qt.callLater(function() { if (searchLoader.item) searchLoader.item.focusWith(text) })
    }
    function openArtist(i, fromItem) {
        if (i < 0) return
        openDetail("artist", i)
        if (!fromItem) return
        Qt.callLater(function() {
            const hero = artistLoader.item ? artistLoader.item.heroAvatar : null
            if (hero) fly(fromItem, null, hero, artistLoader)
        })
    }
    function openPlaylist(id) { if (id && id.length) openDetail("playlist", id) }

    Connections {
        target: Nav
        function onOpenAlbum(i) { if (i >= 0) root.openAlbum(i, null) }
        function onOpenArtist(i) { root.openArtist(i, null) }
        function onOpenArtistFrom(i, item) { root.openArtist(i, item) }
        function onOpenPlaylist(id) { root.openPlaylist(id) }
        function onTrackMenu(id, x, y, ctx) { trackMenu.open(id, x, y, ctx) }
        function onToast(icon, text) { toast.show(icon, text) }
    }

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
            readonly property int inset: Theme.islandHeight + 60
            readonly property bool libraryEmpty: library.albumCount === 0

            AlbumWall {
                id: wall
                anchors.fill: parent
                bottomInset: pages.inset
                readonly property bool active: root.view === "albums" && !pages.libraryEmpty
                opacity: active ? 1 : 0
                visible: opacity > 0
                scale: active ? 1 : 0.985
                Behavior on opacity { OpacityAnimator { duration: wall.active ? 300 : 150; easing.type: Easing.OutCubic } }
                Behavior on scale { ScaleAnimator { duration: wall.active ? 460 : 150; easing.type: Easing.OutQuint } }
                onOpenAlbum: (index, coverItem) => root.openAlbum(index, coverItem)
            }

            AlbumDetail {
                id: detail
                anchors.fill: parent
                bottomInset: pages.inset
                readonly property bool active: root.view === "album"
                opacity: active ? 1 : 0
                visible: opacity > 0
                scale: active ? 1 : 1.015
                Behavior on opacity { OpacityAnimator { duration: detail.active ? 300 : 150; easing.type: Easing.OutCubic } }
                Behavior on scale { ScaleAnimator { duration: detail.active ? 460 : 150; easing.type: Easing.OutQuint } }
                onBack: root.back()
            }

            // 其余页面首次进入时才创建,之后常驻 (切换不重建,滚动位置保留)
            PageLoader { id: artistsLoader; key: "artists"; sourceComponent: ArtistsPage { bottomInset: pages.inset } }
            PageLoader { id: songsLoader; key: "songs"; sourceComponent: SongsPage { bottomInset: pages.inset } }
            PageLoader { id: playlistsLoader; key: "playlists"; sourceComponent: PlaylistsPage { bottomInset: pages.inset } }
            PageLoader {
                id: searchLoader
                key: "search"
                allowEmpty: true
                sourceComponent: SearchPage { bottomInset: pages.inset; onBack: root.back() }
            }
            PageLoader { id: settingsLoader; key: "settings"; allowEmpty: true; sourceComponent: SettingsPage { bottomInset: pages.inset } }
            PageLoader { id: toolsLoader; key: "tools"; allowEmpty: true; sourceComponent: ToolsPage { bottomInset: pages.inset } }
            PageLoader {
                id: artistLoader
                key: "artist"
                sourceComponent: ArtistDetail { bottomInset: pages.inset; artistIndex: root.lastArtist; onBack: root.back() }
            }
            PageLoader {
                id: playlistLoader
                key: "playlist"
                allowEmpty: true
                sourceComponent: PlaylistDetail { bottomInset: pages.inset; playlistId: root.lastPlaylist; onBack: root.back() }
            }

            // 曲库就绪后在空闲时依次预创建其余页面,第一次切换也不会卡顿
            Timer {
                id: preload
                interval: 350
                repeat: true
                running: !pages.libraryEmpty && !library.scanning
                property int step: 0
                onTriggered: {
                    const list = [artistsLoader, songsLoader, playlistsLoader, searchLoader, artistLoader, playlistLoader]
                    if (step >= list.length) { stop(); return }
                    list[step++].used = true
                }
            }

            // 曲库为空:引导选择文件夹;首次导入时显示识别进度 (歌单与搜索页除外)
            Onboarding {
                anchors.fill: parent
                visible: opacity > 0
                opacity: pages.libraryEmpty && ["playlist", "playlists", "search", "tools", "settings"].indexOf(root.view) < 0 ? 1 : 0
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
            current: root.section
            onNavigate: (target) => {
                if (target === "folder") library.folders.length ? libraryPanel.open() : folderDialog.open()
                else if (target === "search") root.openSearch("")
                else root.go(target)
            }
        }
    }

    // ======================= 共享元素:飞行的封面 =======================
    // 两层:底层用来源图块已解码好的那张 (缓存命中,首帧就有画面),上层是落点尺寸的清晰版,就绪后直接盖上 (同一张图,只是更清晰)
    Item {
        id: flying
        visible: false
        transformOrigin: Item.TopLeft
        z: 50
        property real radius: 16
        property string loSource
        property int loDecode: 640
        property string hiSource
        property int hiDecode: 640

        RoundedImage {
            anchors.fill: parent
            radius: flying.radius
            source: flying.loSource
            decodeSize: flying.loDecode
            fadeIn: false
            placeholder: false
            outline: false
        }
        RoundedImage {
            id: flyingHi
            anchors.fill: parent
            radius: flying.radius
            source: flying.hiSource
            decodeSize: flying.hiDecode
            fadeIn: false
            placeholder: false
        }

        ParallelAnimation {
            id: flight
            property real toX
            property real toY
            property real toScale: 1
            property real toRadius: 10
            property Item landing: null // 落点:飞行期间隐藏,落地后显示
            property Item origin: null  // 起点:飞行期间隐藏,避免同一张封面出现两份
            XAnimator { target: flying; to: flight.toX; duration: 460; easing.type: Easing.OutQuint }
            YAnimator { target: flying; to: flight.toY; duration: 460; easing.type: Easing.OutQuint }
            ScaleAnimator { target: flying; to: flight.toScale; duration: 460; easing.type: Easing.OutQuint }
            // 缩放会连带缩小圆角:同步补偿,让屏幕上的圆角从起点平滑过渡到落点
            NumberAnimation { target: flying; property: "radius"; to: flight.toRadius; duration: 460; easing.type: Easing.OutQuint }
            onFinished: handoff.start()
        }
        // 落地交接:等落点图片解码完成再显示它,随后再撤掉飞行副本 (落点自己的淡入在副本下面完成,看不出切换)
        Timer {
            id: handoff
            interval: 16
            repeat: true
            property int waited: 0
            onRunningChanged: if (running) waited = 0
            onTriggered: {
                waited += interval
                const img = root.imageIn(flight.landing)
                if (img && img.status !== Image.Ready && img.status !== Image.Error && waited < 1500) return
                stop()
                if (flight.landing) flight.landing.opacity = 1
                if (flight.origin) flight.origin.opacity = 1
                if (detail.heroCover) detail.heroCover.opacity = 1
                detail.heroAway = false
                flight.landing = null
                flight.origin = null
                reveal.restart()
            }
        }
        Timer { id: reveal; interval: Theme.normal + 40; onTriggered: flying.visible = false }
    }

    // 图块里真正显示封面的 RoundedImage (来源可能是卡片容器或交叉淡化封面)
    function imageIn(item) {
        if (!item) return null
        if (item.status !== undefined && item.decodeSize !== undefined) return item
        let best = null
        for (let k = 0; k < item.children.length; ++k) {
            const c = imageIn(item.children[k])
            if (c && (!best || (c.opacity > best.opacity) || (c.status === Image.Ready && best.status !== Image.Ready))) best = c
        }
        return best
    }
    // 图块在屏幕上的矩形。page 正处于切页缩放 (0.985 / 1.015 → 1) 时,按它的缩放中心换算成停稳后的位置与尺寸,
    // 否则飞行落在缩放中途的位置,页面停稳后封面会再跳一下
    function settledRect(item, page) {
        const p = item.mapToItem(root.contentItem, 0, 0)
        const q = item.mapToItem(root.contentItem, item.width, item.height)
        const s = page && page.scale > 0 ? page.scale : 1
        if (s === 1) return Qt.rect(p.x, p.y, q.x - p.x, q.y - p.y)
        const c = page.mapToItem(root.contentItem, page.width / 2, page.height / 2)
        return Qt.rect(c.x + (p.x - c.x) / s, c.y + (p.y - c.y) / s, (q.x - p.x) / s, (q.y - p.y) / s)
    }
    function fly(fromItem, fromPage, toItem, toPage) {
        // 上一段飞行还没收尾:先把它藏起的图块还原
        flight.stop()
        handoff.stop()
        reveal.stop()
        if (flight.landing) flight.landing.opacity = 1
        if (flight.origin) flight.origin.opacity = 1
        flight.landing = null
        flight.origin = null

        const fromImg = imageIn(fromItem), toImg = imageIn(toItem)
        if (!fromImg || !toImg || fromItem.width <= 0 || toItem.width <= 0) return false
        const a = settledRect(fromItem, fromPage)
        const b = settledRect(toItem, toPage)
        if (a.width <= 0 || b.width <= 0) return false
        const fromRadius = (fromItem.radius !== undefined ? fromItem.radius : fromImg.radius) * a.width / fromItem.width
        const toRadius = (toItem.radius !== undefined ? toItem.radius : toImg.radius) * b.width / toItem.width

        const fromReady = fromImg.status === Image.Ready
        flying.loSource = fromReady ? fromImg.source : toImg.source
        flying.loDecode = fromReady ? fromImg.decodeSize : toImg.decodeSize
        flying.hiSource = toImg.source
        flying.hiDecode = toImg.decodeSize
        flying.width = b.width
        flying.height = b.height
        flying.x = a.x
        flying.y = a.y
        flying.scale = a.width / b.width
        flying.radius = fromRadius / flying.scale
        flight.landing = toItem
        flight.origin = fromItem
        toItem.opacity = 0
        fromItem.opacity = 0
        flying.visible = true
        flight.toX = b.x
        flight.toY = b.y
        flight.toScale = 1
        flight.toRadius = toRadius
        flight.restart()
        return true
    }

    function openAlbum(index, coverItem) {
        if (index < 0) return
        albumTracks.albumIndex = index
        detail.resetScroll()
        const fromItem = coverItem
        openDetail("album", index)
        // 从专辑墙打开时封面飞入详情页;其他入口直接淡入
        if (!fromItem) return
        Qt.callLater(function() {
            if (detail.heroCover) fly(fromItem, null, detail.heroCover, detail)
        })
    }

    // 返回专辑墙时封面飞回对应卡片 (专辑墙此刻仍在回弹缩放,落点按停稳后的位置算)
    function closeAlbumFlight() {
        if (detailKind !== "album" || section !== "albums") return
        const hero = detail.heroCover
        const card = wall.coverOf(albumTracks.albumIndex)
        if (!hero || !card) return
        if (fly(hero, null, card, wall)) detail.heroAway = true
    }
    function closeArtistFlight() {
        const page = artistLoader.item, grid = artistsLoader.item
        if (!page || !grid) return
        const hero = page.heroAvatar
        const cell = grid.avatarOf(root.lastArtist)
        if (hero && cell) fly(hero, null, cell, artistsLoader)
    }
    function closeAlbum() { if (detailKind === "album") back() }

    // 顶部渐暗:内容滚到窗口顶端时,保证窗口按钮在浅色封面上依然可见
    Rectangle {
        anchors.left: parent.left
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

    DecryptToast {
        z: 25
        anchors.horizontalCenter: island.horizontalCenter
        y: library.scanning ? 64 : 18
        Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
    }

    DecryptPrompt {
        id: decryptPrompt
        z: 24
        anchors.horizontalCenter: island.horizontalCenter
        y: island.y - height - 14
    }
    Connections {
        target: decrypt
        function onPromptRequested() { decryptPrompt.open() }
    }

    TrackMenu {
        id: trackMenu
        z: 45
    }

    Toast {
        id: toast
        z: 46
        anchors.horizontalCenter: island.horizontalCenter
        y: island.y - height - 16
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
            root.go("albums")
        }
    }

    // ======================= 拖放添加文件夹 =======================
    DropArea {
        id: drop
        anchors.fill: parent
        z: 60
        keys: ["text/uri-list"]
        enabled: root.view !== "tools"
        onDropped: (d) => {
            if (!d.hasUrls) return
            // 加密音频交给解密工具,其余 (文件夹 / 普通音频) 加入曲库
            const enc = [], other = []
            for (let i = 0; i < d.urls.length; ++i) {
                const u = String(d.urls[i])
                if (/\.(kgm|kgma|vpr|ncm|kgg)$/i.test(u)) enc.push(d.urls[i])
                else other.push(d.urls[i])
            }
            if (enc.length) decrypt.decryptUrls(enc)
            if (other.length) library.addFolderUrls(other)
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
            Icon { anchors.centerIn: parent; name: "folder"; size: 44; opacity: 0.9 }
        }
    }

    // ======================= 键盘 / 鼠标侧键 =======================
    readonly property bool typing: activeFocusItem && activeFocusItem.hasOwnProperty("cursorPosition")
    Shortcut { sequence: "Ctrl+K"; onActivated: root.openSearch("") }
    Shortcut { sequence: "Ctrl+F"; onActivated: root.openSearch("") }
    Shortcut { sequence: "Space"; enabled: !root.typing; onActivated: player.toggle() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: player.next() }
    Shortcut { sequence: "Ctrl+Left"; onActivated: player.previous() }
    Shortcut { sequence: "Alt+Left"; onActivated: root.back() }
    Shortcut {
        sequence: "Escape"
        enabled: !root.typing
        onActivated: {
            if (trackMenu.shown) trackMenu.close()
            else if (libraryPanel.shown) libraryPanel.close()
            else if (chain.shown) chain.shown = false
            else if (nowPlaying.shown) nowPlaying.shown = false
            else root.back()
        }
    }
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.BackButton
        onClicked: root.back()
    }

    // 在任意位置直接打字即进入搜索
    Item {
        id: typeCatcher
        focus: !root.typing
        Keys.onPressed: (e) => {
            if (e.modifiers & (Qt.ControlModifier | Qt.AltModifier)) return
            if (e.text.length === 1 && /\S/.test(e.text)) {
                root.openSearch(e.text)
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
    function automationBack() { root.back() }
    function automationScrollEnd() { wall.scrollToEnd() }
    function automationSearch(q) { root.openSearch(String(q)) }
    function automationCloseSearch() { root.back() }
    function automationShowChain() { chain.shown = true }
    function automationNowPlaying(on) { nowPlaying.shown = !!on }
    function automationPlayTitle(t) {
        for (let i = 0; i < library.albumCount; ++i)
            if (library.albumInfo(i).title === String(t)) { player.playAlbum(i, 0); return }
    }
    function automationHideChain() { chain.shown = false }
    function automationDecryptPrompt(on) { on ? decryptPrompt.open() : decryptPrompt.close() }
    function automationLibraryPanel(on) { on ? libraryPanel.open() : libraryPanel.close() }
    function automationGo(sec) { root.go(String(sec)) }
    function automationOpenArtist(name) { root.openArtist(library.artistIndexOf(String(name))) }
    function automationOpenPlaylist(id) { root.openPlaylist(String(id)) }
    function automationTrackMenu(x, y) { trackMenu.open(player.trackId, Number(x), Number(y), {}) }
}
