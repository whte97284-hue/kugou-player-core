import QtQuick
import PlayerUi

// 全部歌曲:按标题拼音分组 (字母分节),右缘字母索引快速跳转;单击即以整个列表为队列播放
Item {
    id: page
    property int bottomInset: 140
    readonly property int sidePadding: width > 1400 ? 56 : 40

    function playAll(shuffled) {
        if (songsModel.count === 0) return
        player.shuffle = shuffled
        player.playTracks(songsModel.trackIds(), shuffled ? Math.floor(Math.random() * songsModel.count) : 0)
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding
        anchors.rightMargin: page.sidePadding + 18
        model: songsModel
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: page.bottomInset
        cacheBuffer: 600
        reuseItems: true
        spacing: 0
        SmoothScroll { flickable: list }

        header: Item {
            width: list.width
            height: 150
            Row {
                y: 62
                spacing: 14
                IconButton {
                    width: 56; height: 56
                    filled: true
                    icon: "play"
                    iconSize: 22
                    onClicked: page.playAll(false)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 44
                    outlined: true
                    icon: "shuffle"
                    iconSize: 18
                    onClicked: page.playAll(true)
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 10
                    spacing: 8
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "note"; size: 16; opacity: 0.45 }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: songsModel.count.toLocaleString(Qt.locale(), "f", 0)
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                    }
                }
            }
        }

        section.property: "letter"
        section.delegate: Item {
            width: list.width
            height: 44
            Text {
                x: 14
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                text: section
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.Bold
            }
        }

        delegate: TrackRow {
            width: list.width
            readonly property string letter: model.letter
            trackId: model.trackId
            number: model.number
            title: model.title
            artist: model.artist
            album: model.album
            quality: model.quality
            duration: model.duration
            cover: model.cover
            showAlbum: true
            onActivated: player.playTracks(songsModel.trackIds(), index)
        }

        onContentYChanged: syncLetter.restart()
        Timer {
            id: syncLetter
            interval: 60
            onTriggered: {
                const i = list.indexAt(20, list.contentY + 60)
                const it = i >= 0 ? list.itemAtIndex(i) : null
                if (it) rail.current = it.letter
            }
        }
    }

    LetterRail {
        id: rail
        anchors.right: parent.right
        anchors.rightMargin: page.sidePadding - 18
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -page.bottomInset / 4
        letters: songsModel.letters
        onJump: (l) => {
            const i = songsModel.indexOfLetter(l)
            if (i >= 0) list.positionViewAtIndex(i, ListView.Beginning)
        }
    }
}
