import QtQuick
import PlayerUi

// 歌曲页:「全部」按标题拼音分组 (字母分节),右缘字母索引快速跳转;「最近播放」按时间倒序列出播放历史。
// 单击即以当前列表为队列播放
Item {
    id: page
    property int bottomInset: 140
    readonly property int sidePadding: width > 1400 ? 56 : 40
    property bool showHistory: false
    readonly property var source: showHistory ? historyTracks : songsModel

    function playAll(shuffled) {
        if (source.count === 0) return
        player.shuffle = shuffled
        player.playTracks(source.trackIds(), shuffled ? Math.floor(Math.random() * source.count) : 0)
    }
    onShowHistoryChanged: list.positionViewAtBeginning()

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding
        anchors.rightMargin: page.sidePadding + 18
        model: page.source
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
                        text: page.source.count.toLocaleString(Qt.locale(), "f", 0)
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                    }
                }
            }
            // 全部 / 最近播放:两个文字标签,选中的那个用正文色并带一条强调色短线
            Row {
                anchors.right: parent.right
                y: 74
                spacing: 22
                Repeater {
                    model: [{ label: qsTr("全部"), history: false }, { label: qsTr("最近播放"), history: true }]
                    Item {
                        readonly property bool active: page.showHistory === modelData.history
                        width: tabText.implicitWidth
                        height: 32
                        Text {
                            id: tabText
                            text: modelData.label
                            color: parent.active ? Theme.text : (tabMouse.containsMouse ? Theme.textSecondary : Theme.textTertiary)
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            font.weight: parent.active ? Font.DemiBold : Font.Normal
                            Behavior on color { ColorAnimation { duration: Theme.fast } }
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            width: 16; height: 2; radius: 1
                            color: Theme.accent
                            scale: parent.active ? 1 : 0
                            Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutBack } }
                        }
                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.showHistory = modelData.history
                        }
                    }
                }
            }
        }
        // 最近播放为空时的提示
        footer: Item {
            width: list.width
            height: page.showHistory && historyTracks.count === 0 ? 120 : 0
            Text {
                visible: parent.height > 0
                anchors.centerIn: parent
                text: qsTr("还没有播放记录")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }
        }

        section.property: page.showHistory ? "" : "letter"
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
            readonly property string letter: page.showHistory ? "" : model.letter
            trackId: model.trackId
            number: model.number
            title: model.title
            artist: model.artist
            album: model.album
            quality: model.quality
            duration: model.duration
            cover: model.cover
            showAlbum: true
            onActivated: player.playTracks(page.source.trackIds(), index)
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
        visible: !page.showHistory
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
