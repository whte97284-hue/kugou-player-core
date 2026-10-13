import QtQuick
import PlayerUi

// 歌曲信息:右键菜单「歌曲信息」打开的居中卡片 —— 标签、格式与音频参数、文件路径
Item {
    id: dlg
    anchors.fill: parent
    visible: card.opacity > 0
    property bool shown: false
    property real trackId: -1
    property var info: ({})

    function open(id) {
        trackId = id
        info = library.trackInfo(id)
        if (info.path === undefined) return
        shown = true
        forceActiveFocus()
    }
    function close() { shown = false }

    Keys.onEscapePressed: close()

    readonly property var rows: {
        const t = info
        if (t.path === undefined) return []
        const audio = []
        if (t.sampleRate > 0) audio.push((t.sampleRate / 1000).toLocaleString(Qt.locale(), "f", t.sampleRate % 1000 ? 1 : 0) + " kHz")
        if (t.bits > 0) audio.push(t.bits + " bit")
        if (t.channels > 0) audio.push(t.channels === 1 ? qsTr("单声道") : t.channels === 2 ? qsTr("立体声") : qsTr("%1 声道").arg(t.channels))
        if (t.bitrate > 0) audio.push(t.bitrate + " kbps")
        const no = t.trackNo > 0 ? (t.discNo > 0 ? t.discNo + "-" : "") + t.trackNo : ""
        return [
            { k: qsTr("标题"), v: t.title },
            { k: qsTr("歌手"), v: t.artist },
            { k: qsTr("专辑"), v: t.album },
            { k: qsTr("专辑歌手"), v: t.albumArtist !== t.artist ? t.albumArtist : "" },
            { k: qsTr("年份"), v: t.year },
            { k: qsTr("流派"), v: t.genre },
            { k: qsTr("音轨号"), v: no },
            { k: qsTr("时长"), v: t.duration },
            { k: qsTr("格式"), v: t.codec && t.codec !== t.format ? t.format + " / " + t.codec : t.format },
            { k: qsTr("音频"), v: audio.join("  ·  ") },
            { k: qsTr("品质"), v: t.quality },
            { k: qsTr("文件大小"), v: t.size > 0 ? Theme.formatBytes(t.size) : "" },
            { k: qsTr("文件位置"), v: t.path + (t.isCue ? qsTr("  (CUE 分轨)") : "") + (t.exists ? "" : qsTr("  (文件已不存在)")), path: true },
            { k: qsTr("加密原文件"), v: t.source || "", path: true }
        ].filter(r => r.v !== undefined && String(r.v).length > 0)
    }

    // 遮罩:点空白处关闭
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.42)
        opacity: dlg.shown ? 1 : 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
        MouseArea {
            anchors.fill: parent
            enabled: dlg.shown
            acceptedButtons: Qt.AllButtons
            onPressed: dlg.close()
            onWheel: (w) => {}
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48)
        height: body.implicitHeight + 40
        radius: Theme.radiusL
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.09)
        opacity: dlg.shown ? 1 : 0
        scale: dlg.shown ? 1 : 0.97
        Behavior on opacity { OpacityAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        Column {
            id: body
            x: 24
            y: 20
            width: parent.width - 48
            spacing: 14

            Text {
                width: parent.width
                text: qsTr("歌曲信息")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }

            Column {
                width: parent.width
                spacing: 8
                Repeater {
                    model: dlg.rows
                    delegate: Row {
                        width: parent.width
                        spacing: 14
                        Text {
                            width: 64
                            text: modelData.k
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            topPadding: 1
                        }
                        TextEdit {
                            // 只读但可选中复制
                            width: parent.width - 64 - 14
                            text: modelData.v
                            readOnly: true
                            selectByMouse: true
                            wrapMode: modelData.path ? TextEdit.WrapAnywhere : TextEdit.Wrap
                            color: Theme.text
                            selectionColor: Theme.accentAlpha(0.45)
                            selectedTextColor: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                    }
                }
            }

            Row {
                anchors.right: parent.right
                spacing: 10
                Capsule {
                    height: 34
                    text: qsTr("复制路径")
                    onClicked: { library.copyToClipboard(dlg.info.path); Nav.toast("check", qsTr("已复制文件路径")) }
                }
                Capsule {
                    height: 34
                    text: qsTr("打开文件位置")
                    onClicked: if (!library.revealTrack(dlg.trackId)) Nav.toast("folder", qsTr("找不到这首歌所在的文件夹"))
                }
                Capsule {
                    height: 34
                    filled: true
                    text: qsTr("关闭")
                    onClicked: dlg.close()
                }
            }
        }
    }
}
