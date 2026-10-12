import QtQuick
import PlayerUi

// 沉浸播放页 v3:两栏网格,每个元素都有用途
//  左栏「播放卡」:封面 → 歌名歌手 → 进度 → 控制 → 音量 → 一行信号链,全部与封面左右对齐
//  右栏:歌词 (当前行居中高亮、平滑滚动、点击跳转);无歌词时为「接下来播放」
//  背景:暖炭黑 + 封面后的强调色柔光 + 右下一束很淡的辅助色光,只用两种颜色
Item {
    id: np
    property bool shown: false
    signal closeRequested()
    signal chainClicked()
    // 共享元素飞行用:大封面,以及它此刻离停稳位置还差多少 (进场时左栏仍在上浮)
    readonly property Item coverItem: cover
    readonly property real coverSettleDy: -cardRise.y

    // 进场错峰:左栏各行依次上浮淡入 (order 越大越晚),退场同时下沉,收起时不拖沓
    component Rise: Item {
        property int order: 0
        width: parent ? parent.width : 0
        height: childrenRect.height
        opacity: np.shown ? 1 : 0
        Behavior on opacity { SequentialAnimation { PauseAnimation { duration: np.shown ? 70 + order * 45 : 0 } OpacityAnimator { duration: np.shown ? 420 : 160; easing.type: Easing.OutCubic } } }
        transform: Translate {
            y: np.shown ? 0 : 22 + order * 6
            Behavior on y { SequentialAnimation { PauseAnimation { duration: np.shown ? 70 + order * 45 : 0 } NumberAnimation { duration: np.shown ? 720 : 200; easing.type: Easing.OutQuint } } }
        }
    }

    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { OpacityAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
    // 完全淡入后为 true:此时底下的曲库页面与极光背景被整屏盖住,可以停止绘制它们
    property bool covering: false
    onShownChanged: if (shown) coverDelay.restart(); else { coverDelay.stop(); covering = false }
    Timer { id: coverDelay; interval: Theme.normal + 40; onTriggered: np.covering = true }

    // ---------------- 背景 ----------------
    Rectangle { anchors.fill: parent; color: Theme.base }
    MouseArea { anchors.fill: parent; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    readonly property real padX: Math.max(64, width * 0.07)
    readonly property real contentW: Math.min(width - padX * 2, 1240)
    readonly property real originX: (width - contentW) / 2
    readonly property real coverSize: Math.max(280, Math.min(height * 0.46, contentW * 0.36, 440))
    readonly property real gap: Math.max(64, contentW * 0.07)
    readonly property real columnH: coverSize + 300
    readonly property real topY: Math.max(64, (height - columnH) / 2)

    ShaderEffect {
        x: np.originX + np.coverSize / 2 - width / 2
        y: np.topY + np.coverSize / 2 - height / 2
        width: np.coverSize * 2.6
        height: width
        property color color: Theme.accent
        property real intensity: 0.2 + 0.16 * player.breath
        fragmentShader: "qrc:/shaders/glow.frag.qsb"
    }
    ShaderEffect {
        x: np.width * 0.55
        y: np.height * 0.45
        width: np.width * 0.8
        height: np.height
        property color color: player.palette && player.palette.length > 2 ? Qt.lighter(player.palette[2], 2.2) : Theme.accent
        property real intensity: 0.12
        fragmentShader: "qrc:/shaders/glow.frag.qsb"
    }
    ShaderEffect {
        anchors.fill: parent
        property size size: Qt.size(width, height)
        property real strength: 0.022
        fragmentShader: "qrc:/shaders/grain.frag.qsb"
    }

    // 收起
    IconButton {
        x: 24
        y: 20
        icon: "back"
        iconSize: 18
        rotation: -90
        onClicked: np.closeRequested()
    }

    // ---------------- 左栏:播放卡 ----------------
    Item {
        id: card
        // 进场:左栏先上浮,右栏稍晚跟上;退场同步下沉
        transform: Translate {
            id: cardRise
            y: np.shown ? 0 : 36
            Behavior on y { NumberAnimation { duration: 520; easing.type: Easing.OutQuint } }
        }
        x: np.originX
        y: np.topY
        width: np.coverSize
        height: np.columnH

        // 封面舞台:暂停时整张封面连同投影微微后退 (0.86),继续播放时带一点回弹地浮回来
        Item {
            id: coverStage
            width: np.coverSize
            height: np.coverSize
            scale: player.playing || !player.hasTrack ? 1 : 0.86
            Behavior on scale { ScaleAnimator { duration: player.playing ? 680 : 520; easing.type: player.playing ? Easing.OutBack : Easing.OutQuint; easing.overshoot: 1.2 } }
            SoftShadow { target: cover; spread: 40; offsetY: player.playing ? 26 : 12; strength: player.playing ? 0.95 : 0.6
                Behavior on offsetY { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                Behavior on strength { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } } }
            CrossfadeCover {
                id: cover
                width: np.coverSize
                height: np.coverSize
                radius: 14
                source: player.cover
                decodeSize: 1024
                // 随呼吸极轻微地起伏 (最多 1%),只让人「感觉」它活着
                scale: 1 + 0.01 * player.breath
            }
        }

        Column {
            anchors.top: coverStage.bottom
            anchors.topMargin: 26
            width: parent.width
            spacing: 0

            Rise { order: 0; Column { id: titleBlock; width: parent.width
            // 切歌:新歌名与歌手从右侧轻轻滑入
            transform: Translate { id: titleShift }
            Connections {
                target: player
                function onTrackChanged() { if (np.shown) titleIn.restart() }
            }
            ParallelAnimation {
                id: titleIn
                NumberAnimation { target: titleShift; property: "x"; from: 18; to: 0; duration: 620; easing.type: Easing.OutQuint }
                OpacityAnimator { target: titleBlock; from: 0; to: 1; duration: 420; easing.type: Easing.OutCubic }
            }
            Item {
                width: parent.width
                height: titleText.implicitHeight
                Text {
                    id: titleText
                    width: parent.width - 40
                    text: player.title
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                IconButton {
                    readonly property bool fav: playlists.revision >= 0 && player.hasTrack && playlists.isFavorite(player.trackId)
                    anchors.right: parent.right
                    anchors.rightMargin: -8
                    anchors.verticalCenter: titleText.verticalCenter
                    width: 38; height: 38
                    icon: fav ? "heart-filled" : "heart"
                    iconTint: fav ? Theme.accent : "transparent"
                    popOnChange: true
                    iconSize: 19
                    onClicked: playlists.toggleFavorite(player.trackId)
                }
            }
            Text {
                topPadding: 4
                width: parent.width
                text: player.artist + (player.album.length ? "  —  " + player.album : "")
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 15
            }
            }}

            Rise { order: 1; Column { width: parent.width
            Item { width: 1; height: 22 }
            SeekBar {
                id: seek
                width: parent.width
                position: player.position
                duration: player.duration
                onSeekRequested: (s) => player.seek(s)
            }
            Item {
                width: parent.width
                height: 18
                Text {
                    text: seek.fmt(seek.dragging ? seek.dragValue : player.position)
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
                Text {
                    anchors.right: parent.right
                    text: "-" + seek.fmt(Math.max(0, player.duration - (seek.dragging ? seek.dragValue : player.position)))
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
            }
            }}

            Rise { order: 2; Column { width: parent.width
            Item { width: 1; height: 14 }
            Item {
                width: parent.width
                height: 66
                IconButton {
                    anchors.left: parent.left
                    anchors.leftMargin: -8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40
                    icon: "shuffle"
                    iconSize: 17
                    iconTint: player.shuffle ? Theme.accent : "transparent"
                    restOpacity: player.shuffle ? 1 : 0.5
                    onClicked: player.shuffle = !player.shuffle
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: -8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40
                    icon: player.repeatMode === 2 ? "repeat-one" : "repeat"
                    iconSize: 17
                    iconTint: player.repeatMode > 0 ? Theme.accent : "transparent"
                    restOpacity: player.repeatMode > 0 ? 1 : 0.5
                    onClicked: player.repeatMode = (player.repeatMode + 1) % 3
                }
                Row {
                    anchors.centerIn: parent
                    spacing: 30
                    IconButton { icon: "prev"; iconSize: 22; width: 48; height: 48; anchors.verticalCenter: parent.verticalCenter; onClicked: player.previous() }
                    IconButton {
                        icon: player.playing ? "pause" : "play"
                        iconSize: 26
                        width: 66; height: 66
                        filled: true
                        onClicked: player.toggle()
                    }
                    IconButton { icon: "next"; iconSize: 22; width: 48; height: 48; anchors.verticalCenter: parent.verticalCenter; onClicked: player.next() }
                }
            }
            }}

            Rise { order: 3; Column { width: parent.width
            Item { width: 1; height: 16 }
            Row {
                width: parent.width
                spacing: 12
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "mute"; size: 15; opacity: 0.45 }
                SeekBar {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 15 * 2 - 24
                    showTip: false
                    position: player.volume
                    duration: 1
                    onSeekRequested: (v) => player.volume = v
                    onDragValueChanged: if (dragging) player.volume = dragValue
                }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "volume"; size: 15; opacity: 0.45 }
            }
            }}

            Rise { order: 4; Column { width: parent.width
            Item { width: 1; height: 22 }
            // 音质小胶囊:平时只有一个点与「24/96」这类简写,安静地居中;
            // 悬停时胶囊平滑拉宽、换成完整信号链,点击展开详情
            Item {
                width: parent.width
                height: 28
                Rectangle {
                    id: qcap
                    readonly property bool open: chainMouse.containsMouse
                    readonly property real shortW: shortText.implicitWidth + 40
                    readonly property real longW: Math.min(parent.width, longText.implicitWidth + 40)
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 28
                    width: open ? longW : shortW
                    radius: 14
                    color: Qt.rgba(1, 0.96, 0.9, open ? 0.07 : 0.035)
                    border.width: 1
                    border.color: Theme.hairline
                    clip: true
                    visible: player.hasTrack
                    Behavior on width { NumberAnimation { duration: 420; easing.type: Easing.OutQuint } }
                    Behavior on color { ColorAnimation { duration: Theme.normal } }
                    Rectangle {
                        id: dot
                        x: 15
                        anchors.verticalCenter: parent.verticalCenter
                        width: 5; height: 5; radius: 2.5
                        color: player.bitPerfect ? Theme.accent : Theme.textTertiary
                    }
                    Text {
                        id: shortText
                        x: 27
                        anchors.verticalCenter: parent.verticalCenter
                        text: player.quality
                        opacity: qcap.open ? 0 : 1
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                    }
                    Text {
                        id: longText
                        x: 27
                        width: qcap.longW - 40
                        anchors.verticalCenter: parent.verticalCenter
                        text: player.chainSummary
                        elide: Text.ElideRight
                        opacity: qcap.open ? 1 : 0
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        Behavior on opacity { SequentialAnimation { PauseAnimation { duration: qcap.open ? 120 : 0 } OpacityAnimator { duration: Theme.normal } } }
                    }
                    MouseArea {
                        id: chainMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: np.chainClicked()
                    }
                }
            }
            }}
        }
    }

    // ---------------- 右栏:歌词 / 接下来播放 ----------------
    Item {
        id: side
        opacity: np.shown ? 1 : 0
        Behavior on opacity { SequentialAnimation { PauseAnimation { duration: np.shown ? 90 : 0 } OpacityAnimator { duration: 380; easing.type: Easing.OutCubic } } }
        transform: Translate {
            y: np.shown ? 0 : 48
            Behavior on y { SequentialAnimation { PauseAnimation { duration: np.shown ? 90 : 0 } NumberAnimation { duration: 560; easing.type: Easing.OutQuint } } }
        }
        x: card.x + card.width + np.gap
        y: np.topY
        width: np.originX + np.contentW - x
        height: np.columnH

        readonly property bool hasLyrics: player.lyrics.length > 0
        property string tab: hasLyrics ? "lyrics" : "queue"
        onHasLyricsChanged: tab = hasLyrics ? "lyrics" : "queue"

        Row {
            id: tabs
            spacing: 8
            Repeater {
                model: [{ key: "lyrics", icon: "lyrics" }, { key: "queue", icon: "queue" }]
                Item {
                    width: 40
                    height: 34
                    readonly property bool active: side.tab === modelData.key
                    Icon {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -3
                        name: modelData.icon
                        size: 20
                        tint: active ? Theme.accent : "transparent"
                        opacity: active || tm.containsMouse ? 1 : 0.45
                        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 18
                        height: 2
                        radius: 1
                        color: Theme.accent
                        opacity: active ? 1 : 0
                        scale: active ? 1 : 0.3
                        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                        Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
                    }
                    MouseArea {
                        id: tm
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: side.tab = modelData.key
                    }
                }
            }
        }

        // 歌词:当前句停在视口上方约 1/3 处。换句时各行带错峰延迟依次跟上 (像一道波浪),
        // 当前句随演唱从左到右逐渐点亮;手动滚动后暂停跟随,片刻后平滑回到当前句
        ListView {
            id: lyricView
            anchors.top: tabs.bottom
            anchors.topMargin: 18
            width: parent.width
            height: parent.height - tabs.height - 18
            clip: true
            visible: side.tab === "lyrics"
            model: player.lyrics
            spacing: 6
            topMargin: player.lyricsSynced ? height * anchorRatio : 0
            bottomMargin: player.lyricsSynced ? height * 0.6 : 0
            cacheBuffer: 3000 // 一首歌的歌词基本全部实例化,波浪动画对视口外的行也成立
            boundsBehavior: Flickable.StopAtBounds
            readonly property real anchorRatio: 0.3
            property bool following: true
            property int anchoredIndex: -1

            // 当前句的 Y (内容坐标) → 让它停在视口锚点处的 contentY
            function targetFor(idx) {
                const it = itemAtIndex(idx)
                return it ? it.y - height * anchorRatio : NaN
            }
            function follow(wave) {
                if (!player.lyricsSynced || !following || !visible) return
                const idx = player.lyricIndex
                if (idx < 0) { settle.stop(); contentY = -topMargin; anchoredIndex = idx; return }
                let t = targetFor(idx)
                if (isNaN(t)) { positionViewAtIndex(idx, ListView.Beginning); t = targetFor(idx) }
                if (isNaN(t)) return
                const delta = contentY - t
                settle.stop()
                anchoredIndex = idx
                // 跳转 / 拖动进度条:整体平滑滑到新位置
                if (Math.abs(delta) >= height * 0.5 && wave) { settle.to = t; settle.restart(); return }
                contentY = t
                // 相邻换句才做波浪;跳转 / 拖动进度条时直接到位
                if (wave && Math.abs(delta) > 0.5 && Math.abs(delta) < height * 0.5 && !Theme.reduced) {
                    for (let k = 0; k < contentItem.children.length; ++k) {
                        const c = contentItem.children[k]
                        if (c.kick) c.kick(delta, idx)
                    }
                }
            }
            function pauseFollow() {
                following = false
                resume.restart()
            }
            Timer {
                id: resume
                interval: 2600
                onTriggered: {
                    lyricView.following = true
                    const t = lyricView.targetFor(player.lyricIndex)
                    if (!isNaN(t) && player.lyricIndex >= 0) { settle.to = t; settle.restart() }
                    else lyricView.follow(false)
                }
            }
            NumberAnimation { id: settle; target: lyricView; property: "contentY"; duration: 760; easing.type: Easing.OutQuint }
            SmoothScroll { flickable: lyricView; onScrolled: lyricView.pauseFollow() }
            onMovementStarted: pauseFollow()
            onVisibleChanged: if (visible) Qt.callLater(follow, false)
            onCountChanged: { following = true; Qt.callLater(follow, false) }
            onHeightChanged: Qt.callLater(follow, false)
            Connections {
                target: player
                function onLyricIndexChanged() { lyricView.follow(true) }
            }
            Connections {
                target: np
                function onShownChanged() { if (np.shown) { lyricView.following = true; Qt.callLater(lyricView.follow, false) } }
            }

            delegate: Item {
                id: row
                width: lyricView.width
                height: line.implicitHeight + 14
                readonly property int dist: Math.abs(index - player.lyricIndex)
                readonly property bool current: player.lyricsSynced && index === player.lyricIndex
                readonly property bool sung: player.lyricsSynced && index < player.lyricIndex

                // 波浪:内容整体已瞬间移到新位置,本行先留在原处 (偏移 delta),
                // 按与当前句的距离错峰延迟后再弹回;当前句及之前的行最先动
                transform: Translate { id: lag }
                function kick(delta, idx) {
                    wave.stop()
                    lag.y += delta
                    const rel = index - idx
                    waveDelay.duration = rel < -1 ? 0 : Math.min(9, rel + 1) * 34
                    wave.start()
                }
                SequentialAnimation {
                    id: wave
                    PauseAnimation { id: waveDelay }
                    NumberAnimation { target: lag; property: "y"; to: 0; duration: 760; easing.type: Easing.OutBack; easing.overshoot: 0.7 }
                }

                Text {
                    id: line
                    width: Math.min(row.width, implicitWidth)
                    y: 7
                    text: modelData.text
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: player.lyricsSynced ? 24 : 19
                    font.weight: player.lyricsSynced ? Font.DemiBold : Font.Medium
                    lineHeight: 1.2
                    opacity: !player.lyricsSynced ? 0.72 : row.current ? 1 : Math.max(0.2, (row.sung ? 0.5 : 0.56) - row.dist * 0.08)
                    scale: row.current || !player.lyricsSynced ? 1 : 0.94
                    transformOrigin: Item.Left
                    Behavior on opacity { OpacityAnimator { duration: 420; easing.type: Easing.OutCubic } }
                    Behavior on scale { ScaleAnimator { duration: 520; easing.type: Easing.OutQuint } }

                    // 当前句:未唱部分压暗,随演唱进度从左到右点亮 (交界处约 28px 柔和过渡)
                    layer.enabled: row.current && !Theme.reduced
                    layer.effect: ShaderEffect {
                        readonly property real start: modelData.time
                        readonly property real span: modelData.duration > 0 ? modelData.duration : 3
                        property real progress: row.current ? Math.max(0, Math.min(1, (player.position + 0.12 - start) / span)) : 0
                        property real lines: Math.max(1, line.lineCount)
                        property real soft: 14 / Math.max(1, line.width)
                        property real dim: 0.42
                        fragmentShader: "qrc:/shaders/wipe.frag.qsb"
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: modelData.time >= 0
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { lyricView.following = true; resume.stop(); player.seek(modelData.time) }
                }
            }
        }
        Icon {
            anchors.top: tabs.bottom
            anchors.topMargin: 40
            visible: side.tab === "lyrics" && !side.hasLyrics
            name: "lyrics"
            size: 36
            opacity: 0.15
        }

        // 接下来播放
        ListView {
            id: queueView
            anchors.top: tabs.bottom
            anchors.topMargin: 18
            width: parent.width
            height: parent.height - tabs.height - 18
            clip: true
            visible: side.tab === "queue"
            model: player.queueModel
            boundsBehavior: Flickable.StopAtBounds
            spacing: 2
            cacheBuffer: 400
            reuseItems: true
            SmoothScroll { flickable: queueView }
            onVisibleChanged: if (visible) positionViewAtIndex(Math.max(0, player.queuePosition - 1), ListView.Beginning)
            delegate: Item {
                width: queueView.width
                height: 54
                readonly property bool current: model.queueIndex === player.queuePosition
                readonly property bool played: model.queueIndex < player.queuePosition
                readonly property bool hovered: rowMouse.containsMouse || rm.hovered
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusM
                    color: current ? Theme.accentAlpha(0.12) : (hovered ? Theme.hover : "transparent")
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
                        } else player.playQueueIndex(model.queueIndex)
                    }
                }
                RoundedImage {
                    id: qc
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38; height: 38
                    radius: 6
                    source: model.cover
                    decodeSize: 76
                    opacity: played && !current ? 0.5 : 1
                }
                Loader {
                    anchors.centerIn: qc
                    active: current
                    sourceComponent: Rectangle {
                        width: 38; height: 38; radius: 6
                        color: Qt.rgba(0, 0, 0, 0.5)
                        PlayingBars { anchors.centerIn: parent; maxHeight: 13 }
                    }
                }
                Column {
                    anchors.left: qc.right
                    anchors.leftMargin: 14
                    anchors.right: dur.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: model.title
                        elide: Text.ElideRight
                        color: current ? Theme.accent : (played ? Theme.textTertiary : Theme.text)
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        font.weight: current ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        width: parent.width
                        text: model.artist
                        elide: Text.ElideRight
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
                Text {
                    id: dur
                    anchors.right: rm.left
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.duration
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
                IconButton {
                    id: rm
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 30
                    icon: "close"
                    iconSize: 12
                    enabled: !current
                    opacity: hovered && !current ? 1 : 0
                    onClicked: player.removeFromQueue(model.queueIndex)
                }
            }
        }
    }
}
