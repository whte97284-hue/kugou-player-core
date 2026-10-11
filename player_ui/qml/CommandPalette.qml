import QtQuick
import PlayerUi

// 命令面板:Ctrl+K 或直接打字唤出。拼音 / 首字母 / 简繁互搜,方向键选择,回车播放 / 打开专辑
Item {
    id: palette
    property bool shown: false
    signal openAlbum(int index)
    anchors.fill: parent
    visible: dim.opacity > 0

    function open(initialText) {
        shown = true
        input.text = initialText || ""
        searchModel.query = input.text
        results.currentIndex = 0
        input.forceActiveFocus()
    }
    function close() {
        shown = false
        input.focus = false
    }
    function activate(i) {
        if (i < 0 || i >= results.count) return
        const item = results.itemAtIndex(i)
        if (!item) return
        if (item.kind === "album") palette.openAlbum(item.ref)
        else player.playTrack(item.ref)
        close()
    }

    Rectangle {
        id: dim
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
        opacity: palette.shown ? 1 : 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        MouseArea { anchors.fill: parent; onClicked: palette.close() }
    }

    Rectangle {
        id: panel
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(60, parent.height * 0.12)
        width: Math.min(680, parent.width - 80)
        height: 64 + (results.count > 0 ? Math.min(results.contentHeight + 16, parent.height * 0.56) : (input.text.length ? 64 : 0))
        radius: Theme.radiusL
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.1)
        clip: true
        opacity: palette.shown ? 1 : 0
        scale: palette.shown ? 1 : 0.97
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }

        Icon { x: 22; y: 21; name: "search"; size: 22; opacity: 0.6 }
        TextInput {
            id: input
            x: 58
            y: 0
            width: parent.width - 120
            height: 64
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.text
            selectionColor: Theme.accentAlpha(0.4)
            font.family: Theme.fontFamily
            font.pixelSize: 19
            clip: true
            onTextChanged: debounce.restart()
            Keys.onDownPressed: results.currentIndex = Math.min(results.count - 1, results.currentIndex + 1)
            Keys.onUpPressed: results.currentIndex = Math.max(0, results.currentIndex - 1)
            Keys.onReturnPressed: palette.activate(results.currentIndex)
            Keys.onEnterPressed: palette.activate(results.currentIndex)
            Keys.onEscapePressed: palette.close()
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !input.text.length
                text: qsTr("搜索歌曲、专辑、歌手 — 支持拼音和首字母")
                color: Theme.textTertiary
                font: input.font
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 20
            y: 21
            width: 40; height: 22; radius: 5
            color: Theme.hover
            Text {
                anchors.centerIn: parent
                text: "Esc"
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
        }
        Timer {
            id: debounce
            interval: 40
            onTriggered: {
                searchModel.query = input.text
                results.currentIndex = 0
            }
        }
        Rectangle { y: 64; width: parent.width; height: 1; color: Theme.hairline; visible: input.text.length > 0 }

        Text {
            y: 64
            width: parent.width
            height: 64
            visible: input.text.length > 0 && results.count === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("没有找到「%1」").arg(input.text)
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }

        ListView {
            id: results
            x: 8
            y: 72
            width: parent.width - 16
            height: parent.height - 80
            model: searchModel
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 120
            highlight: Rectangle { radius: Theme.radiusM; color: Theme.accentAlpha(0.14) }
            delegate: Item {
                id: rowItem
                width: results.width
                height: 58
                readonly property string kind: model.kind
                readonly property var ref: model.ref
                RoundedImage {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 42; height: 42
                    radius: model.kind === "album" ? 8 : 6
                    source: model.cover
                    decodeSize: 96
                }
                Column {
                    x: 66
                    width: parent.width - 66 - metaRow.width - 20
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: model.title
                        elide: Text.ElideRight
                        color: results.currentIndex === index ? Theme.accent : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }
                    Text {
                        width: parent.width
                        text: model.subtitle
                        elide: Text.ElideRight
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
                Row {
                    id: metaRow
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    QualityBadge { text: model.quality; anchors.verticalCenter: parent.verticalCenter }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "enter"
                        size: 14
                        opacity: results.currentIndex === index ? 0.6 : 0
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: results.currentIndex = index
                    onClicked: palette.activate(index)
                }
            }
        }
    }
}
