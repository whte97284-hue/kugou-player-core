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
    Row {
        x: 24
        y: 22
        spacing: 2
        IconButton {
            icon: "back"
            iconSize: 18
            rotation: -90
            onClicked: np.closeRequested()
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("收起")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }

    // ---------------- 左栏:播放卡 ----------------
    Item {
        id: card
        x: np.originX
        y: np.topY
        width: np.coverSize
        height: np.columnH

        SoftShadow { target: cover; spread: 40; offsetY: 24; strength: 0.95 }
        RoundedImage {
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

            Text {
                width: parent.width
                text: player.title
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.weight: Font.Bold
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
                Row {
                    anchors.centerIn: parent
                    spacing: 34
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
        x: card.x + card.width + np.gap
        y: np.topY
        width: np.originX + np.contentW - x
        height: np.columnH

        readonly property bool hasLyrics: player.lyrics.length > 0
        property string tab: hasLyrics ? "lyrics" : "queue"
        onHasLyricsChanged: tab = hasLyrics ? "lyrics" : "queue"

        Row {
            id: tabs
            spacing: 28
            Repeater {
                model: [{ key: "lyrics", label: qsTr("歌词") }, { key: "queue", label: qsTr("接下来播放") }]
                Item {
                    width: tabText.implicitWidth
                    height: 34
                    readonly property bool active: side.tab === modelData.key
                    Text {
                        id: tabText
                        text: modelData.label
                        color: active ? Theme.text : Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.weight: active ? Font.DemiBold : Font.Normal
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        radius: 1
                        color: Theme.accent
                        opacity: active ? 1 : 0
                        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                    }
                    MouseArea {
                        anchors.fill: parent
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
        Text {
            anchors.top: tabs.bottom
            anchors.topMargin: 24
            visible: side.tab === "lyrics" && !side.hasLyrics
            text: qsTr("没有找到歌词 · 将同名 .lrc 文件放在歌曲旁即可显示")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 14
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
            model: player.queueTracks
            boundsBehavior: Flickable.StopAtBounds
            spacing: 2
            delegate: Item {
                width: queueView.width
                height: 50
                readonly property bool current: modelData.index === player.queuePosition
                readonly property bool played: modelData.index < player.queuePosition
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusM
                    color: current ? Theme.accentAlpha(0.12) : (rowMouse.containsMouse ? Theme.hover : "transparent")
                }
                Text {
                    x: 14
                    width: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.number
                    color: current ? Theme.accent : Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
                Text {
                    x: 50
                    width: parent.width - 50 - 70
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.title
                    elide: Text.ElideRight
                    color: current ? Theme.accent : (played ? Theme.textTertiary : Theme.text)
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    font.weight: current ? Font.DemiBold : Font.Normal
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.duration
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: player.playQueueIndex(modelData.index)
                }
            }
        }
    }
}
