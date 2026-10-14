import QtQuick
import PlayerUi

// 曲目右键菜单:播放 / 下一首播放 / 加入队列 / 加入歌单 (二级) / 收藏 / 查看专辑 / 查看歌手 /
// 打开文件位置 / 复制歌名 / 复制文件路径 / 歌曲信息 / 从歌单移除
// trackId < 0 且 ctx.albumIndex 有值时是「整张专辑」菜单 (专辑详情的「更多」、专辑墙卡片右键)
Item {
    id: menu
    anchors.fill: parent
    visible: panel.opacity > 0
    property real trackId: -1
    property var ctx: ({})
    property bool shown: false
    property bool subShown: false
    // 点击位置;面板位置随菜单实际高度绑定,菜单项变多时也不会超出窗口底部
    property real reqX: 0
    property real reqY: 0
    signal infoRequested(real trackId)

    readonly property bool fav: playlists.revision >= 0 && trackId >= 0 && playlists.isFavorite(trackId)
    readonly property bool albumMode: trackId < 0 && ctx && ctx.albumIndex !== undefined && ctx.albumIndex >= 0
    // 菜单作用的曲目:单曲,或整张专辑
    function targetIds() { return albumMode ? library.albumTrackIds(ctx.albumIndex) : [trackId] }

    function open(id, px, py, c) {
        trackId = id
        ctx = c || {}
        subShown = false
        rebuild()
        reqX = px
        reqY = py
        shown = true
    }
    function close() { shown = false; subShown = false }

    property var items: []
    function rebuild() {
        if (albumMode) {
            const ids = library.albumTrackIds(ctx.albumIndex)
            const allFav = playlists.revision >= 0 && ids.length > 0 && ids.every(i => playlists.isFavorite(i))
            const info = library.albumInfo(ctx.albumIndex)
            items = [
                { key: "albumPlay", icon: "play", text: qsTr("播放") },
                { key: "albumShuffle", icon: "shuffle", text: qsTr("随机播放") },
                { key: "next", icon: "next-up", text: qsTr("下一首播放") },
                { key: "queue", icon: "queue", text: qsTr("添加到播放队列") },
                { key: "playlist", icon: "playlist", text: qsTr("添加到歌单"), sub: true },
                { key: "favAll", icon: allFav ? "heart-filled" : "heart", text: allFav ? qsTr("全部取消收藏") : qsTr("全部收藏"), on: allFav },
                { key: "sep" },
                { key: "albumOpen", icon: "album-open", text: qsTr("查看专辑"), hide: ctx.fromDetail === true },
                { key: "albumArtist", icon: "artist", text: qsTr("查看歌手"), hide: library.artistIndexOf(info.artist || "") < 0 },
                { key: "sep" },
                { key: "reveal", icon: "folder-open", text: qsTr("打开所在文件夹") },
                { key: "copyAlbum", icon: "note", text: qsTr("复制专辑名") }
            ].filter(e => !e.hide)
            return
        }
        const sameAlbum = ctx && ctx.albumIndex !== undefined && ctx.albumIndex === library.albumOfTrack(trackId)
        const list = [
            { key: "play", icon: "play", text: qsTr("播放") },
            { key: "next", icon: "next-up", text: qsTr("下一首播放") },
            { key: "queue", icon: "queue", text: qsTr("添加到播放队列") },
            { key: "playlist", icon: "playlist", text: qsTr("添加到歌单"), sub: true },
            { key: "fav", icon: menu.fav ? "heart-filled" : "heart", text: menu.fav ? qsTr("取消收藏") : qsTr("收藏") },
            { key: "sep" },
            { key: "album", icon: "album-open", text: qsTr("查看专辑"), hide: sameAlbum },
            { key: "artist", icon: "artist", text: qsTr("查看歌手") },
            { key: "sep" },
            { key: "reveal", icon: "folder-open", text: qsTr("打开文件位置") },
            { key: "copyName", icon: "note", text: qsTr("复制歌名") },
            { key: "copyPath", icon: "external", text: qsTr("复制文件路径") },
            { key: "info", icon: "more", text: qsTr("歌曲信息") }
        ]
        if (ctx && ctx.playlistId)
            list.push({ key: "sep" }, { key: "remove", icon: "trash", text: qsTr("从歌单移除") })
        items = list.filter(e => !e.hide)
    }
    function act(key) {
        const id = trackId
        if (albumMode && actAlbum(key)) { close(); return }
        if (key === "play") player.playTrack(id)
        else if (key === "next") { player.playNext(id); Nav.toast("next-up", qsTr("将在下一首播放")) }
        else if (key === "queue") { player.enqueue([id]); Nav.toast("queue", qsTr("已添加到播放队列")) }
        else if (key === "fav") playlists.toggleFavorite(id)
        else if (key === "album") Nav.openAlbum(library.albumOfTrack(id))
        else if (key === "artist") Nav.openArtist(library.artistOfTrack(id))
        else if (key === "remove") playlists.removeTrack(ctx.playlistId, id)
        else if (key === "reveal") {
            if (!library.revealTrack(id)) Nav.toast("folder", qsTr("找不到这首歌所在的文件夹"))
        } else if (key === "copyName" || key === "copyPath") {
            const t = library.trackInfo(id)
            if (t.path !== undefined) {
                library.copyToClipboard(key === "copyPath" ? t.path : (t.artist ? t.artist + " - " + t.title : t.title))
                Nav.toast("check", key === "copyPath" ? qsTr("已复制文件路径") : qsTr("已复制歌名"))
            }
        } else if (key === "info") menu.infoRequested(id)
        close()
    }
    // 整张专辑的操作;返回 false 表示不是专辑专属的键
    function actAlbum(key) {
        const a = ctx.albumIndex
        const ids = library.albumTrackIds(a)
        if (!ids.length) return true
        if (key === "albumPlay") { player.shuffle = false; player.playAlbum(a, 0) }
        else if (key === "albumShuffle") { player.shuffle = true; player.playAlbum(a, Math.floor(Math.random() * ids.length)) }
        else if (key === "next") { player.playNextTracks(ids); Nav.toast("next-up", qsTr("%1 首将在当前歌曲之后播放").arg(ids.length)) }
        else if (key === "queue") { player.enqueue(ids); Nav.toast("queue", qsTr("已添加 %1 首到播放队列").arg(ids.length)) }
        else if (key === "favAll") {
            const allFav = ids.every(i => playlists.isFavorite(i))
            let n = 0
            for (const i of ids) if (playlists.isFavorite(i) === allFav) { playlists.toggleFavorite(i); ++n }
            Nav.toast(allFav ? "heart" : "heart-filled", allFav ? qsTr("已取消收藏 %1 首").arg(n) : qsTr("已收藏 %1 首").arg(n))
        }
        else if (key === "albumOpen") Nav.openAlbum(a)
        else if (key === "albumArtist") Nav.openArtist(library.artistIndexOf(library.albumInfo(a).artist || ""))
        else if (key === "reveal") { if (!library.revealTrack(ids[0])) Nav.toast("folder", qsTr("找不到这张专辑所在的文件夹")) }
        else if (key === "copyAlbum") {
            const info = library.albumInfo(a)
            library.copyToClipboard(info.artist ? info.artist + " - " + info.title : info.title)
            Nav.toast("check", qsTr("已复制专辑名"))
        }
        else return false
        return true
    }
    function addTo(pid, name) {
        const ids = targetIds()
        const n = playlists.addTracks(pid, ids)
        if (albumMode)
            Nav.toast(n > 0 ? "check" : "playlist", n > 0 ? qsTr("已添加 %1 首到「%2」").arg(n).arg(name) : qsTr("「%1」里已经有这些歌").arg(name))
        else
            Nav.toast(n > 0 ? "check" : "playlist", n > 0 ? qsTr("已添加到「%1」").arg(name) : qsTr("「%1」里已经有这首歌").arg(name))
        close()
    }

    MouseArea {
        anchors.fill: parent
        enabled: menu.shown
        acceptedButtons: Qt.AllButtons
        onPressed: menu.close()
        onWheel: (w) => menu.close()
    }

    // ---- 一级菜单 ----
    Rectangle {
        id: panel
        width: 212
        x: Math.max(8, Math.min(menu.reqX, menu.width - width - 8))
        y: Math.max(8, Math.min(menu.reqY, menu.height - height - 8))
        implicitHeight: col.implicitHeight + 12
        height: implicitHeight
        radius: 14
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.09)
        opacity: menu.shown ? 1 : 0
        scale: menu.shown ? 1 : 0.96
        transformOrigin: Item.TopLeft
        Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        Behavior on scale { ScaleAnimator { duration: Theme.fast; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        Column {
            id: col
            x: 6
            y: 6
            width: parent.width - 12
            Repeater {
                model: menu.items
                delegate: Loader {
                    width: col.width
                    sourceComponent: modelData.key === "sep" ? sepC : itemC
                    property var entry: modelData
                }
            }
        }
    }
    Component {
        id: sepC
        Item { height: 9; Rectangle { y: 4; x: 8; width: parent.width - 16; height: 1; color: Theme.hairline } }
    }
    Component {
        id: itemC
        Rectangle {
            id: it
            height: 38
            radius: 9
            readonly property bool hot: m.containsMouse || (!!entry.sub && menu.subShown)
            color: hot ? Theme.hover : "transparent"
            readonly property bool on: (entry.key === "fav" && menu.fav) || entry.on === true
            Icon {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                name: entry.icon || ""
                size: 15
                tint: it.on ? Theme.accent : "transparent"
                opacity: it.on ? 1 : (it.hot ? 0.9 : 0.62)
            }
            Text {
                x: 38
                anchors.verticalCenter: parent.verticalCenter
                text: entry.text
                color: it.on ? Theme.accent : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }
            Text {
                visible: !!entry.sub
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "›"
                color: Theme.textTertiary
                font.pixelSize: 16
            }
            MouseArea {
                id: m
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: {
                    if (entry.sub) {
                        const p = it.mapToItem(menu, 0, 0)
                        const right = panel.x + panel.width + sub.width + 4 < menu.width
                        sub.x = right ? panel.x + panel.width + 4 : panel.x - sub.width - 4
                        sub.y = Math.max(8, Math.min(p.y - 6, menu.height - sub.height - 8))
                        menu.subShown = true
                    } else {
                        menu.subShown = false
                    }
                }
                onClicked: if (!entry.sub) menu.act(entry.key)
            }
        }
    }

    // ---- 二级:歌单 ----
    Rectangle {
        id: sub
        width: 220
        height: Math.min(subList.contentHeight + 12, 360)
        radius: 14
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.09)
        opacity: menu.shown && menu.subShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true }

        ListView {
            id: subList
            x: 6; y: 6
            width: parent.width - 12
            height: parent.height - 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: [{ id: "", name: qsTr("新建歌单"), create: true }].concat(playlists.playlists)
            delegate: Rectangle {
                width: subList.width
                height: 38
                radius: 9
                color: sm.containsMouse ? Theme.hover : "transparent"
                Text {
                    x: 14
                    width: parent.width - 14 - 36
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name
                    elide: Text.ElideRight
                    color: modelData.create ? Theme.accent : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
                Text {
                    visible: !modelData.create
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.count !== undefined ? modelData.count : ""
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
                MouseArea {
                    id: sm
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (modelData.create) {
                            const name = playlists.suggestName()
                            menu.addTo(playlists.create(name), name)
                        } else {
                            menu.addTo(modelData.id, modelData.name)
                        }
                    }
                }
            }
        }
    }
}
