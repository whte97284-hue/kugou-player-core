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

        Row {
            spacing: 10
            Row {
                visible: hero.live
                anchors.verticalCenter: parent.verticalCenter
                height: 12
                spacing: 2
                Repeater {
                    model: 4
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: 3
                        radius: 1.5
                        height: 3 + 9 * (player.playing ? (player.spectrum[2 + index * 6] || 0) : 0.15)
                        color: Theme.accent
                    }
                }
            }
            Text {
                text: hero.live ? qsTr("正在播放") : qsTr("今日推荐")
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: 2.5
            }
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
            spacing: 12
            Rectangle {
                width: heroPlay.implicitWidth + 62
                height: 44
                radius: 22
                color: Theme.accent
                scale: heroPlayMouse.pressed ? 0.96 : 1
                Behavior on scale { ScaleAnimator { duration: Theme.fast } }
                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property bool pausing: hero.live && player.playing
                        name: (pausing ? "pause" : "play") + (Theme.accentIsLight ? "-dark" : "")
                        size: 17
                    }
                    Text {
                        id: heroPlay
                        anchors.verticalCenter: parent.verticalCenter
                        text: hero.live ? (player.playing ? qsTr("暂停") : qsTr("继续播放")) : qsTr("播放")
                        color: Theme.onAccent
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                }
                MouseArea {
                    id: heroPlayMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: hero.live ? player.toggle() : player.playAlbum(hero.albumIndex, 0)
                }
            }
            Rectangle {
                width: openLabel.implicitWidth + 40
                height: 44
                radius: 22
                color: openMouse.containsMouse ? Theme.pressed : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)
                Text {
                    id: openLabel
                    anchors.centerIn: parent
                    text: qsTr("打开专辑")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                }
                MouseArea {
                    id: openMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: hero.openAlbum(hero.albumIndex, sleeve)
                }
            }
        }
    }

    // 区块标题
    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        spacing: 12
        Text {
            text: qsTr("全部专辑")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 22
            font.weight: Font.Bold
        }
        Text {
            anchors.baseline: parent.children[0].baseline
            text: library.albumCount > 0 ? qsTr("%1 张  ·  %2 首").arg(library.albumCount).arg(library.trackCount) : ""
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
    }
}
