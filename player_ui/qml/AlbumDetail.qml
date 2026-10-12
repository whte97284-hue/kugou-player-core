import QtQuick
import PlayerUi

// 专辑详情:大封面 + 信息区 (随列表一起滚动) + 曲目列表;多碟专辑按碟分组
Item {
    id: page
    property int bottomInset: 140
    readonly property Item heroCover: list.headerItem ? list.headerItem.coverItem : null
    readonly property bool isCurrentAlbum: player.hasTrack && player.albumIndex === albumTracks.albumIndex
    // 封面正飞回专辑墙:唱片随之隐去,不让它在封面离开后单独露出来
    property bool heroAway: false
    signal back()

    function heroCoverRect() {
        if (!heroCover) return Qt.rect(0, 0, 0, 0)
        const p = heroCover.mapToItem(page, 0, 0)
        return Qt.rect(p.x, p.y, heroCover.width, heroCover.height)
    }
    function resetScroll() { list.positionViewAtBeginning() }

    readonly property int sidePadding: width > 1400 ? 64 : 44

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding
        anchors.rightMargin: page.sidePadding
        model: albumTracks
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: page.bottomInset
        cacheBuffer: 800
        reuseItems: true
        spacing: 2
        SmoothScroll { flickable: list }

        section.property: albumTracks.multiDisc ? "disc" : ""
        section.delegate: Item {
            width: list.width
            height: 46
            Row {
                x: 14
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 8
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "disc"; size: 15; tint: Theme.accent }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: section
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }
            }
        }

        header: Item {
            id: hero
            width: list.width
            readonly property Item coverItem: heroCoverItem
            readonly property real coverSize: Math.min(280, Math.max(200, list.width * 0.24))
            height: coverSize + 150

            IconButton {
                y: 50
                x: -8
                icon: "back"
                iconSize: 20
                onClicked: page.back()
            }

            Vinyl {
                width: hero.coverSize * 0.94
                height: width
                y: heroCoverItem.y + (hero.coverSize - height) / 2
                x: page.isCurrentAlbum ? hero.coverSize * 0.42 : hero.coverSize * 0.04
                Behavior on x { NumberAnimation { duration: 1000; easing.type: Easing.OutCubic } }
                cover: albumTracks.cover
                opacity: page.heroAway ? 0 : 1
                Behavior on opacity { OpacityAnimator { duration: 90 } }
                spinning: page.isCurrentAlbum && player.playing
                labelSize: 256
            }
            SoftShadow { target: heroCoverItem; spread: 36; offsetY: 20 }
            RoundedImage {
                id: heroCoverItem
                y: 104
                width: hero.coverSize
                height: hero.coverSize
                radius: 10
                source: albumTracks.cover
                decodeSize: 640
            }

            Column {
                anchors.left: heroCoverItem.right
                anchors.leftMargin: page.isCurrentAlbum ? hero.coverSize * 0.42 + 44 : 44
                Behavior on anchors.leftMargin { NumberAnimation { duration: 1000; easing.type: Easing.OutCubic } }
                anchors.right: parent.right
                anchors.bottom: heroCoverItem.bottom
                spacing: 10

                Text {
                    width: parent.width
                    text: albumTracks.title
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: width > 640 ? 44 : 34
                    font.weight: Font.Bold
                    font.letterSpacing: -0.5
                    lineHeight: 1.05
                }
                Text {
                    text: albumTracks.artist
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 18
                    font.weight: Font.Medium
                    font.underline: artistMouse.containsMouse
                    MouseArea {
                        id: artistMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Nav.openArtist(library.artistIndexOf(albumTracks.artist))
                    }
                }
                Row {
                    spacing: 12
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: albumTracks.summary
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                    Repeater {
                        model: albumTracks.badges
                        QualityBadge { text: modelData; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                Item { width: 1; height: 8 }
                Row {
                    spacing: 12
                    IconButton {
                        width: 52; height: 52
                        filled: true
                        icon: page.isCurrentAlbum && player.playing ? "pause" : "play"
                        iconSize: 20
                        onClicked: page.isCurrentAlbum ? player.toggle() : player.playAlbum(albumTracks.albumIndex, 0)
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 42; height: 42
                        outlined: true
                        icon: "shuffle"
                        iconSize: 17
                        onClicked: {
                            player.shuffle = true
                            player.playAlbum(albumTracks.albumIndex, Math.floor(Math.random() * Math.max(1, list.count)))
                        }
                    }
                }
            }

            // 正在播放这张专辑时,信息区右侧浮现律动频谱
            Spectrum {
                anchors.right: parent.right
                anchors.bottom: heroCoverItem.bottom
                anchors.bottomMargin: 4
                width: Math.min(220, parent.width * 0.22)
                height: 56
                bars: 24
                opacity: page.isCurrentAlbum ? 0.9 : 0
                visible: opacity > 0 && parent.width > 900
                Behavior on opacity { OpacityAnimator { duration: Theme.slow } }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                width: parent.width
                height: 1
                color: Theme.hairline
            }
        }

        delegate: TrackRow {
            width: list.width
            trackId: model.trackId
            number: model.number
            title: model.title
            artist: model.artist
            albumArtist: albumTracks.artist
            quality: model.quality
            duration: model.duration
            onActivated: player.playAlbum(albumTracks.albumIndex, index)
        }
    }
}
