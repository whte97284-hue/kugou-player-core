import QtQuick
import PlayerUi

// 曲目右键菜单:播放 / 下一首播放 / 加入队列 / 加入歌单 (二级) / 收藏 / 查看专辑 / 查看歌手 /
// 打开文件位置 / 复制歌名 / 复制文件路径 / 歌曲信息 / 从歌单移除
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
        const list = [
            { key: "play", icon: "play", text: qsTr("播放") },
            { key: "next", icon: "next-up", text: qsTr("下一首播放") },
            { key: "queue", icon: "queue", text: qsTr("添加到播放队列") },
            { key: "playlist", icon: "playlist", text: qsTr("添加到歌单"), sub: true },
            { key: "fav", icon: menu.fav ? "heart-filled" : "heart", text: menu.fav ? qsTr("取消收藏") : qsTr("收藏") },
            { key: "sep" },
            { key: "album", icon: "album-open", text: qsTr("查看专辑") },
            { key: "artist", icon: "artist", text: qsTr("查看歌手") },
            { key: "sep" },
            { key: "reveal", icon: "folder-open", text: qsTr("打开文件位置") },
            { key: "copyName", icon: "note", text: qsTr("复制歌名") },
            { key: "copyPath", icon: "external", text: qsTr("复制文件路径") },
            { key: "info", icon: "more", text: qsTr("歌曲信息") }
        ]
        if (ctx && ctx.playlistId)
            list.push({ key: "sep" }, { key: "remove", icon: "trash", text: qsTr("从歌单移除") })
        items = list
    }
    function act(key) {
        const id = trackId
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
    function addTo(pid, name) {
        const n = playlists.addTracks(pid, [trackId])
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
        width: 196
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
            Text {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                text: entry.text
                color: entry.key === "fav" && menu.fav ? Theme.accent : Theme.text
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
