import QtQuick
import QtQuick.Controls.Basic
import PlayerUi

// 专辑详情:大封面 + 信息区 (随列表一起滚动) + 曲目列表;多碟专辑按碟分组
Item {
    id: page
    property int bottomInset: 140
    readonly property Item heroCover: list.headerItem ? list.headerItem.coverItem : null
    readonly property bool isCurrentAlbum: player.hasTrack && player.albumIndex === albumTracks.albumIndex
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

        section.property: albumTracks.multiDisc ? "disc" : ""
        section.delegate: Text {
            width: list.width
            height: 44
            verticalAlignment: Text.AlignBottom
            bottomPadding: 8
            leftPadding: 14
            text: qsTr("碟 %1").arg(section)
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.letterSpacing: 1.5
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
                    text: qsTr("专辑")
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2
                }
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
                    // 主按钮:强调色胶囊
                    Rectangle {
                        width: playLabel.implicitWidth + 64
                        height: 44
                        radius: 22
                        color: Theme.accent
                        scale: playMouse.pressed ? 0.96 : 1
                        Behavior on scale { ScaleAnimator { duration: Theme.fast } }
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: Theme.accentIsLight ? (page.isCurrentAlbum && player.playing ? "pause-dark" : "play-dark")
                                                                                : (page.isCurrentAlbum && player.playing ? "pause" : "play")
                                size: 18
                            }
                            Text {
                                id: playLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.isCurrentAlbum && player.playing ? qsTr("暂停") : qsTr("播放")
                                color: Theme.onAccent
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }
                        MouseArea {
                            id: playMouse
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.isCurrentAlbum ? player.toggle() : player.playAlbum(albumTracks.albumIndex, 0)
                        }
                    }
                    Rectangle {
                        width: 44; height: 44; radius: 22
                        color: shuffleMouse.containsMouse ? Theme.pressed : Theme.hover
                        border.width: 1
                        border.color: Theme.hairline
                        Icon { anchors.centerIn: parent; name: "shuffle"; size: 18; opacity: 0.85 }
                        MouseArea {
                            id: shuffleMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: player.playAlbum(albumTracks.albumIndex,
                                                        Math.floor(Math.random() * Math.max(1, list.count)))
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
            number: model.number
            title: model.title
            artist: model.artist
            albumArtist: albumTracks.artist
            quality: model.quality
            duration: model.duration
            current: player.trackId === model.trackId
            onActivated: player.playAlbum(albumTracks.albumIndex, index)
        }

        ScrollBar.vertical: ScrollBar {
            contentItem: Rectangle {
                implicitWidth: 4
                radius: 2
                color: Qt.rgba(1, 1, 1, parent.pressed ? 0.4 : 0.18)
            }
        }
    }
}
