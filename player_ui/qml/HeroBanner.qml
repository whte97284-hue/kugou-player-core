import QtQuick
import PlayerUi

// 专辑墙顶部横幅 (杂志跨页式):正在播放时展示当前专辑,否则展示「今日推荐」
// 封套 + 半露的黑胶 (播放时滑出更多并旋转),大标题,行动按钮
Item {
    id: hero
    signal openAlbum(int index, Item coverItem)
    readonly property bool live: player.hasTrack && player.albumIndex >= 0
    // 依赖 library.generation:扫描完成 (版本号变化) 后重新挑选推荐专辑
    property int albumIndex: live ? player.albumIndex : (library.generation >= 0 ? library.featuredAlbum() : -1)
    property var info: library.generation >= 0 ? library.albumInfo(albumIndex) : ({})
    readonly property bool hasAlbum: info && info.title !== undefined
    readonly property real s: Math.min(260, Math.max(200, width * 0.21))
    height: hasAlbum ? s + 170 : 120
    readonly property Item coverItem: sleeve

    Connections {
        target: library
        function onLibraryChanged() { hero.info = library.albumInfo(hero.albumIndex) }
    }
    onAlbumIndexChanged: info = library.albumInfo(albumIndex)

    Item {
        id: art
        visible: hero.hasAlbum
        y: 92
        width: hero.s
        height: hero.s
        Vinyl {
            width: hero.s * 0.94
            height: width
            y: (hero.s - height) / 2
            x: hero.live && player.playing ? hero.s * 0.40 : hero.s * 0.22
            Behavior on x { NumberAnimation { duration: 1000; easing.type: Easing.OutCubic } }
            cover: hero.info.cover || ""
            spinning: hero.live && player.playing
            labelSize: 256
        }
        SoftShadow { target: sleeve; spread: 36; offsetY: 20 }
        RoundedImage {
            id: sleeve
            width: hero.s
            height: hero.s
            radius: 10
            source: hero.info.cover || ""
            decodeSize: 640
        }
        MouseArea {
            anchors.fill: sleeve
            cursorShape: Qt.PointingHandCursor
            onClicked: hero.openAlbum(hero.albumIndex, sleeve)
        }
    }

    Column {
        visible: hero.hasAlbum
        x: hero.s * 1.40 + 56
        width: hero.width - x
        anchors.bottom: art.bottom
        spacing: 10

        // 正在播放时一枚律动图标;推荐专辑时留白
        Item {
            width: 1
            height: 14
            PlayingBars { visible: hero.live; maxHeight: 14 }
        }
        Text {
            width: parent.width
            text: hero.info.title || ""
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Math.min(54, Math.max(36, hero.width * 0.04))
            font.weight: Font.Bold
            font.letterSpacing: -0.8
            lineHeight: 1.04
        }
        Text {
            text: hero.info.artist || ""
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: 18
            font.weight: Font.Medium
        }
        Row {
            spacing: 12
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: hero.info.summary || ""
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }
            QualityBadge { text: hero.info.badge || ""; anchors.verticalCenter: parent.verticalCenter }
        }
        Item { width: 1; height: 8 }
        Row {
            spacing: 14
            IconButton {
                width: 52; height: 52
                filled: true
                icon: hero.live && player.playing ? "pause" : "play"
                iconSize: 20
                onClicked: hero.live ? player.toggle() : player.playAlbum(hero.albumIndex, 0)
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 42; height: 42
                outlined: true
                icon: "album-open"
                iconSize: 17
                onClicked: hero.openAlbum(hero.albumIndex, sleeve)
            }
        }
    }

    // 区块分隔:一条细线 + 数量 (图标代替文字标题)
    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        spacing: 8
        visible: library.albumCount > 0
        Icon { anchors.verticalCenter: parent.verticalCenter; name: "albums"; size: 15; opacity: 0.45 }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: library.albumCount
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }
}
