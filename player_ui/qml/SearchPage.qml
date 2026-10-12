import QtQuick
import PlayerUi

// 搜索页:大号输入框常驻顶部;结果按「最佳匹配 + 歌手 / 专辑带 / 歌曲列表」组织
//  · 拼音 / 首字母 / 简繁 / 假名罗马音都能搜;匹配到的文字以强调色标出
//  · 回车直接打开 / 播放最佳匹配;↓ 进入歌曲列表,回车播放所选
Item {
    id: page
    property int bottomInset: 140
    signal back()
    readonly property int sidePadding: width > 1400 ? 56 : 40
    readonly property string q: searchModel.query.trim()
    readonly property var best: searchModel.top
    readonly property bool hasTop: best && best.kind !== undefined
    readonly property bool empty: q.length > 0 && !hasTop && searchModel.count === 0

    function focusWith(t) {
        input.text = t
        searchModel.query = t
        input.forceActiveFocus()
        input.cursorPosition = input.text.length
    }
    function openTop() {
        if (!hasTop) return
        if (best.kind === "artist") Nav.openArtist(best.index)
        else if (best.kind === "album") Nav.openAlbum(best.index)
        else player.playTracks(searchModel.trackIds(), 0)
    }

    Timer { id: debounce; interval: 90; onTriggered: searchModel.query = input.text }

    // ---------------- 输入框 ----------------
    Item {
        id: field
        x: page.sidePadding
        y: 54
        width: Math.min(860, page.width - page.sidePadding * 2)
        height: 56
        Icon {
            id: glass
            anchors.verticalCenter: parent.verticalCenter
            name: "search"
            size: 24
            tint: input.activeFocus ? Theme.accent : "transparent"
            opacity: input.activeFocus ? 1 : 0.5
        }
        TextInput {
            id: input
            anchors.left: glass.right
            anchors.leftMargin: 16
            anchors.right: clear.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            selectionColor: Theme.accentAlpha(0.4)
            selectedTextColor: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 28
            font.weight: Font.DemiBold
            clip: true
            onTextChanged: debounce.restart()
            Keys.onReturnPressed: page.sel >= 0 ? player.playTracks(searchModel.trackIds(), page.sel) : page.openTop()
            Keys.onEnterPressed: page.sel >= 0 ? player.playTracks(searchModel.trackIds(), page.sel) : page.openTop()
            Keys.onDownPressed: page.moveSel(1)
            Keys.onUpPressed: page.moveSel(-1)
            Keys.onEscapePressed: text.length ? (text = "") : page.back()
        }
        IconButton {
            id: clear
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 36; height: 36
            icon: "close"
            iconSize: 15
            opacity: input.text.length ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
            onClicked: { input.text = ""; input.forceActiveFocus() }
        }
        // 下划线:聚焦时从中间向两侧展开为强调色
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            height: 2
            radius: 1
            width: parent.width
            color: Theme.accent
            scale: input.activeFocus ? 1 : 0
            Behavior on scale { ScaleAnimator { duration: Theme.slow; easing.type: Easing.OutQuint } }
        }
    }

    // ---------------- 结果 ----------------
    // 单个可滚动列:最佳匹配 / 歌手 / 专辑 / 歌曲。键盘焦点始终留在输入框:↑↓ 选择歌曲,回车播放
    property int sel: -1
    onQChanged: { sel = -1; results.contentY = 0; if (q.length) refresh.restart() }
    // 结果刷新:新结果从略下方轻轻浮上并淡入,而不是整页瞬间替换
    ParallelAnimation {
        id: refresh
        OpacityAnimator { target: col; from: 0.35; to: 1; duration: 260; easing.type: Easing.OutCubic }
        NumberAnimation { target: colShift; property: "y"; from: 10; to: 0; duration: 380; easing.type: Easing.OutQuint }
    }
    function moveSel(d) {
        const n = searchModel.count
        if (n === 0) return
        sel = Math.max(-1, Math.min(n - 1, sel + d))
        if (sel < 0) { results.contentY = 0; return }
        const r = trackRepeater.itemAt(sel)
        if (!r) return
        const y = r.mapToItem(col, 0, 0).y
        if (y < results.contentY + 20) results.contentY = Math.max(0, y - 20)
        else if (y + r.height > results.contentY + results.height - page.bottomInset)
            results.contentY = Math.min(results.contentHeight - results.height + page.bottomInset, y + r.height - results.height + page.bottomInset)
    }

    Flickable {
        id: results
        anchors.top: field.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        x: page.sidePadding
        width: page.width - page.sidePadding * 2
        clip: true
        contentHeight: col.height + page.bottomInset
        boundsBehavior: Flickable.StopAtBounds
        opacity: page.q.length ? 1 : 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        SmoothScroll { flickable: results }

            Column {
            id: col
            transform: Translate { id: colShift }
            width: results.width
            topPadding: 24
            spacing: 0

            // 最佳匹配 + 歌手
            Row {
                visible: page.hasTop || searchModel.artists.length > 0
                width: parent.width
                height: visible ? 196 : 0
                spacing: 28

                // 最佳匹配卡片
                Rectangle {
                    id: topCard
                    visible: page.hasTop
                    width: Math.min(420, parent.width * 0.42)
                    height: 196
                    radius: 18
                    color: topMouse.containsMouse ? Theme.pressed : Theme.hover
                    border.width: 1
                    border.color: Theme.hairline
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                    RoundedImage {
                        id: topArt
                        x: 22; y: 22
                        width: 92; height: 92
                        radius: page.best && page.best.kind === "artist" ? 46 : 10
                        source: page.best && page.best.cover ? page.best.cover : ""
                        decodeSize: 208
                    }
                    Column {
                        x: 22
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 20
                        width: parent.width - 44 - 64
                        spacing: 4
                        Text {
                            width: parent.width
                            text: page.best ? Nav.highlight(page.best.title || page.best.name || "", page.q, Theme.accent) : ""
                            textFormat: Text.StyledText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 24
                            font.weight: Font.Bold
                        }
                        Row {
                            spacing: 8
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: !page.best ? "" : page.best.kind === "artist" ? "artist" : page.best.kind === "album" ? "album-open" : "note"
                                size: 14
                                opacity: 0.5
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: !page.best ? "" : (page.best.kind === "artist" ? page.best.summary : (page.best.subtitle || page.best.artist || ""))
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                            }
                        }
                    }
                    MouseArea {
                        id: topMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.openTop()
                    }
                    IconButton {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 18
                        width: 50; height: 50
                        filled: true
                        icon: "play"
                        iconSize: 20
                        opacity: topMouse.containsMouse || hovered ? 1 : 0
                        scale: topMouse.containsMouse || hovered ? 1 : 0.8
                        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                        Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutBack } }
                        onClicked: {
                            const t = page.best
                            if (t.kind === "album") player.playAlbum(t.index, 0)
                            else if (t.kind === "artist") { artistTracks.artistIndex = t.index; player.playTracks(artistTracks.trackIds(), 0) }
                            else player.playTracks(searchModel.trackIds(), 0)
                        }
                    }
                }

                // 歌手
                ListView {
                    id: artistStrip
                    width: parent.width - (topCard.visible ? topCard.width + 28 : 0)
                    height: 196
                    orientation: ListView.Horizontal
                    spacing: 18
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: searchModel.artists
                    SmoothScroll { flickable: artistStrip; horizontal: true }
                    delegate: Item {
                        width: 128
                        height: artistStrip.height
                        readonly property bool hovered: am.containsMouse
                        RoundedImage {
                            y: 22 + (parent.hovered ? -3 : 0)
                            width: 128; height: 128
                            radius: 64
                            source: modelData.cover
                            decodeSize: 256
                        }
                        Text {
                            y: 160
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: Nav.highlight(modelData.name, page.q, Theme.accent)
                            textFormat: Text.StyledText
                            elide: Text.ElideRight
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: am
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Nav.openArtist(modelData.index)
                        }
                    }
                }
            }

            // 专辑带
            Item { width: 1; height: searchModel.albums.length ? 30 : 0 }
            Marker { visible: searchModel.albums.length > 0; icon: "album-open"; count: searchModel.albums.length }
            ListView {
                id: albumStrip
                visible: searchModel.albums.length > 0
                width: parent.width
                height: visible ? 206 : 0
                orientation: ListView.Horizontal
                spacing: 20
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: searchModel.albums
                SmoothScroll { flickable: albumStrip; horizontal: true }
                delegate: Item {
                    width: 150
                    height: albumStrip.height
                    readonly property bool hovered: alm.containsMouse
                    RoundedImage {
                        y: 10 + (parent.hovered ? -3 : 0)
                        width: 150; height: 150
                        radius: 8
                        source: modelData.cover
                        decodeSize: 220
                    }
                    Text {
                        y: 170
                        width: parent.width
                        text: Nav.highlight(modelData.title, page.q, Theme.accent)
                        textFormat: Text.StyledText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        y: 189
                        width: parent.width
                        text: modelData.artist
                        elide: Text.ElideRight
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: alm
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Nav.openAlbum(modelData.index)
                    }
                }
            }

            Item { width: 1; height: searchModel.count ? 26 : 0 }
            Marker { visible: searchModel.count > 0; icon: "note"; count: searchModel.count }
            Item { width: 1; height: 6 }

            Repeater {
                id: trackRepeater
                model: searchModel
                delegate: TrackRow {
                    width: col.width
                    trackId: model.trackId
                    number: index + 1
                    title: model.title
                    artist: model.artist
                    album: model.album
                    quality: model.quality
                    duration: model.duration
                    cover: model.cover
                    showAlbum: true
                    highlight: page.q
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusM
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.accentAlpha(0.55)
                        visible: page.sel === index
                    }
                    onActivated: player.playTracks(searchModel.trackIds(), index)
                }
            }
        }
    }

    // 空查询 / 无结果:只留一个淡淡的图标
    Icon {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -20
        name: "search"
        size: 64
        opacity: page.q.length === 0 ? 0.06 : (page.empty ? 0.14 : 0)
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
    }

    component Marker: Row {
        property string icon
        property int count
        height: visible ? 28 : 0
        spacing: 8
        leftPadding: 2
        Icon { anchors.verticalCenter: parent.verticalCenter; name: parent.icon; size: 15; opacity: 0.5 }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.count
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }
}
