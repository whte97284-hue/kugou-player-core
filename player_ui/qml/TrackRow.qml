import QtQuick
import PlayerUi

// 通用曲目行 (专辑 / 歌曲 / 歌手 / 歌单 / 搜索共用)
//  左:序号或小封面 (正在播放时为律动图标) · 标题 + 歌手 · 专辑列 (宽屏) · 收藏 · 品质 · 时长 · 更多
//  单击播放;右键或「更多」弹出菜单;悬停才显示收藏与更多,平时保持安静
Item {
    id: row
    property real trackId: -1
    property int number
    property string title
    property string artist
    property string albumArtist
    property string album
    property string quality
    property string duration
    property string cover               // 非空时以小封面代替序号
    property bool showAlbum: false
    property string highlight           // 搜索词高亮
    property var menuContext: ({})
    readonly property bool current: player.hasTrack && player.trackId === trackId
    signal activated()
    height: cover.length ? 60 : 54

    readonly property bool hovered: mouse.containsMouse || heart.hovered || more.hovered
    readonly property bool fav: playlists.revision >= 0 && playlists.isFavorite(trackId)
    readonly property bool wide: width > 760

    // 当前曲目底色 + 悬停底色 (透明度动画在渲染线程执行,快速划过整列也不掉帧)
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusM
        visible: row.current
        color: Theme.accentAlpha(0.10)
    }
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.hover
        opacity: row.hovered && !row.current ? 1 : 0
        Behavior on opacity { OpacityAnimator { duration: 120 } }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (e) => {
            if (e.button === Qt.RightButton) {
                const p = mapToItem(null, e.x, e.y)
                Nav.trackMenu(row.trackId, p.x, p.y, row.menuContext)
            } else {
                row.activated()
            }
        }
    }

    // ---- 左:序号 / 封面 ----
    Item {
        id: lead
        x: 12
        width: row.cover.length ? 40 : 32
        height: parent.height
        Text {
            anchors.centerIn: parent
            visible: !row.cover.length && !row.current && !row.hovered
            text: row.number
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
        RoundedImage {
            visible: row.cover.length > 0
            anchors.centerIn: parent
            width: 40; height: 40
            radius: 6
            source: row.cover
            decodeSize: 80
        }
        Rectangle {
            visible: row.cover.length > 0 && (row.current || row.hovered)
            anchors.centerIn: parent
            width: 40; height: 40; radius: 6
            color: Qt.rgba(0, 0, 0, 0.45)
        }
        Icon {
            anchors.centerIn: parent
            visible: !row.current && row.hovered
            name: "play"
            size: 14
        }
        Loader {
            anchors.centerIn: parent
            active: row.current
            sourceComponent: PlayingBars { maxHeight: 14 }
        }
    }

    // ---- 右:收藏 · 品质 · 时长 · 更多 ----
    Row {
        id: meta
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        IconButton {
            id: heart
            anchors.verticalCenter: parent.verticalCenter
            width: 32; height: 32
            icon: row.fav ? "heart-filled" : "heart"
            iconTint: row.fav ? Theme.accent : "transparent"
            popOnChange: true
            iconSize: 15
            restOpacity: row.fav ? 0.95 : 0.6
            opacity: row.fav || row.hovered ? 1 : 0
            onClicked: playlists.toggleFavorite(row.trackId)
        }
        // 品质标签放进定宽槽位,专辑列因此上下对齐
        Item {
            anchors.verticalCenter: parent.verticalCenter
            visible: row.wide
            width: 64
            height: 20
            QualityBadge {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: row.quality
                opacity: row.hovered || row.current ? 1 : 0.55
            }
        }
        Text {
            width: 52
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text: row.duration
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
        IconButton {
            id: more
            anchors.verticalCenter: parent.verticalCenter
            width: 32; height: 32
            icon: "more"
            iconSize: 16
            opacity: row.hovered ? 1 : 0
            onClicked: {
                const p = mapToItem(null, width / 2, height)
                Nav.trackMenu(row.trackId, p.x, p.y, row.menuContext)
            }
        }
    }

    // ---- 中:标题 / 歌手 / 专辑 ----
    Item {
        anchors.left: lead.right
        anchors.leftMargin: 14
        anchors.right: meta.left
        anchors.rightMargin: 12
        height: parent.height

        Column {
            anchors.left: parent.left
            anchors.right: albumText.visible ? albumText.left : parent.right
            anchors.rightMargin: albumText.visible ? 20 : 0
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                width: parent.width
                text: row.highlight.length ? Nav.highlight(row.title, row.highlight, Theme.accent) : row.title
                textFormat: row.highlight.length ? Text.StyledText : Text.PlainText
                elide: Text.ElideRight
                color: row.current ? Theme.accent : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: row.current ? Font.DemiBold : Font.Normal
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: row.artist !== row.albumArtist ? row.artist : ""
                elide: Text.ElideRight
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }
        Text {
            id: albumText
            visible: row.showAlbum && row.wide && row.album.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(parent.width * 0.36, 300)
            text: row.album
            elide: Text.ElideRight
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }
}
