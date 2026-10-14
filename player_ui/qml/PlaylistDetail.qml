import QtQuick
import PlayerUi

// 歌单详情:封面拼图 + 可编辑名称 + 播放 / 随机 / 删除;曲目可右键移除
Item {
    id: page
    property int bottomInset: 140
    property string playlistId
    signal back()
    readonly property int sidePadding: Theme.pagePadding(width)
    readonly property var info: playlists.revision >= 0 && playlistId.length ? playlists.info(playlistId) : ({})
    property bool confirmDelete: false

    onPlaylistIdChanged: {
        playlistTracks.playlistId = playlistId
        confirmDelete = false
        list.userScrolled = false
        Qt.callLater(list.positionViewAtBeginning)
        if (Nav.renameNext === playlistId) {
            Nav.renameNext = ""
            renameSoon.restart()
        }
    }
    // 等页面淡入后再进入编辑,光标不会在过渡中闪烁
    Timer { id: renameSoon; interval: 320; onTriggered: page.startRename() }
    function startRename() {
        if (info.builtin) return
        list.positionViewAtBeginning()
        if (list.headerItem) list.headerItem.beginEdit()
    }

    function play(shuffled) {
        if (playlistTracks.count === 0) return
        player.shuffle = shuffled
        player.playTracks(playlistTracks.trackIds(), shuffled ? Math.floor(Math.random() * playlistTracks.count) : 0)
    }

    ShaderEffect {
        x: page.sidePadding - 160
        y: -120
        width: 640
        height: 560
        property color color: Theme.accent
        property real intensity: 0.14
        fragmentShader: "qrc:/shaders/glow.frag.qsb"
    }

    Timer { id: deleteWindow; interval: 2600; onTriggered: page.confirmDelete = false }

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding
        anchors.rightMargin: page.sidePadding
        model: playlistTracks
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: page.bottomInset
        cacheBuffer: 600
        reuseItems: true
        SmoothScroll { flickable: list; onScrolled: list.userScrolled = true }
        // 头部内容 (专辑带、封面) 异步就绪后高度会变:用户还没滚动时始终停在顶部
        property bool userScrolled: false
        onMovementStarted: userScrolled = true
        onCountChanged: if (!userScrolled) Qt.callLater(positionViewAtBeginning)

        header: Column {
            onHeightChanged: if (!list.userScrolled) Qt.callLater(list.positionViewAtBeginning)
            id: head
            width: list.width
            function beginEdit() { nameEdit.text = page.info.name || ""; nameEdit.visible = true; nameEdit.forceActiveFocus(); nameEdit.selectAll() }

            Item { width: 1; height: 44 }
            IconButton { x: -8; icon: "back"; iconSize: 20; onClicked: page.back() }
            Item { width: 1; height: 18 }
            Row {
                spacing: 40
                Item {
                    id: art
                    width: Math.min(240, Math.max(170, list.width * 0.2))
                    height: width
                    SoftShadow { target: mosaic; spread: 32; offsetY: 20 }
                    Mosaic {
                        id: mosaic
                        anchors.fill: parent
                        covers: page.info.covers || []
                        favorite: !!page.info.builtin
                        decodeSize: 480
                    }
                }
                Column {
                    anchors.bottom: art.bottom
                    spacing: 12
                    width: list.width - art.width - 40

                    Item {
                        width: parent.width
                        height: nameText.implicitHeight
                        Text {
                            id: nameText
                            width: parent.width
                            visible: !nameEdit.visible
                            text: page.info.name || ""
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: list.width > 800 ? 46 : 34
                            font.weight: Font.Bold
                            font.letterSpacing: -0.5
                            MouseArea {
                                anchors.fill: parent
                                enabled: !page.info.builtin
                                cursorShape: Qt.IBeamCursor
                                onDoubleClicked: head.beginEdit()
                            }
                        }
                        TextInput {
                            id: nameEdit
                            visible: false
                            width: parent.width
                            color: Theme.text
                            selectionColor: Theme.accentAlpha(0.4)
                            font: nameText.font
                            maximumLength: 60
                            onAccepted: { playlists.rename(page.playlistId, text); visible = false }
                            onActiveFocusChanged: if (!activeFocus && visible) { playlists.rename(page.playlistId, text); visible = false }
                            Keys.onEscapePressed: visible = false
                            Rectangle { anchors.top: parent.bottom; width: parent.width; height: 2; color: Theme.accent }
                        }
                    }
                    Text {
                        text: page.info.summary || ""
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                    Item { width: 1; height: 6 }
                    Row {
                        spacing: 14
                        IconButton { width: 52; height: 52; filled: true; icon: "play"; iconSize: 20; onClicked: page.play(false) }
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 42; height: 42; outlined: true; icon: "shuffle"; iconSize: 17
                            onClicked: page.play(true)
                        }
                        IconButton {
                            visible: !page.info.builtin
                            anchors.verticalCenter: parent.verticalCenter
                            width: 42; height: 42; outlined: true; icon: "edit"; iconSize: 16
                            onClicked: head.beginEdit()
                        }
                        // 删除:第一次点击变为警示色并轻摇,2.6 秒内再点一次才删除
                        IconButton {
                            id: del
                            visible: !page.info.builtin
                            anchors.verticalCenter: parent.verticalCenter
                            width: 42; height: 42; outlined: !page.confirmDelete; icon: "trash"; iconSize: 16
                            filled: page.confirmDelete
                            fillColor: Theme.warn
                            transform: Translate { id: nudge }
                            onClicked: {
                                if (page.confirmDelete) {
                                    deleteWindow.stop()
                                    const id = page.playlistId
                                    page.back()
                                    playlists.remove(id)
                                } else {
                                    page.confirmDelete = true
                                    deleteWindow.restart()
                                    shake.restart()
                                }
                            }
                            SequentialAnimation {
                                id: shake
                                NumberAnimation { target: nudge; property: "x"; to: -4; duration: 60 }
                                NumberAnimation { target: nudge; property: "x"; to: 4; duration: 80 }
                                NumberAnimation { target: nudge; property: "x"; to: 0; duration: 60 }
                            }
                        }
                    }
                }
            }
            Item { width: 1; height: 30 }
            Rectangle { width: list.width; height: 1; color: Theme.hairline }
            Item { width: 1; height: 10 }
        }

        // 拖动排序:悬停时行左侧出现把手,按住把手上下拖,强调色细线标出落点
        property int dragFrom: -1
        property int dropAt: -1
        delegate: Item {
            id: rowWrap
            width: list.width
            height: trackRow.height
            opacity: list.dragFrom === index ? 0.35 : 1
            Behavior on opacity { OpacityAnimator { duration: Theme.fast } }

            TrackRow {
                id: trackRow
                width: parent.width
                trackId: model.trackId
                number: model.number
                title: model.title
                artist: model.artist
                album: model.album
                quality: model.quality
                duration: model.duration
                cover: model.cover
                showAlbum: true
                menuContext: ({ playlistId: page.playlistId })
                onActivated: player.playTracks(playlistTracks.trackIds(), index)
            }
            Icon {
                x: -26
                anchors.verticalCenter: parent.verticalCenter
                name: "grip"
                size: 16
                opacity: grip.containsMouse || grip.pressed ? 0.9 : (trackRow.hovered ? 0.4 : 0)
                Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
            }
            MouseArea {
                id: grip
                x: -34
                width: 30
                height: parent.height
                hoverEnabled: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                preventStealing: true
                onPressed: (e) => {
                    list.dragFrom = index
                    list.dropAt = index
                    ghost.title = model.title
                    ghost.cover = model.cover
                    move(e)
                }
                onPositionChanged: (e) => { if (pressed) move(e) }
                onReleased: {
                    const from = list.dragFrom, to = list.dropAt
                    list.dragFrom = -1
                    list.dropAt = -1
                    ghost.visible = false
                    if (from >= 0 && to >= 0 && to !== from) playlists.move(page.playlistId, from, to)
                }
                onCanceled: { list.dragFrom = -1; list.dropAt = -1; ghost.visible = false }
                function move(e) {
                    const p = mapToItem(list.contentItem, e.x, e.y)
                    let i = list.indexAt(40, p.y)
                    if (i < 0) i = p.y < list.originY + (list.headerItem ? list.headerItem.height : 0) ? 0 : list.count - 1
                    list.dropAt = i
                    const v = mapToItem(page, e.x, e.y)
                    ghost.y = v.y - ghost.height / 2
                    ghost.visible = true
                    // 拖到上下边缘时自动滚动
                    const lv = mapToItem(list, e.x, e.y)
                    if (lv.y < 60) list.contentY = Math.max(list.originY, list.contentY - 12)
                    else if (lv.y > list.height - page.bottomInset - 20)
                        list.contentY = Math.min(list.contentHeight + list.originY - list.height + list.bottomMargin, list.contentY + 12)
                }
            }
            // 落点指示线
            Rectangle {
                visible: list.dropAt === index && list.dragFrom >= 0 && list.dropAt !== list.dragFrom
                y: list.dropAt > list.dragFrom ? parent.height - 1 : -1
                width: parent.width
                height: 2
                radius: 1
                color: Theme.accent
            }
        }
    }

    // 拖动中跟随指针的行影
    Rectangle {
        id: ghost
        property string title
        property string cover
        visible: false
        x: page.sidePadding + 8
        width: Math.min(420, page.width - page.sidePadding * 2)
        height: 52
        radius: Theme.radiusM
        color: Theme.panel
        border.width: 1
        border.color: Theme.accentAlpha(0.5)
        z: 10
        RoundedImage { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 36; height: 36; radius: 6; source: ghost.cover; decodeSize: 72 }
        Text {
            x: 56
            width: parent.width - 70
            anchors.verticalCenter: parent.verticalCenter
            text: ghost.title
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }
    }

    // 空歌单:一个淡淡的图标,不写字
    Icon {
        visible: playlistTracks.count === 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.62
        name: page.info.builtin ? "heart" : "note"
        size: 40
        opacity: 0.18
    }
}
