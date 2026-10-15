import QtQuick
import PlayerUi

// 歌词来源面板:从右下角的来源胶囊弹出,盖在歌词栏上
//  当前来源 (强调色圆点 + 来源名 + 文件 / 歌名;可保存到本地、在文件夹中显示)
//  → 搜索框 (默认「歌名 歌手」,打开时自动搜一次) → 候选列表 (点一下立即换上,勾号标出正在用的)
// 只用强调色与文字的明暗,不给各个来源配颜色
Item {
    id: panelRoot
    property bool shown: false
    // 弹出锚点:面板右下角对齐这个点 (在 panelRoot 坐标系里)
    property real anchorRight: width - 20
    property real anchorBottom: height - 60
    anchors.fill: parent
    visible: pop.opacity > 0
    z: 5

    readonly property var src: player.lyricSource
    readonly property bool hasSource: !!(src && src.kind && src.kind !== "none")
    property int pending: -1   // 点了、还在拉取的候选

    function open() {
        shown = true
        query.text = player.lyricQuery()
        if (!player.lyricSearching && player.lyricCandidates.length === 0) player.searchLyrics(query.text)
    }
    property int hoverRow: -1  // 截图用:模拟悬停
    function pick(i) { if (player.lyricPicked !== i) { pending = i; player.useLyricCandidate(i) } }
    function save() { player.saveLyrics() }
    function close() { shown = false; query.focus = false }
    function fmt(sec) {
        if (!(sec > 0)) return ""
        const s = Math.round(sec)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    Connections {
        target: player
        function onLyricSearchChanged() { if (player.lyricPicked >= 0 || player.lyricSearchNote.length) panelRoot.pending = -1 }
        function onTrackChanged() { panelRoot.pending = -1; if (panelRoot.shown) query.text = player.lyricQuery() }
    }

    // 点面板外收起
    MouseArea {
        anchors.fill: parent
        enabled: panelRoot.shown
        acceptedButtons: Qt.AllButtons
        onPressed: panelRoot.close()
        onWheel: (w) => w.accepted = true
    }

    // 面板连同投影一起从右下角 (来源胶囊处) 轻轻放大淡入
    Item {
        id: pop
        width: Math.min(420, panelRoot.width - 40)
        // 高度随结果多少伸缩 (至少留四行,搜索中不跳),放不下时列表内部滚动
        height: Math.min(600, panelRoot.anchorBottom - 72, list.y + Math.max(4 * 60, list.contentHeight) + 12)
        Behavior on height { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        x: panelRoot.anchorRight - width
        y: panelRoot.anchorBottom - height
        transformOrigin: Item.BottomRight
        opacity: panelRoot.shown ? 1 : 0
        scale: panelRoot.shown ? 1 : 0.94
        Behavior on opacity { OpacityAnimator { duration: panelRoot.shown ? 220 : 140; easing.type: Easing.OutCubic } }
        Behavior on scale { ScaleAnimator { duration: panelRoot.shown ? 420 : 160; easing.type: Easing.OutQuint } }
        SoftShadow { target: card; strength: 0.85; spread: 30; offsetY: 16 }

        Rectangle {
            id: card
            anchors.fill: parent
            radius: Theme.radiusL
            color: Theme.panel
            border.width: 1
            border.color: Qt.rgba(1, 0.96, 0.9, 0.08)
            // 吃掉面板内的点击,不让它漏到「点外面收起」
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }
            Keys.onEscapePressed: panelRoot.close()

            readonly property int pad: 20

            // ---- 标题 ----
            Text {
                id: heading
                x: card.pad
                y: 22
                text: qsTr("歌词来源")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 16
                font.weight: Font.Bold
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: heading.verticalCenter
                width: 32; height: 32
                icon: "close"
                iconSize: 16
                onClicked: panelRoot.close()
            }

            // ---- 当前来源 ----
            Rectangle {
                id: current
                x: card.pad
                y: heading.y + heading.height + 16
                width: card.width - card.pad * 2
                height: 64
                radius: Theme.radiusM
                color: Theme.accentAlpha(0.08)
                // 来源变了 (换上候选 / 存到本地) 时整块轻轻亮一下
                Rectangle {
                    id: flash
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.accentAlpha(0.14)
                    opacity: 0
                    OpacityAnimator on opacity { id: flashAnim; running: false; from: 1; to: 0; duration: 700; easing.type: Easing.OutCubic }
                }
                Connections {
                    target: player
                    function onLyricSourceChanged() { if (panelRoot.shown) flashAnim.restart() }
                }

                Rectangle {
                    id: dot
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: Theme.accent
                    opacity: panelRoot.hasSource ? 1 : 0.35
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                }
                Column {
                    anchors.left: dot.right
                    anchors.leftMargin: 12
                    anchors.right: actions.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: panelRoot.hasSource ? panelRoot.src.label : qsTr("还没有歌词")
                        color: panelRoot.hasSource ? Theme.accent : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideMiddle
                        text: panelRoot.hasSource ? panelRoot.src.detail : qsTr("从下面的结果里挑一份")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Capsule {
                        visible: !!(panelRoot.src && panelRoot.src.path)
                        text: qsTr("显示文件")
                        height: 30
                        onClicked: libtools.reveal(panelRoot.src.path)
                    }
                    Capsule {
                        visible: !!(panelRoot.src && panelRoot.src.savable)
                        text: qsTr("保存到本地")
                        filled: true
                        height: 30
                        onClicked: panelRoot.save()
                    }
                }
            }

            // ---- 搜索框 ----
            Rectangle {
                id: field
                x: card.pad
                y: current.y + current.height + 16
                width: card.width - card.pad * 2
                height: 40
                radius: 20
                color: Qt.rgba(1, 0.96, 0.9, query.activeFocus ? 0.08 : 0.05)
                border.width: 1
                border.color: query.activeFocus ? Theme.accentAlpha(0.5) : "transparent"
                Behavior on border.color { ColorAnimation { duration: Theme.fast } }
                Icon {
                    id: glass
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 16
                    opacity: 0.5
                }
                TextInput {
                    id: query
                    anchors.left: glass.right
                    anchors.leftMargin: 10
                    anchors.right: go.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.text
                    selectionColor: Theme.accentAlpha(0.4)
                    selectedTextColor: Theme.text
                    selectByMouse: true
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    clip: true
                    Keys.onReturnPressed: player.searchLyrics(text)
                    Keys.onEnterPressed: player.searchLyrics(text)
                    Keys.onEscapePressed: panelRoot.close()
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !query.text.length
                        text: qsTr("歌名、歌手")
                        color: Theme.textTertiary
                        font: query.font
                    }
                }
                Capsule {
                    id: go
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    height: 32
                    pad: 16
                    accent: true
                    text: qsTr("搜索")
                    onClicked: player.searchLyrics(query.text)
                }
            }

            // ---- 结果状态 ----
            Item {
                id: status
                x: card.pad
                y: field.y + field.height + 14
                width: card.width - card.pad * 2
                height: 18
                Text {
                    id: statusText
                    anchors.verticalCenter: parent.verticalCenter
                    text: player.lyricSearching ? qsTr("正在搜索")
                        : player.lyricSearchNote.length ? player.lyricSearchNote
                        : player.lyricCandidates.length ? qsTr("找到 %1 份,点一下即可换上").arg(player.lyricCandidates.length)
                        : ""
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
                // 搜索中:三个强调色小点依次呼吸
                Row {
                    anchors.left: statusText.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    visible: player.lyricSearching
                    Repeater {
                        model: 3
                        Rectangle {
                            width: 4; height: 4; radius: 2
                            color: Theme.accent
                            SequentialAnimation on opacity {
                                running: player.lyricSearching && panelRoot.shown
                                loops: Animation.Infinite
                                PauseAnimation { duration: index * 160 }
                                NumberAnimation { from: 0.25; to: 1; duration: 360; easing.type: Easing.InOutSine }
                                NumberAnimation { from: 1; to: 0.25; duration: 360; easing.type: Easing.InOutSine }
                                PauseAnimation { duration: (2 - index) * 160 }
                            }
                        }
                    }
                }
            }

            // ---- 候选列表 ----
            ListView {
                id: list
                x: 8
                y: status.y + status.height + 8
                width: card.width - 16
                height: card.height - y - 8
                clip: true
                model: player.lyricCandidates
                boundsBehavior: Flickable.StopAtBounds
                opacity: player.lyricSearching ? 0.4 : 1
                Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                // 每次出结果整列重新错峰淡入
                property int wave: 0
                onModelChanged: wave++

                delegate: Item {
                    id: row
                    width: list.width
                    height: 60
                    readonly property bool inUse: player.lyricPicked === index
                    readonly property bool loading: panelRoot.pending === index
                    opacity: 0
                    property real lift: 8
                    transform: Translate { y: row.lift }
                    Component.onCompleted: appear.start()
                    ParallelAnimation {
                        id: appear
                        SequentialAnimation {
                            PauseAnimation { duration: Math.min(index, 8) * 30 }
                            ParallelAnimation {
                                OpacityAnimator { target: row; from: 0; to: 1; duration: 260; easing.type: Easing.OutCubic }
                                NumberAnimation { target: row; property: "lift"; from: 8; to: 0; duration: 420; easing.type: Easing.OutQuint }
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        radius: Theme.radiusS
                        color: row.inUse ? Theme.accentAlpha(0.10) : Theme.hover
                        opacity: row.inUse || rowMouse.containsMouse || panelRoot.hoverRow === index ? 1 : 0
                        Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: tags.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: modelData.title
                            color: row.inUse ? Theme.accent : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            font.weight: row.inUse ? Font.DemiBold : Font.Medium
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: [modelData.artist, modelData.album, panelRoot.fmt(modelData.duration)].filter(s => s && s.length).join("  ·  ")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                        }
                    }
                    // 右侧:来源 / 逐字 / 无时间轴 小标签 (同一种淡强调色);正在用的换成勾,拉取中换成呼吸点
                    Row {
                        id: tags
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        component Tag: Rectangle {
                            property alias label: t.text
                            property bool dim: false
                            width: t.implicitWidth + 14
                            height: 20
                            radius: 10
                            color: dim ? "transparent" : Theme.accentAlpha(0.12)
                            border.width: dim ? 1 : 0
                            border.color: Qt.rgba(1, 0.96, 0.9, 0.12)
                            Text {
                                id: t
                                anchors.centerIn: parent
                                color: parent.dim ? Theme.textTertiary : Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                        }
                        Tag { label: qsTr("逐字"); visible: modelData.wordTimed && modelData.synced }
                        Tag { label: qsTr("无时间轴"); dim: true; visible: !modelData.synced }
                        Tag { label: modelData.source }
                        Item {
                            width: 20; height: 20
                            Icon {
                                anchors.centerIn: parent
                                name: "check"
                                size: 16
                                tint: Theme.accent
                                opacity: row.inUse ? 1 : 0
                                scale: row.inUse ? 1 : 0.4
                                Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                                Behavior on scale { ScaleAnimator { duration: 360; easing.type: Easing.OutBack } }
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 6; height: 6; radius: 3
                                color: Theme.accent
                                visible: row.loading
                                SequentialAnimation on opacity {
                                    running: row.loading
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 0.3; to: 1; duration: 420; easing.type: Easing.InOutSine }
                                    NumberAnimation { from: 1; to: 0.3; duration: 420; easing.type: Easing.InOutSine }
                                }
                            }
                        }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panelRoot.pick(index)
                    }
                }
            }
        }
    }
}
