import QtQuick
import QtQuick.Window
import PlayerUi

// 桌面歌词:置顶、透明、无边框的独立窗口。
//  · 双行交替 (酷狗式:上一行靠左、下一行靠右),当前句逐字高亮;有翻译时下一行显示翻译
//  · 悬停显示半透明底板与工具条:上一首 / 播放 / 下一首 · 字号 · 配色 · 单双行 · 锁定 · 关闭
//  · 锁定后鼠标穿透 (不挡桌面操作);鼠标移到歌词上时只露出一个「解锁」小按钮,只有它能点
//  · 拖动任意空白处移动,位置记住
Window {
    id: dl
    readonly property bool locked: settings.lyricsLocked
    readonly property int fs: settings.lyricsFontSize
    // 锁定时鼠标是否悬停在窗口 / 解锁按钮上 (窗口不接收鼠标,由定时器读全局光标位置)
    property bool hoverLocked: false
    property bool overUnlock: false
    readonly property bool hovered: locked ? hoverLocked : hoverArea.containsMouse || dragging
    property bool dragging: false

    // 配色:已唱部分的颜色;0 跟随封面强调色
    readonly property var presets: [
        { name: qsTr("跟随封面"), c: Theme.accent },
        { name: qsTr("冰蓝"), c: "#8fd8ff" },
        { name: qsTr("樱粉"), c: "#ff9cbc" },
        { name: qsTr("薄荷"), c: "#7ee8be" },
        { name: qsTr("柠檬"), c: "#ffe27a" }
    ]
    readonly property color sungColor: presets[Math.max(0, Math.min(presets.length - 1, settings.lyricsColor))].c

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
           | (locked && !overUnlock ? Qt.WindowTransparentForInput : 0)
    color: "transparent"
    // 不挂在主窗口下:主窗口最小化时桌面歌词照常显示
    transientParent: null
    title: qsTr("桌面歌词")
    width: Math.min(Screen.width - 40, Math.max(640, fs * 26))
    height: toolbarH + fs * (settings.lyricsTwoLine ? 2.75 : 1.6) + 28
    readonly property int toolbarH: 44

    function placeDefault() {
        const p = settings.lyricsPos
        // 记住的位置还在某块屏幕上就用它 (换了显示器则回到默认)
        const scr = Qt.application.screens
        for (let i = 0; i < scr.length; ++i) {
            const s = scr[i]
            if (p.x >= s.virtualX && p.x < s.virtualX + s.width - 60 && p.y >= s.virtualY && p.y < s.virtualY + s.height - 40) {
                x = p.x; y = p.y
                return
            }
        }
        x = Screen.virtualX + (Screen.width - width) / 2
        y = Screen.virtualY + Screen.height - height - 120
    }
    onVisibleChanged: if (visible) placeDefault()
    Component.onCompleted: placeDefault()

    // ---------- 歌词数据 ----------
    readonly property var lines: player.lyrics
    readonly property bool synced: player.lyricsSynced && lines.length > 0
    readonly property int cur: synced ? Math.max(0, player.lyricIndex) : -1
    readonly property var curLine: cur >= 0 && cur < lines.length ? lines[cur] : null
    readonly property bool hasTrans: curLine !== null && curLine.translation !== undefined && curLine.translation.length > 0
    readonly property string fallback: player.hasTrack ? player.title + (player.artist ? "  —  " + player.artist : "") : "Hi-Fi Player"
    function lineText(i) { return i >= 0 && i < lines.length ? lines[i].text : "" }
    // 当前句演唱进度 0..1 (player.position 约 60Hz 更新,足够平滑)
    function progressOf(i) {
        if (!synced || i !== cur || !curLine) return i < cur ? 1 : 0
        if (player.lyricIndex < 0) return 0
        const d = curLine.duration > 0 ? curLine.duration : 3
        return Math.max(0, Math.min(1, (player.position - curLine.time) / d))
    }
    // 双行交替:偶数句在上 (靠左),奇数句在下 (靠右);另一行预告下一句
    readonly property int topIdx: cur < 0 ? -1 : (cur % 2 === 0 ? cur : cur + 1)
    readonly property int botIdx: cur < 0 ? -1 : (cur % 2 === 1 ? cur : cur + 1)

    // ---------- 锁定状态下的悬停检测 ----------
    Timer {
        interval: 120
        repeat: true
        running: dl.visible && dl.locked
        onTriggered: {
            const p = windowHelper.cursorPos()
            const inWin = p.x >= dl.x && p.x < dl.x + dl.width && p.y >= dl.y && p.y < dl.y + dl.height
            dl.hoverLocked = inWin
            const b = unlockBtn.mapToGlobal(0, 0)
            dl.overUnlock = inWin && p.x >= b.x - 4 && p.x < b.x + unlockBtn.width + 4 && p.y >= b.y - 4 && p.y < b.y + unlockBtn.height + 4
        }
        onRunningChanged: if (!running) { dl.hoverLocked = false; dl.overUnlock = false }
    }

    // ---------- 底板 (悬停且未锁定时) ----------
    Rectangle {
        anchors.fill: parent
        radius: 18
        color: Qt.rgba(0.06, 0.055, 0.05, 0.62)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        opacity: dl.hovered && !dl.locked ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPressed: { dl.dragging = true; dl.startSystemMove() }
        onReleased: dl.dragging = false
        onCanceled: dl.dragging = false
    }
    // 系统拖动结束后不一定收到 released:位置一变就记下来
    onXChanged: savePos.restart()
    onYChanged: savePos.restart()
    Timer { id: savePos; interval: 400; onTriggered: { if (dl.visible && dl.dragging) settings.lyricsPos = Qt.point(Math.round(dl.x), Math.round(dl.y)); dl.dragging = false } }

    // ---------- 工具条 ----------
    component Tool: Rectangle {
        id: tool
        property string icon
        property string label
        property bool on: false
        signal clicked()
        width: label.length ? lab.implicitWidth + 20 : 32
        height: 32
        radius: 16
        color: tm.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : on ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Icon { visible: tool.icon.length > 0; anchors.centerIn: parent; name: tool.icon; size: 15 }
        Text {
            id: lab
            visible: tool.label.length > 0
            anchors.centerIn: parent
            text: tool.label
            color: "#f3efe9"
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        MouseArea { id: tm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: tool.clicked() }
    }

    Row {
        id: toolbar
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        height: dl.toolbarH - 12
        spacing: 4
        opacity: dl.hovered && !dl.locked ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Tool { icon: "prev"; onClicked: player.previous() }
        Tool { icon: player.playing ? "pause" : "play"; onClicked: player.toggle() }
        Tool { icon: "next"; onClicked: player.next() }
        Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Qt.rgba(1, 1, 1, 0.14) }
        Tool { label: "A-"; onClicked: settings.lyricsFontSize = dl.fs - 2 }
        Tool { label: "A+"; onClicked: settings.lyricsFontSize = dl.fs + 2 }
        // 配色小圆点
        Repeater {
            model: dl.presets
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 32; height: 32; radius: 16
                color: cm.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: settings.lyricsColor === index ? 16 : 12
                    height: width
                    radius: width / 2
                    color: modelData.c
                    border.width: settings.lyricsColor === index ? 2 : 0
                    border.color: "#ffffff"
                    Behavior on width { NumberAnimation { duration: 120 } }
                }
                MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: settings.lyricsColor = index }
            }
        }
        Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Qt.rgba(1, 1, 1, 0.14) }
        Tool { label: settings.lyricsTwoLine ? qsTr("双行") : qsTr("单行"); onClicked: settings.lyricsTwoLine = !settings.lyricsTwoLine }
        Tool { icon: "lock"; onClicked: settings.lyricsLocked = true }
        Tool { icon: "close"; onClicked: settings.desktopLyrics = false }
    }

    // ---------- 歌词 ----------
    // 一行:底层未唱文字 + 按进度裁剪的已唱文字;深色描边,任何桌面背景上都看得清
    component LyricLine: Item {
        id: ll
        property string text
        property real progress: 0
        property real size: dl.fs
        property bool active: false
        property int align: Text.AlignHCenter
        height: Math.ceil(size * 1.35)
        readonly property real textW: Math.min(width, base.implicitWidth)
        readonly property real textX: align === Text.AlignLeft ? 0 : align === Text.AlignRight ? width - textW : (width - textW) / 2
        // 柔和投影:浅色桌面上也能看清白字
        Text {
            x: ll.textX + 1
            y: 2
            width: ll.textW
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: ll.text
            elide: Text.ElideRight
            color: Qt.rgba(0, 0, 0, 0.32)
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.22)
            font: base.font
        }
        Text {
            id: base
            x: ll.textX
            width: ll.textW
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: ll.text
            elide: Text.ElideRight
            color: ll.active ? "#f6f2ec" : Qt.rgba(0.96, 0.95, 0.93, 0.78)
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.7)
            font.family: Theme.fontFamily
            font.pixelSize: ll.size
            font.weight: Font.DemiBold
        }
        Item {
            x: ll.textX
            width: ll.textW * ll.progress
            height: parent.height
            clip: true
            visible: ll.active && ll.progress > 0
            Text {
                width: ll.textW
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: ll.text
                elide: Text.ElideRight
                color: dl.sungColor
                style: Text.Outline
                styleColor: Qt.rgba(0, 0, 0, 0.7)
                font: base.font
            }
        }
    }

    Column {
        id: lyricCol
        x: 28
        width: parent.width - 56
        anchors.top: parent.top
        anchors.topMargin: dl.toolbarH
        spacing: Math.round(dl.fs * 0.12)

        // 没有可同步的歌词:显示歌名
        LyricLine {
            visible: !dl.synced
            width: parent.width
            text: dl.fallback
            active: true
        }
        // 单行,或当前句有翻译:当前句居中,翻译在下
        LyricLine {
            visible: dl.synced && (!settings.lyricsTwoLine || dl.hasTrans)
            width: parent.width
            text: dl.lineText(dl.cur)
            active: true
            progress: dl.progressOf(dl.cur)
        }
        LyricLine {
            visible: dl.synced && settings.lyricsTwoLine && dl.hasTrans
            width: parent.width
            size: Math.round(dl.fs * 0.7)
            text: dl.hasTrans ? dl.curLine.translation : ""
        }
        // 双行交替
        LyricLine {
            visible: dl.synced && settings.lyricsTwoLine && !dl.hasTrans
            width: parent.width
            align: Text.AlignLeft
            text: dl.lineText(dl.topIdx)
            active: dl.topIdx === dl.cur
            progress: dl.progressOf(dl.topIdx)
        }
        LyricLine {
            visible: dl.synced && settings.lyricsTwoLine && !dl.hasTrans
            width: parent.width
            align: Text.AlignRight
            text: dl.lineText(dl.botIdx)
            active: dl.botIdx === dl.cur
            progress: dl.progressOf(dl.botIdx)
        }
    }

    // ---------- 锁定后的解锁按钮 ----------
    Rectangle {
        id: unlockBtn
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        width: unlockRow.implicitWidth + 24
        height: 30
        radius: 15
        color: dl.overUnlock ? Qt.rgba(0.06, 0.055, 0.05, 0.85) : Qt.rgba(0.06, 0.055, 0.05, 0.6)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        opacity: dl.locked && dl.hoverLocked ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Row {
            id: unlockRow
            anchors.centerIn: parent
            spacing: 6
            Icon { anchors.verticalCenter: parent.verticalCenter; name: "unlock"; size: 13 }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("解锁")
                color: "#f3efe9"
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: dl.locked
            cursorShape: Qt.PointingHandCursor
            onClicked: { settings.lyricsLocked = false; dl.overUnlock = false }
        }
    }
}
