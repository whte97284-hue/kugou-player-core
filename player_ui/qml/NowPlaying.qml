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

    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { OpacityAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }

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
        property real intensity: 0.30
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
            y: np.shown ? 0 : 36
            Behavior on y { NumberAnimation { duration: 520; easing.type: Easing.OutQuint } }
        }
        x: np.originX
        y: np.topY
        width: np.coverSize
        height: np.columnH

        SoftShadow { target: cover; spread: 40; offsetY: 24; strength: 0.95 }
        CrossfadeCover {
            id: cover
            width: np.coverSize
            height: np.coverSize
            radius: 14
            source: player.cover
            decodeSize: 1024
        }

        Column {
            anchors.top: cover.bottom
            anchors.topMargin: 26
            width: parent.width
            spacing: 0

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

            Item { width: 1; height: 18 }
            // 一行信号链:点击展开详情
            Item {
                width: parent.width
                height: 20
                Rectangle {
                    id: dot
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6; height: 6; radius: 3
                    color: player.bitPerfect ? Theme.ok : Theme.warn
                }
                Text {
                    anchors.left: dot.right
                    anchors.leftMargin: 9
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: player.chainSummary
                    elide: Text.ElideRight
                    color: chainMouse.containsMouse ? Theme.textSecondary : Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
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

        // 歌词
        ListView {
            id: lyricView
            anchors.top: tabs.bottom
            anchors.topMargin: 18
            width: parent.width
            height: parent.height - tabs.height - 18
            clip: true
            visible: side.tab === "lyrics"
            model: player.lyrics
            currentIndex: Math.max(0, player.lyricIndex)
            spacing: 6
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: height * 0.32
            preferredHighlightEnd: height * 0.32 + 64
            highlightMoveDuration: 520
            highlightMoveVelocity: -1
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                width: lyricView.width
                height: line.implicitHeight + 14
                readonly property int dist: Math.abs(index - player.lyricIndex)
                readonly property bool current: index === player.lyricIndex
                Text {
                    id: line
                    width: parent.width
                    y: 7
                    text: modelData.text
                    wrapMode: Text.WordWrap
                    color: current ? Theme.text : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    lineHeight: 1.2
                    opacity: current ? 1 : Math.max(0.22, 0.62 - dist * 0.09)
                    scale: current ? 1 : 0.94
                    transformOrigin: Item.Left
                    Behavior on opacity { OpacityAnimator { duration: 360 } }
                    Behavior on scale { ScaleAnimator { duration: 360; easing.type: Easing.OutCubic } }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: player.seek(modelData.time)
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
