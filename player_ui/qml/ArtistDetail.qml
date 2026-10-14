import QtQuick
import PlayerUi

// 歌手详情:圆形大头像 + 名字 + 播放;横向专辑带;全部曲目
Item {
    id: page
    property int bottomInset: 140
    property int artistIndex: -1
    signal back()
    readonly property int sidePadding: Theme.pagePadding(width)
    readonly property Item heroAvatar: list.headerItem ? list.headerItem.avatarItem : null
    readonly property var info: library.generation >= 0 ? library.artistInfo(artistIndex) : ({})
    readonly property var albums: library.generation >= 0 ? library.artistAlbums(artistIndex) : []

    onArtistIndexChanged: {
        artistTracks.artistIndex = artistIndex
        list.userScrolled = false
        Qt.callLater(list.positionViewAtBeginning)
    }

    function play(shuffled) {
        if (artistTracks.count === 0) return
        player.shuffle = shuffled
        player.playTracks(artistTracks.trackIds(), shuffled ? Math.floor(Math.random() * artistTracks.count) : 0)
    }

    // 头像色调的柔光,让页面不至于只是一张列表
    ShaderEffect {
        x: page.sidePadding - 160
        y: -120
        width: 640
        height: 560
        property color color: Theme.accent
        property real intensity: 0.16
        fragmentShader: "qrc:/shaders/glow.frag.qsb"
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding
        anchors.rightMargin: page.sidePadding
        model: artistTracks
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
            readonly property Item avatarItem: avatarImg
            onHeightChanged: if (!list.userScrolled) Qt.callLater(list.positionViewAtBeginning)
            width: list.width
            Item { width: 1; height: 44 }
            IconButton { x: -8; icon: "back"; iconSize: 20; onClicked: page.back() }
            Item { width: 1; height: 18 }

            Row {
                spacing: 40
                Item {
                    id: avatar
                    width: Math.min(220, Math.max(160, list.width * 0.18))
                    height: width
                    SoftShadow { target: avatarImg; spread: 30; offsetY: 18 }
                    RoundedImage {
                        id: avatarImg
                        anchors.fill: parent
                        radius: width / 2
                        source: page.info.cover || ""
                        decodeSize: 440
                    }
                }
                Column {
                    anchors.verticalCenter: avatar.verticalCenter
                    spacing: 12
                    Text {
                        width: list.width - avatar.width - 40
                        text: page.info.name || ""
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: list.width > 800 ? 48 : 36
                        font.weight: Font.Bold
                        font.letterSpacing: -0.5
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
                    }
                }
            }

            Item { width: 1; height: 40 }
            // 专辑带
            AlbumStrip {
                width: list.width
                model: page.albums
                onOpen: (index) => Nav.openAlbum(index)
            }
            Item { width: 1; height: 18 }
            Rectangle { width: list.width; height: 1; color: Theme.hairline }
            Item { width: 1; height: 10 }
        }

        delegate: TrackRow {
            width: list.width
            trackId: model.trackId
            number: model.number
            title: model.title
            artist: model.artist
            albumArtist: page.info.name || ""
            album: model.album
            quality: model.quality
            duration: model.duration
            cover: model.cover
            showAlbum: true
            onActivated: player.playTracks(artistTracks.trackIds(), index)
        }
    }
}
