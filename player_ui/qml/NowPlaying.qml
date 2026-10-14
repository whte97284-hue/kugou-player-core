import QtQuick
import PlayerUi

// 沉浸播放页 v4:整窗铺满模糊封面,左边封面与控制,右边歌词占满整个高度,没有用不到的空白
//  左栏:封面 → 歌名歌手 (居中) → 进度 (时间在两侧) → 控制 → 音量
//  右栏:歌词居中排版,当前句停在视口偏上处、用强调色随演唱点亮,译文跟在原句下面;无歌词时为「接下来播放」
//  顶栏:左收起、中间音质小胶囊、右边歌词 / 队列切换
Item {
    id: np
    property bool shown: false
    signal closeRequested()
    signal chainClicked()
    // 共享元素飞行用:大封面。封面本身不参与进场位移,飞行落点在整个过程中保持不动
    readonly property Item coverItem: cover

    // 进场错峰:封面下方各行依次上浮淡入 (order 越大越晚),退场同时下沉
    component Rise: Item {
        property int order: 0
        width: parent ? parent.width : 0
        height: childrenRect.height
        opacity: np.shown ? 1 : 0
        Behavior on opacity { SequentialAnimation { PauseAnimation { duration: np.shown ? 140 + order * 50 : 0 } OpacityAnimator { duration: np.shown ? 420 : 160; easing.type: Easing.OutCubic } } }
        transform: Translate {
            y: np.shown ? 0 : 18 + order * 6
            Behavior on y { SequentialAnimation { PauseAnimation { duration: np.shown ? 140 + order * 50 : 0 } NumberAnimation { duration: np.shown ? 680 : 200; easing.type: Easing.OutQuint } } }
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
    // 背景直接沿用首页那层模糊封面 (同一张图、同一参数,就在播放页下面),播放页自己不再画一份:
    // 打开 / 收起时背景纹丝不动,只有内容淡入淡出;也不会因为自己的图层还没渲染好而闪黑
    Rectangle { anchors.fill: parent; color: Theme.base; visible: !player.hasTrack }
    MouseArea { anchors.fill: parent; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    // ---------------- 版面 ----------------
    readonly property real padX: Math.max(40, width * 0.045)
    readonly property real leftW: Math.round(width * 0.46)
    // 封面下方固定内容的总高度 (歌名、歌手、进度、控制、音量与间距)
    readonly property real belowCover: 224
    readonly property real coverSize: Math.round(Math.max(200, Math.min(leftW - padX - 24, height - belowCover - 120, 520)))
    readonly property real colX: Math.round(padX + (leftW - padX - coverSize) / 2)
    readonly property real colY: Math.round((height - coverSize - belowCover) / 2 + 8)
    readonly property real sideX: leftW + Math.max(16, width * 0.02)
    readonly property real sideW: width - padX - sideX

    // ---------------- 顶栏 ----------------
    IconButton {
        x: 20
        y: 16
        icon: "back"
        iconSize: 18
        rotation: -90
        onClicked: np.closeRequested()
    }

    // 音质小胶囊:平时只有一个点与「24/96」这类简写;悬停时平滑拉宽、换成完整信号链,点击展开详情
    Rectangle {
        id: qcap
        readonly property bool open: chainMouse.containsMouse
        readonly property real shortW: shortText.implicitWidth + 40
        readonly property real longW: Math.min(np.width * 0.5, longText.implicitWidth + 40)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 22
        height: 28
        width: open ? longW : shortW
        radius: 14
        color: Qt.rgba(1, 0.96, 0.9, open ? 0.09 : 0.05)
        border.width: 1
        border.color: Theme.hairline
        clip: true
        visible: player.hasTrack
        opacity: np.shown ? 1 : 0
        Behavior on opacity { SequentialAnimation { PauseAnimation { duration: np.shown ? 260 : 0 } OpacityAnimator { duration: 360 } } }
        Behavior on width { NumberAnimation { duration: 420; easing.type: Easing.OutQuint } }
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        Rectangle {
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

    // 歌词 / 队列切换:放在右下角,避开顶部的窗口按钮
    Row {
        id: tabs
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        spacing: 4
        z: 2
        opacity: np.shown ? 1 : 0
        Behavior on opacity { SequentialAnimation { PauseAnimation { duration: np.shown ? 260 : 0 } OpacityAnimator { duration: 360 } } }
        Repeater {
            model: [{ key: "lyrics", icon: "lyrics" }, { key: "queue", icon: "queue" }]
            Item {
                width: 40
                height: 38
                readonly property bool active: side.tab === modelData.key
                Icon {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -3
                    name: modelData.icon
                    size: 19
                    tint: active ? Theme.accent : "transparent"
                    opacity: active || tm.containsMouse ? 1 : 0.5
                    Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16
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

    // ---------------- 左栏:封面与控制 ----------------
    Item {
        id: card
        x: np.colX
        y: np.colY
        width: np.coverSize
        height: np.coverSize + np.belowCover

        // 封面舞台:暂停时整张封面连同投影微微后退 (0.9),继续播放时带一点回弹地浮回来
        Item {
            id: coverStage
            width: np.coverSize
            height: np.coverSize
            scale: player.playing || !player.hasTrack ? 1 : 0.9
            Behavior on scale { ScaleAnimator { duration: player.playing ? 680 : 520; easing.type: player.playing ? Easing.OutBack : Easing.OutQuint; easing.overshoot: 1.2 } }
            SoftShadow { target: cover; spread: 44; offsetY: player.playing ? 24 : 12; strength: player.playing ? 0.9 : 0.6
                Behavior on offsetY { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                Behavior on strength { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } } }
            CrossfadeCover {
                id: cover
                width: np.coverSize
                height: np.coverSize
                radius: 12
                source: player.cover
            }
        }

        Column {
            y: np.coverSize + 26
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
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 84
                    horizontalAlignment: Text.AlignHCenter
                    text: player.title
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                IconButton {
                    readonly property bool fav: playlists.revision >= 0 && player.hasTrack && playlists.isFavorite(player.trackId)
                    anchors.right: parent.right
                    anchors.rightMargin: -8
                    anchors.verticalCenter: titleText.verticalCenter
                    width: 36; height: 36
                    icon: fav ? "heart-filled" : "heart"
                    iconTint: fav ? Theme.accent : "transparent"
                    popOnChange: true
                    iconSize: 18
                    onClicked: playlists.toggleFavorite(player.trackId)
                }
            }
            Text {
                topPadding: 3
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: player.artist + (player.album.length ? "  ·  " + player.album : "")
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 14
            }
            }}

            Rise { order: 1; Column { width: parent.width
            Item { width: 1; height: 16 }
            // 进度:时间分列两侧,中间是进度条
            Item {
                width: parent.width
                height: 24
                Text {
                    id: posText
                    width: 40
                    anchors.verticalCenter: parent.verticalCenter
                    text: seek.fmt(seek.dragging ? seek.dragValue : player.position)
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
                SeekBar {
                    id: seek
                    anchors.left: posText.right
                    anchors.right: remText.left
                    anchors.verticalCenter: parent.verticalCenter
                    position: player.position
                    duration: player.duration
                    onSeekRequested: (s) => player.seek(s)
                }
                Text {
                    id: remText
                    width: 40
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: "-" + seek.fmt(Math.max(0, player.duration - (seek.dragging ? seek.dragValue : player.position)))
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
            }
            }}

            Rise { order: 2; Column { width: parent.width
            Item { width: 1; height: 10 }
            Item {
                width: parent.width
                height: 62
                IconButton {
                    anchors.left: parent.left
                    anchors.leftMargin: -6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38; height: 38
                    icon: "shuffle"
                    iconSize: 16
                    iconTint: player.shuffle ? Theme.accent : "transparent"
                    restOpacity: player.shuffle ? 1 : 0.5
                    onClicked: player.shuffle = !player.shuffle
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: -6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38; height: 38
                    icon: player.repeatMode === 2 ? "repeat-one" : "repeat"
                    iconSize: 16
                    iconTint: player.repeatMode > 0 ? Theme.accent : "transparent"
                    restOpacity: player.repeatMode > 0 ? 1 : 0.5
                    onClicked: player.repeatMode = (player.repeatMode + 1) % 3
                }
                Row {
                    anchors.centerIn: parent
                    spacing: Math.max(14, Math.min(30, np.coverSize * 0.06))
                    IconButton { icon: "prev"; nudge: -1; iconSize: 21; width: 46; height: 46; anchors.verticalCenter: parent.verticalCenter; onClicked: player.previous() }
                    IconButton {
                        icon: player.playing ? "pause" : "play"
                        morphOnChange: true
                        iconSize: 24
                        width: 60; height: 60
                        filled: true
                        onClicked: player.toggle()
                    }
                    IconButton { icon: "next"; nudge: 1; iconSize: 21; width: 46; height: 46; anchors.verticalCenter: parent.verticalCenter; onClicked: player.next() }
                }
            }
            }}

            Rise { order: 3; Column { width: parent.width
            Item { width: 1; height: 12 }
            Row {
                width: parent.width
                spacing: 12
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "mute"; size: 14; opacity: 0.45 }
                SeekBar {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 14 * 2 - 24
                    showTip: false
                    position: player.volume
                    duration: 1
                    onSeekRequested: (v) => player.volume = v
                    onDragValueChanged: if (dragging) player.volume = dragValue
                }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "volume"; size: 14; opacity: 0.45 }
            }
            }}
        }
    }

    // ---------------- 右栏:歌词 / 接下来播放 ----------------
    Item {
        id: side
        opacity: np.shown ? 1 : 0
        Behavior on opacity { SequentialAnimation { PauseAnimation { duration: np.shown ? 120 : 0 } OpacityAnimator { duration: 420; easing.type: Easing.OutCubic } } }
        transform: Translate {
            y: np.shown ? 0 : 40
            Behavior on y { SequentialAnimation { PauseAnimation { duration: np.shown ? 120 : 0 } NumberAnimation { duration: 640; easing.type: Easing.OutQuint } } }
        }
        x: np.sideX
        y: 0
        width: np.sideW
        height: np.height

        readonly property bool hasLyrics: player.lyrics.length > 0
        property string tab: hasLyrics ? "lyrics" : "queue"
        onHasLyricsChanged: tab = hasLyrics ? "lyrics" : "queue"

        // 歌词:占满整个高度,上下边缘渐隐。当前句停在视口约 42% 处;换句时整列沿一条缓入缓出曲线平滑滑动,
        // 正在滑动时再换句会从当前位置接着滑,不会跳;手动滚动后暂停跟随,片刻后平滑回到当前句
        ListView {
            id: lyricView
            anchors.fill: parent
            visible: side.tab === "lyrics"
            model: player.lyrics
            spacing: 4
            readonly property real anchorRatio: 0.42
            // 首句停在顶部渐隐带下方、末句停在底部渐隐带上方:开头和结尾不再把歌词推出视野
            topMargin: height * 0.2
            bottomMargin: height * 0.28
            // 一首歌的歌词全部实例化:每行位置都是实测的,不会因估算行高在滚动中途跳动
            cacheBuffer: 100000
            boundsBehavior: Flickable.StopAtBounds
            readonly property int fontSize: Math.round(Math.max(20, Math.min(28, np.width / 44)))
            property bool following: true

            layer.enabled: true
            layer.effect: ShaderEffect {
                property real fadeTop: 0.16
                property real fadeBottom: 0.22
                fragmentShader: "qrc:/shaders/fade.frag.qsb"
            }

            // 让第 idx 句的中心停在视口锚点处的 contentY
            function targetFor(idx) {
                const it = itemAtIndex(idx)
                return it ? it.y + it.height / 2 - height * anchorRatio : NaN
            }
            function follow(animate) {
                if (!player.lyricsSynced || !following || !visible || count === 0) return
                const idx = player.lyricIndex
                let t = idx < 0 ? -topMargin : targetFor(idx)
                if (isNaN(t)) { positionViewAtIndex(idx, ListView.Center); t = targetFor(idx) }
                if (isNaN(t)) return
                // 夹在可滚动范围内:前几句时列表停在顶端,当前句自己往下走;末尾同理
                t = Math.max(originY - topMargin, Math.min(t, originY + contentHeight + bottomMargin - height))
                settle.stop()
                if (!animate || Theme.reduced || Math.abs(contentY - t) < 0.5) { contentY = t; return }
                // 跨越很远 (拖动进度条) 时稍慢一点,相邻换句约 0.62 秒
                settle.duration = Math.abs(contentY - t) > height * 0.6 ? 820 : 620
                settle.to = t
                settle.start()
            }
            function pauseFollow() {
                following = false
                settle.stop()
                resume.restart()
            }
            Timer {
                id: resume
                interval: 2600
                onTriggered: { lyricView.following = true; lyricView.follow(true) }
            }
            NumberAnimation {
                id: settle
                target: lyricView
                property: "contentY"
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.3, 0.0, 0.15, 1.0, 1.0, 1.0]
            }
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
                height: line.implicitHeight + (sub.visible ? sub.implicitHeight + 2 : 0) + 18
                readonly property int dist: Math.abs(index - player.lyricIndex)
                readonly property bool current: player.lyricsSynced && index === player.lyricIndex
                readonly property bool sung: player.lyricsSynced && index < player.lyricIndex
                readonly property string translation: modelData.translation || ""
                opacity: !player.lyricsSynced ? 0.75 : row.current ? 1 : Math.max(0.14, (row.sung ? 0.42 : 0.5) - (row.dist - 1) * 0.07)
                Behavior on opacity { OpacityAnimator { duration: 480; easing.type: Easing.OutCubic } }

                Item {
                    id: body
                    width: parent.width
                    height: parent.height
                    // 当前句放大到 1,其余略小;以中心为原点,居中排版下左右对称地缩放
                    scale: row.current || !player.lyricsSynced ? 1 : 0.9
                    Behavior on scale { ScaleAnimator { duration: 620; easing.type: Easing.OutQuint } }

                    Text {
                        id: line
                        // 单行时宽度贴合文字,逐字点亮的进度才能对准文字本身;换行时占满整栏
                        width: Math.min(row.width - 24, implicitWidth)
                        x: (row.width - width) / 2
                        y: 9
                        text: modelData.text
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        color: row.current ? Qt.lighter(Theme.accent, 1.12) : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: player.lyricsSynced ? lyricView.fontSize : 18
                        font.weight: player.lyricsSynced ? Font.Bold : Font.Medium
                        lineHeight: 1.18
                        Behavior on color { ColorAnimation { duration: 360 } }

                        // 当前句:未唱部分压暗,随演唱进度从左到右点亮 (交界处约 28px 柔和过渡)
                        layer.enabled: row.current && !Theme.reduced
                        layer.effect: ShaderEffect {
                            readonly property real start: modelData.time
                            readonly property real span: modelData.duration > 0 ? modelData.duration : 3
                            property real progress: row.current ? Math.max(0, Math.min(1, (player.position + 0.12 - start) / span)) : 0
                            property real lines: Math.max(1, line.lineCount)
                            property real soft: 14 / Math.max(1, line.width)
                            property real dim: 0.5
                            fragmentShader: "qrc:/shaders/wipe.frag.qsb"
                        }
                    }
                    Text {
                        id: sub
                        visible: row.translation.length > 0
                        anchors.top: line.bottom
                        anchors.topMargin: 2
                        x: 12
                        width: row.width - 24
                        text: row.translation
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Math.round(lyricView.fontSize * 0.6)
                        font.weight: Font.Medium
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
            anchors.centerIn: parent
            visible: side.tab === "lyrics" && !side.hasLyrics
            name: "lyrics"
            size: 36
            opacity: 0.15
        }

        // 接下来播放
        ListView {
            id: queueView
            y: 72
            width: parent.width
            height: parent.height - y - 32
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
