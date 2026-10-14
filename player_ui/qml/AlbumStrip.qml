import QtQuick
import PlayerUi

// 横向专辑带 (歌手详情的专辑、专辑详情页尾「更多这位歌手的专辑」共用)
// model: library.artistAlbums() 返回的列表 [{ index, title, year, cover, badge }]
ListView {
    id: strip
    property int coverSize: 150
    signal open(int index, Item coverItem)
    width: parent ? parent.width : 0
    height: count > 0 ? coverSize + 64 : 0
    visible: count > 0
    orientation: ListView.Horizontal
    spacing: 22
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    SmoothScroll { flickable: strip; horizontal: true }
    delegate: Item {
        width: strip.coverSize
        height: strip.height
        readonly property bool hovered: am.containsMouse
        SoftShadow { target: ac; spread: 18; offsetY: 10; strength: parent.hovered ? 0.9 : 0.5 }
        RoundedImage {
            id: ac
            width: strip.coverSize; height: strip.coverSize
            radius: 8
            y: parent.hovered ? -3 : 0
            Behavior on y { YAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
            source: modelData.cover
            decodeSize: Math.ceil(strip.coverSize * 1.5)
        }
        Text {
            y: strip.coverSize + 10
            width: parent.width
            text: modelData.title
            elide: Text.ElideRight
            color: parent.hovered ? Theme.accent : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Row {
            y: strip.coverSize + 30
            width: parent.width
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: modelData.year || ""
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
            QualityBadge { anchors.verticalCenter: parent.verticalCenter; text: modelData.badge || "" }
        }
        MouseArea {
            id: am
            anchors.fill: parent
            hoverEnabled: !Theme.scrolling
            cursorShape: Qt.PointingHandCursor
            onClicked: strip.open(modelData.index, ac)
        }
    }
}
