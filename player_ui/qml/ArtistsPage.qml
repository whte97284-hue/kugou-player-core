import QtQuick
import PlayerUi

// 歌手墙:圆形头像 (取其代表专辑封面) + 名字;字母索引跳转
Item {
    id: page
    property int bottomInset: 140
    readonly property int sidePadding: width > 1400 ? 56 : 40
    readonly property int minCell: 168
    readonly property int columns: Math.max(3, Math.floor((width - sidePadding * 2 - 24) / minCell))
    readonly property real cellW: (width - sidePadding * 2 - 24) / columns

    function avatarOf(i) {
        for (let k = 0; k < grid.contentItem.children.length; ++k) {
            const c = grid.contentItem.children[k]
            if (c.artistIndex === i && c.avatarItem && c.visible) return c.avatarItem
        }
        return null
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: page.sidePadding
        anchors.rightMargin: page.sidePadding + 24
        model: artistModel
        cellWidth: page.cellW
        cellHeight: page.cellW + 56
        cacheBuffer: 600
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 64
        bottomMargin: page.bottomInset
        SmoothScroll { flickable: grid }

        delegate: Item {
            id: cell
            width: grid.cellWidth
            height: grid.cellHeight
            readonly property string letter: model.letter
            readonly property int artistIndex: model.artistIndex
            readonly property Item avatarItem: avatarImg
            readonly property bool hovered: m.containsMouse
            readonly property real d: width - 36

            Item {
                id: avatar
                x: 18
                width: cell.d
                height: cell.d
                y: cell.hovered ? -5 : 0
                scale: m.pressed ? 0.97 : 1
                Behavior on y { YAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
                Behavior on scale { ScaleAnimator { duration: Theme.fast } }
                SoftShadow { target: avatarImg; spread: 18; offsetY: cell.hovered ? 16 : 10; strength: cell.hovered ? 0.9 : 0.55 }
                RoundedImage {
                    id: avatarImg
                    anchors.fill: parent
                    radius: width / 2
                    source: model.cover
                    decodeSize: Math.ceil(cell.d * 1.3)
                }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.accent
                    opacity: cell.hovered ? 0.9 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                }
            }
            Text {
                y: cell.d + 14
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: model.name
                elide: Text.ElideRight
                color: cell.hovered ? Theme.text : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                y: cell.d + 35
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: model.summary
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
            MouseArea {
                id: m
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Nav.openArtistFrom(model.artistIndex, avatarImg)
            }
        }

        onContentYChanged: syncLetter.restart()
        Timer {
            id: syncLetter
            interval: 60
            onTriggered: {
                const i = grid.indexAt(40, grid.contentY + 40)
                const it = i >= 0 ? grid.itemAtIndex(i) : null
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
        letters: artistModel.letters
        onJump: (l) => {
            const i = artistModel.indexOfLetter(l)
            if (i >= 0) grid.positionViewAtIndex(i, GridView.Beginning)
        }
    }
}
