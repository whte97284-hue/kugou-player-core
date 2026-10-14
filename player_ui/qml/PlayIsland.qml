import QtQuick
import PlayerUi

// 底部播放条 (通栏):顶边是可拖动的进度线
//  左:旋转迷你黑胶 · 歌名 / 歌手与时间 · 收藏
//  中 (相对整个窗口居中):播放模式 · 上一首 · 播放 · 下一首 · 播放列表
//  右:小频谱 · 信号链胶囊 · 常驻音量
Item {
    id: island
    signal chainClicked()
    signal expandRequested()
    signal queueClicked()
    property bool queueOpen: false
    readonly property Item disc: discVinyl
    height: 76
    readonly property bool compact: width < 900

    // ---- 面板 ----
    // 与背景联动:条内铺一份和窗口背景完全对齐的模糊封面 (同一张图、同一位置),再压一层深色,
    // 看起来像磨砂玻璃透出了后面的封面;换歌时跟背景一起交叉淡化。页面内容不会透上来
    property Item window: null
    Rectangle { anchors.fill: parent; color: Theme.base }
    Item {
        anchors.fill: parent
        clip: true
        Backdrop {
            id: barBackdrop
            x: 0
            y: -island.y
            width: island.width
            height: island.window ? island.window.height : island.height
            source: player.cover
            solid: false
            opacity: player.hasTrack ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 900; easing.type: Easing.InOutQuad } }
        }
    }
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.05, 0.045, 0.04, player.hasTrack ? 0.62 : 0.97)
        Behavior on color { ColorAnimation { duration: 900 } }
    }
    // 上沿一道强调色柔光,随低音轻轻呼吸
    Rectangle {
        width: parent.width
        height: 26
        opacity: 0.55 + 0.45 * player.breath
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.accentAlpha(0.16) }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    // 上沿发丝线 (进度线未走到的部分)
    Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 0.96, 0.9, 0.07) }

    // ---- 左:迷你黑胶 + 歌名 ----
    Item {
        id: discBox
        x: 18
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        width: 52
        height: 52
        Vinyl {
            id: discVinyl
            anchors.fill: parent
            cover: player.cover
            spinning: player.playing
            labelSize: 128
            // 第一次开始播放时,唱片从空位上淡入并轻轻放大
            opacity: player.hasTrack ? 1 : 0
            scale: player.hasTrack ? 1 : 0.8
            visible: opacity > 0
            Behavior on opacity { OpacityAnimator { duration: Theme.slow; easing.type: Easing.OutCubic } }
            Behavior on scale { ScaleAnimator { duration: Theme.slow; easing.type: Easing.OutBack } }
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            opacity: player.hasTrack ? 0 : 1
            visible: opacity > 0
            Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
            color: Theme.hover
            Icon { anchors.centerIn: parent; name: "disc"; size: 22; opacity: 0.45 }
        }
        MouseArea {
            id: discMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (player.hasTrack) island.expandRequested()
        }
        // 悬停唱片:轻轻放大,提示可以点开播放页
        scale: discMouse.containsMouse && player.hasTrack ? 1.08 : 1
        Behavior on scale { ScaleAnimator { duration: 380; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
    }
    // 切歌:旧歌名向左淡出,新歌名从右侧滑入
    property string shownTitle
    property string shownArtist
    Component.onCompleted: { shownTitle = player.title; shownArtist = player.artist }
    Connections {
        target: player
        function onTrackChanged() { swap.restart() }
    }
    SequentialAnimation {
        id: swap
        ParallelAnimation {
            OpacityAnimator { target: titleCol; to: 0; duration: 130; easing.type: Easing.InCubic }
            NumberAnimation { target: titleShift; property: "x"; to: -10; duration: 130; easing.type: Easing.InCubic }
        }
        ScriptAction { script: { island.shownTitle = player.title; island.shownArtist = player.artist; titleShift.x = 14 } }
        ParallelAnimation {
            OpacityAnimator { target: titleCol; to: 1; duration: 320; easing.type: Easing.OutCubic }
            NumberAnimation { target: titleShift; property: "x"; to: 0; duration: 420; easing.type: Easing.OutQuint }
        }
    }
    readonly property real leftEnd: controls.x - 28
    Column {
        id: titleCol
        transform: Translate { id: titleShift }
        anchors.left: discBox.right
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, island.leftEnd - x - favBtn.width - 6)
        spacing: 3
        Text {
            width: Math.min(implicitWidth, island.leftEnd - titleCol.x - favBtn.width - 6)
            text: player.hasTrack ? island.shownTitle : qsTr("未在播放")
            elide: Text.ElideRight
            color: player.hasTrack ? Theme.text : Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        // 歌手,后面跟着播放时间 (时间常驻,歌手名过长时先省略歌手)
        Row {
            spacing: 8
            visible: player.hasTrack
            Text {
                width: Math.min(implicitWidth, island.leftEnd - titleCol.x - favBtn.width - 6 - timeText.implicitWidth - 8)
                text: island.shownArtist
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            Text {
                id: timeText
                text: edge.fmt(edge.dragging ? edge.dragValue : player.position) + " / " + edge.fmt(player.duration)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }
    }
    MouseArea {
        anchors.left: discBox.right
        anchors.right: titleCol.right
        height: parent.height
        cursorShape: player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (player.hasTrack) island.expandRequested()
    }
    IconButton {
        id: favBtn
        readonly property bool fav: playlists.revision >= 0 && player.hasTrack && playlists.isFavorite(player.trackId)
        visible: player.hasTrack
        anchors.left: titleCol.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 34; height: 34
        icon: fav ? "heart-filled" : "heart"
        iconTint: fav ? Theme.accent : "transparent"
        restOpacity: fav ? 1 : 0.6
        popOnChange: true
        iconSize: 16
        onClicked: playlists.toggleFavorite(player.trackId)
    }

    // ---- 中:控制 (相对整条居中) ----
    Row {
        id: controls
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: island.compact ? 60 : 0
        spacing: 14
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 36; height: 36
            icon: player.repeatMode === 2 ? "repeat-one" : (player.shuffle ? "shuffle" : "repeat")
            iconSize: 16
            tip: player.shuffle ? qsTr("随机播放") : player.repeatMode === 2 ? qsTr("单曲循环") : player.repeatMode === 1 ? qsTr("列表循环") : qsTr("顺序播放")
            iconTint: player.shuffle || player.repeatMode > 0 ? Theme.accent : "transparent"
            restOpacity: player.shuffle || player.repeatMode > 0 ? 1 : 0.55
            popOnChange: true
            // 一个按钮轮换四种模式:顺序 → 列表循环 → 单曲循环 → 随机 → 顺序
            onClicked: {
                if (player.shuffle) { player.shuffle = false; player.repeatMode = 0 }
                else if (player.repeatMode === 0) player.repeatMode = 1
                else if (player.repeatMode === 1) player.repeatMode = 2
                else { player.repeatMode = 1; player.shuffle = true }
            }
        }
        IconButton { icon: "prev"; nudge: -1; iconSize: 18; width: 40; height: 40; anchors.verticalCenter: parent.verticalCenter; onClicked: player.previous() }
        Item {
            width: 50
            height: 50
            // 播放键外的强调色光环,随低音轻微脉动
            Rectangle {
                anchors.centerIn: parent
                width: 50; height: 50; radius: 25
                color: Theme.accentAlpha(0.32)
                scale: 1.0 + 0.18 * player.breath
                opacity: Math.min(1, player.breath * 2.2)
            }
            IconButton {
                anchors.centerIn: parent
                icon: player.playing ? "pause" : "play"
                morphOnChange: true
                iconSize: 19
                width: 48; height: 48
                filled: true
                onClicked: player.toggle()
            }
        }
        IconButton { icon: "next"; nudge: 1; iconSize: 18; width: 40; height: 40; anchors.verticalCenter: parent.verticalCenter; onClicked: player.next() }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            width: 36; height: 36
            icon: "queue"
            iconSize: 17
            tip: qsTr("播放列表")
            iconTint: island.queueOpen ? Theme.accent : "transparent"
            restOpacity: island.queueOpen ? 1 : 0.7
            onClicked: island.queueClicked()
        }
    }

    // ---- 右:频谱 · 信号链 · 音量 ----
    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        spacing: 16

        // 小频谱:随音乐跳动,暂停后自然回落
        Spectrum {
            anchors.verticalCenter: parent.verticalCenter
            visible: player.hasTrack && !island.compact
            width: 46
            height: 22
            bars: 10
            gap: 2.6
            opacity: 0.9
        }
        Rectangle {
            visible: player.hasTrack
            anchors.verticalCenter: parent.verticalCenter
            height: 26
            width: chipRow.implicitWidth + 20
            radius: 13
            color: chipMouse.containsMouse ? Theme.pressed : Qt.rgba(1, 1, 1, 0.05)
            border.width: 1
            border.color: player.bitPerfect ? Theme.okAlpha(0.45) : Theme.warnAlpha(0.32)
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 7
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6; height: 6; radius: 3
                    color: player.bitPerfect ? Theme.ok : Theme.warn
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: player.quality
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
            MouseArea {
                id: chipMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: island.chainClicked()
            }
        }
        VolumeControl { anchors.verticalCenter: parent.verticalCenter; pinned: !island.compact }
    }

    // ---- 顶边进度线 ----
    EdgeProgress {
        id: edge
        width: parent.width
        y: -height + 4
        position: player.position
        duration: player.duration
        onSeekRequested: (s) => player.seek(s)
    }
}
