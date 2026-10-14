import QtQuick
import QtQuick.Dialogs
import PlayerUi

// 设置:顶栏长胶囊 + 若干圆角面板;每行「名称 + 一排选项胶囊」,选中项的强调色底板在行内滑动
// 改动即时生效,无「保存」;需要重扫 / 重启才生效的在行尾用浅灰小字说明
Item {
    id: page
    property int bottomInset: 140
    readonly property int pad: width > 1400 ? 56 : 40
    readonly property real contentW: Math.min(1040, width - pad * 2)

    FolderDialog { id: addFolder; onAccepted: library.addFolderUrls([selectedFolder]) }
    FolderDialog { id: outPicker; onAccepted: decrypt.setOutputUrl(selectedFolder) }

    // ---------------- 行:名称 + 选项胶囊 ----------------
    // 同一行的选项等宽 (取最宽文字,且不小于 88),选中项的强调色底板按格位滑动;
    // 未选中为极淡的暖色底、无描边,与面板融为一体
    FontMetrics { id: optFont; font.family: Theme.fontFamily; font.pixelSize: 13; font.weight: Font.DemiBold }
    readonly property int optH: 32
    readonly property int optGap: 6
    // 识别到、还没加入的酷狗下载目录 (加入 / 忽略后刷新)
    property int kgTick: 0
    readonly property var kgCands: { library.folders; kgTick; return library.kugouCandidates() }

    component OptionRow: Item {
        id: orow
        property string label
        property var options: []          // [{ t: 文本, v: 值 }]
        property var value
        property string hint
        signal picked(var v)
        width: parent ? parent.width : 0
        height: 54

        readonly property int sel: {
            for (let i = 0; i < options.length; ++i) if (options[i].v === value) return i
            return -1
        }
        readonly property real cellW: {
            let w = 0
            for (let i = 0; i < options.length; ++i) w = Math.max(w, optFont.advanceWidth(options[i].t))
            return Math.max(88, Math.ceil(w) + 32)
        }
        Text {
            x: 22
            anchors.verticalCenter: parent.verticalCenter
            text: orow.label
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Item {
            x: 140
            anchors.verticalCenter: parent.verticalCenter
            width: orow.options.length * (orow.cellW + page.optGap) - page.optGap
            height: page.optH
            Row {
                spacing: page.optGap
                Repeater {
                    model: orow.options
                    Rectangle {
                        width: orow.cellW
                        height: page.optH
                        radius: height / 2
                        color: Qt.rgba(1, 0.96, 0.9, m.containsMouse && index !== orow.sel ? 0.09 : 0.05)
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                        MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: orow.picked(modelData.v) }
                    }
                }
            }
            // 选中底板:按格位滑动
            Rectangle {
                visible: orow.sel >= 0
                x: Math.max(0, orow.sel) * (orow.cellW + page.optGap)
                width: orow.cellW
                height: page.optH
                radius: height / 2
                color: Theme.accent
                Behavior on x { NumberAnimation { duration: 340; easing.type: Easing.OutQuint } }
            }
            // 文字在最上层:选中为深色、其余为柔和灰
            Row {
                spacing: page.optGap
                Repeater {
                    model: orow.options
                    Text {
                        width: orow.cellW
                        height: page.optH
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.t
                        color: index === orow.sel ? Theme.onAccent : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.weight: index === orow.sel ? Font.DemiBold : Font.Normal
                        Behavior on color { ColorAnimation { duration: Theme.normal } }
                    }
                }
            }
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            text: orow.hint
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }

    // 行:名称 + 任意内容
    component CustomRow: Item {
        property string label
        default property alias content: slot.data
        width: parent ? parent.width : 0
        height: 54
        Text {
            x: 22
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Item {
            id: slot
            x: 140
            width: parent.width - 140 - 22
            height: parent.height
        }
    }

    // 面板:圆角容器,行之间细分隔线;进场时依次浮现
    component Panel: Rectangle {
        id: pnl
        property int order: 0
        default property alias rows: col.data
        width: page.contentW
        height: col.implicitHeight + 8
        radius: 22
        color: Qt.rgba(1, 0.96, 0.9, 0.03)
        border.width: 1
        border.color: Theme.hairline
        opacity: 0
        transform: Translate { id: lift; y: 14 }
        Component.onCompleted: intro.start()
        SequentialAnimation {
            id: intro
            PauseAnimation { duration: 50 + pnl.order * 60 }
            ParallelAnimation {
                OpacityAnimator { target: pnl; to: 1; duration: 380; easing.type: Easing.OutCubic }
                NumberAnimation { target: lift; property: "y"; to: 0; duration: 520; easing.type: Easing.OutQuint }
            }
        }
        Column {
            id: col
            y: 4
            width: parent.width
        }
        // 行分隔线
        Repeater {
            model: Math.max(0, col.children.length - 1)
            Rectangle {
                readonly property Item above: col.children[index]
                x: 22
                y: above ? col.y + above.y + above.height : 0
                width: pnl.width - 44
                height: 1
                color: Theme.hairline
                visible: above && above.visible && above.height > 0
            }
        }
    }

    // 文字小胶囊按钮
    component Pill: Capsule { height: 30 }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: content.height + 52 + page.bottomInset
        boundsBehavior: Flickable.StopAtBounds
        SmoothScroll { flickable: flick }

        Column {
            id: content
            x: (page.width - page.contentW) / 2
            y: 52
            width: page.contentW
            spacing: 14

            // ======================= 顶栏 =======================
            Panel {
                order: 0
                radius: 30
                Item {
                    width: parent.width
                    height: 52
                    Row {
                        x: 22
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Hi-Fi Player"
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 16
                            font.weight: Font.Bold
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1 首  ·  %2 张专辑  ·  %3 位歌手")
                                    .arg(library.trackCount.toLocaleString(Qt.locale(), "f", 0))
                                    .arg(library.albumCount).arg(artistModel.letters.length >= 0 ? artistCountOf() : 0)
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Capsule { interactive: false; text: "v" + settings.version }
                        Capsule { text: qsTr("数据目录"); onClicked: settings.openDataDir() }
                    }
                }
            }

            // ======================= 播放 =======================
            Panel {
                order: 1
                OptionRow {
                    label: qsTr("输出模式")
                    options: [{ t: qsTr("共享"), v: false }, { t: qsTr("WASAPI 独占"), v: true }]
                    value: player.exclusive
                    hint: player.exclusive ? qsTr("绕过系统混音器") : qsTr("经过系统混音器")
                    onPicked: (v) => player.exclusive = v
                }
                CustomRow {
                    label: qsTr("输出设备")
                    Capsule {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (player.outputDevice || qsTr("系统默认")) + (devList.open ? "   ︿" : "   ﹀")
                        width: Math.min(implicitWidth, 460)
                        onClicked: { if (!devList.open) player.refreshDevices(); devList.open = !devList.open }
                    }
                }
                // 设备列表:就地展开,选中即切换 (播放不中断,位置保持)
                Item {
                    id: devList
                    property bool open: false
                    readonly property var names: [""].concat(player.outputDevices)
                    width: parent.width
                    height: open ? devCol.implicitHeight + 14 : 0
                    clip: true
                    visible: height > 0
                    Behavior on height { NumberAnimation { duration: Theme.reduced ? 0 : 320; easing.type: Easing.OutQuint } }
                    Column {
                        id: devCol
                        x: 140
                        y: 2
                        spacing: 4
                        opacity: devList.open ? 1 : 0
                        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                        Repeater {
                            model: devList.names
                            Rectangle {
                                readonly property bool cur: modelData === player.outputDevice
                                width: Math.min(460, Math.max(220, devText.implicitWidth + 36))
                                height: page.optH
                                radius: height / 2
                                color: cur ? Theme.accent : Qt.rgba(1, 0.96, 0.9, dm.containsMouse ? 0.09 : 0.05)
                                Behavior on color { ColorAnimation { duration: Theme.fast } }
                                Text {
                                    id: devText
                                    anchors.fill: parent
                                    anchors.leftMargin: 18
                                    anchors.rightMargin: 18
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    text: modelData || qsTr("系统默认")
                                    color: parent.cur ? Theme.onAccent : Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: parent.cur ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: dm
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { player.outputDevice = modelData; devList.open = false }
                                }
                            }
                        }
                    }
                }
                OptionRow {
                    label: qsTr("无缝播放")
                    options: [{ t: qsTr("开启"), v: true }, { t: qsTr("关闭"), v: false }]
                    value: player.gapless
                    hint: qsTr("曲目之间不留空隙")
                    onPicked: (v) => player.gapless = v
                }
                OptionRow {
                    label: qsTr("音量标准化")
                    options: [{ t: qsTr("关闭"), v: 0 }, { t: qsTr("按曲目"), v: 1 }, { t: qsTr("按专辑"), v: 2 }]
                    value: player.replayGain
                    hint: qsTr("读取 ReplayGain,不削波")
                    onPicked: (v) => player.replayGain = v
                }
                OptionRow {
                    label: qsTr("歌词")
                    options: [{ t: qsTr("LRC 优先"), v: 0 }, { t: qsTr("内嵌优先"), v: 1 }]
                    value: settings.lyricsSource
                    hint: qsTr("同名 .lrc 与文件内嵌歌词")
                    onPicked: (v) => { settings.lyricsSource = v; player.reloadLyrics() }
                }
                OptionRow {
                    label: qsTr("桌面歌词")
                    options: [{ t: qsTr("显示"), v: true }, { t: qsTr("关闭"), v: false }]
                    value: settings.desktopLyrics
                    hint: qsTr("置顶显示在桌面上 · Ctrl D")
                    onPicked: (v) => settings.desktopLyrics = v
                }
                OptionRow {
                    visible: settings.desktopLyrics
                    label: qsTr("歌词锁定")
                    options: [{ t: qsTr("锁定"), v: true }, { t: qsTr("可拖动"), v: false }]
                    value: settings.lyricsLocked
                    hint: qsTr("锁定后鼠标可以穿过歌词")
                    onPicked: (v) => settings.lyricsLocked = v
                }
                OptionRow {
                    label: qsTr("淡入淡出")
                    options: [{ t: qsTr("开启"), v: true }, { t: qsTr("关闭"), v: false }]
                    value: settings.fade
                    hint: qsTr("暂停与继续时")
                    onPicked: (v) => settings.fade = v
                }
                OptionRow {
                    label: qsTr("定时关闭")
                    options: [{ t: qsTr("关闭"), v: 0 }, { t: qsTr("15 分钟"), v: 15 }, { t: qsTr("30 分钟"), v: 30 },
                              { t: qsTr("60 分钟"), v: 60 }, { t: qsTr("播完本首"), v: -1 }]
                    value: player.sleepMinutes
                    hint: player.sleepMinutes > 0
                          ? qsTr("%1 后暂停").arg(Math.floor(player.sleepRemaining / 60) + ":" + String(player.sleepRemaining % 60).padStart(2, "0"))
                          : player.sleepMinutes === -1 ? qsTr("当前歌曲播完后停止") : qsTr("到点后淡出暂停")
                    onPicked: (v) => player.setSleepTimer(v)
                }
                OptionRow {
                    label: qsTr("启动时")
                    options: [{ t: qsTr("恢复上次播放"), v: true }, { t: qsTr("从头开始"), v: false }]
                    value: settings.resume
                    onPicked: (v) => settings.resume = v
                }
            }

            // ======================= 曲库 =======================
            Panel {
                order: 2
                CustomRow {
                    label: qsTr("音乐文件夹")
                    height: Math.max(54, folderFlow.implicitHeight + 22)
                    Flow {
                        id: folderFlow
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: library.folders
                            Rectangle {
                                readonly property bool kg: library.isKugouFolder(modelData)
                                height: 32
                                radius: 16
                                width: Math.min(360, fname.x + fname.width + (fm.containsMouse ? 52 : 14))
                                Behavior on width { NumberAnimation { duration: Theme.fast } }
                                color: Qt.rgba(1, 0.96, 0.9, fm.containsMouse ? 0.09 : 0.05)
                                clip: true
                                Text {
                                    id: kgTag
                                    x: 14
                                    visible: parent.kg
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("酷狗")
                                    color: Theme.accent
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    id: fname
                                    x: parent.kg ? kgTag.x + kgTag.implicitWidth + 8 : 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, 270)
                                    text: modelData
                                    elide: Text.ElideMiddle
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("移除")
                                    color: Theme.accent
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    opacity: fm.containsMouse ? 1 : 0
                                    Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                                }
                                MouseArea {
                                    id: fm
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: library.removeFolder(modelData)
                                }
                            }
                        }
                        Capsule { accent: true; text: qsTr("添加"); onClicked: addFolder.open() }
                    }
                }
                CustomRow {
                    label: qsTr("酷狗音乐")
                    visible: page.kgCands.length > 0
                    height: visible ? 54 : 0
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, 360)
                            elide: Text.ElideMiddle
                            text: qsTr("发现酷狗下载目录 %1").arg(page.kgCands.join("、"))
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                        Capsule {
                            accent: true
                            text: qsTr("加入")
                            onClicked: { library.addKugouFolders(page.kgCands); page.kgTick++ }
                        }
                        Capsule {
                            text: qsTr("忽略")
                            onClicked: { library.dismissKugouCandidates(page.kgCands); page.kgTick++ }
                        }
                    }
                }
                CustomRow {
                    label: qsTr("单独加入")
                    visible: library.extraFileCount > 0
                    height: visible ? 54 : 0
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1 首不在音乐文件夹里的歌 (拖入或从转换页加入)").arg(library.extraFileCount)
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                        Capsule { text: qsTr("全部移出曲库"); onClicked: library.clearExtraFiles() }
                    }
                }
                OptionRow {
                    label: qsTr("实时监控")
                    options: [{ t: qsTr("开启"), v: true }, { t: qsTr("关闭"), v: false }]
                    value: library.watchFolders
                    hint: qsTr("文件夹里有新歌、删除或改名时自动更新")
                    onPicked: (v) => library.watchFolders = v
                }
                OptionRow {
                    label: qsTr("扫描线程")
                    options: [{ t: qsTr("自动"), v: 0 }, { t: "4", v: 4 }, { t: "8", v: 8 }, { t: "16", v: 16 }]
                    value: settings.scanThreads
                    hint: qsTr("机械硬盘建议 4")
                    onPicked: (v) => settings.scanThreads = v
                }
                OptionRow {
                    label: qsTr("CUE 分轨")
                    options: [{ t: qsTr("开启"), v: true }, { t: qsTr("关闭"), v: false }]
                    value: settings.splitCue
                    hint: qsTr("整轨按 CUE 拆分为独立曲目")
                    onPicked: (v) => settings.splitCue = v
                }
                OptionRow {
                    label: qsTr("旧编码标签")
                    options: [{ t: qsTr("简体 GBK"), v: 936 }, { t: qsTr("繁体 Big5"), v: 950 }, { t: qsTr("日文 Shift-JIS"), v: 932 }]
                    value: settings.codePage
                    hint: qsTr("非 Unicode 标签乱码时切换")
                    onPicked: (v) => settings.codePage = v
                }
                OptionRow {
                    label: qsTr("MP3 尾部标签")
                    options: [{ t: qsTr("读取"), v: true }, { t: qsTr("忽略"), v: false }]
                    value: settings.readId3v1
                    hint: qsTr("ID3v1 / APEv2")
                    onPicked: (v) => settings.readId3v1 = v
                }
                // 解析相关设置改动后:立即重扫
                CustomRow {
                    label: qsTr("重新扫描")
                    visible: settings.needRescan
                    height: visible ? 54 : 0
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Capsule {
                            filled: true
                            text: library.scanning ? qsTr("扫描中…") : qsTr("立即重扫")
                            interactive: !library.scanning
                            onClicked: settings.rebuildLibrary()
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("以上设置改动后需要重扫才生效")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                        }
                    }
                }
            }

            // ======================= 转换 =======================
            Panel {
                order: 3
                OptionRow {
                    label: qsTr("加密歌曲")
                    options: [{ t: qsTr("询问"), v: 0 }, { t: qsTr("自动转换"), v: 1 }, { t: qsTr("不转换"), v: 2 }]
                    value: decrypt.policy
                    hint: qsTr("音乐文件夹里的 KGM / NCM / KGG")
                    onPicked: (v) => decrypt.policy = v
                }
                CustomRow {
                    label: qsTr("输出目录")
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        // 路径太长时只留开头盘符和结尾几级,避免压到左边的标题
                        Capsule {
                            readonly property string full: decrypt.outputDir
                            text: full.length > 46 ? full.slice(0, 3) + "…" + full.slice(full.length - 40) : full
                            onClicked: outPicker.open()
                        }
                        Capsule { text: qsTr("打开"); onClicked: decrypt.openOutputFolder() }
                    }
                }
                OptionRow {
                    label: qsTr("写回标签")
                    options: [{ t: qsTr("开启"), v: true }, { t: qsTr("关闭"), v: false }]
                    value: settings.writeTags
                    hint: qsTr("NCM 转换后写入歌名、歌手、封面")
                    onPicked: (v) => settings.writeTags = v
                }
                CustomRow {
                    label: qsTr("转换工具")
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: decrypt.decoders.map(d => d.id.toUpperCase() + " " + (d.available ? qsTr("就绪") : d.reason)).join("   ·   ")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                }
            }

            // ======================= 外观 =======================
            Panel {
                order: 4
                CustomRow {
                    label: qsTr("强调色")
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Capsule {
                            anchors.verticalCenter: parent.verticalCenter
                            filled: settings.accent < 0
                            text: qsTr("跟随封面")
                            onClicked: settings.accent = -1
                        }
                        Repeater {
                            model: settings.accentPresets
                            Item {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30; height: 30
                                readonly property bool on: settings.accent === index
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 15
                                    color: "transparent"
                                    border.width: 1.5
                                    border.color: modelData
                                    opacity: parent.on ? 1 : 0
                                    scale: parent.on ? 1 : 0.7
                                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                                    Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutBack } }
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 20; height: 20; radius: 10
                                    color: modelData
                                    scale: sw.containsMouse ? 1.12 : 1
                                    Behavior on scale { ScaleAnimator { duration: Theme.fast } }
                                }
                                MouseArea { id: sw; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: settings.accent = index }
                            }
                        }
                    }
                }
                OptionRow {
                    label: qsTr("背景")
                    options: [{ t: qsTr("流动"), v: 0 }, { t: qsTr("静止"), v: 1 }, { t: qsTr("纯色"), v: 2 }]
                    value: settings.background
                    hint: qsTr("静止与纯色更省电")
                    onPicked: (v) => settings.background = v
                }
                OptionRow {
                    label: qsTr("动效")
                    options: [{ t: qsTr("完整"), v: false }, { t: qsTr("减弱"), v: true }]
                    value: settings.reducedMotion
                    onPicked: (v) => settings.reducedMotion = v
                }
                OptionRow {
                    label: qsTr("界面缩放")
                    options: [{ t: "90%", v: 0.9 }, { t: "100%", v: 1.0 }, { t: "110%", v: 1.1 }, { t: "125%", v: 1.25 }]
                    value: settings.uiScale
                    hint: settings.restartNeeded ? qsTr("重启后生效") : ""
                    onPicked: (v) => settings.uiScale = v
                }
            }

            // ======================= 存储 =======================
            Panel {
                order: 5
                CustomRow {
                    label: qsTr("封面缓存")
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 80
                            text: Theme.formatBytes(settings.coverCacheBytes)
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                        Capsule { text: qsTr("清除"); onClicked: settings.clearCoverCache() }
                    }
                }
                CustomRow {
                    label: qsTr("扫描缓存")
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 80
                            text: Theme.formatBytes(settings.scanCacheBytes)
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                        Capsule {
                            text: library.scanning ? qsTr("扫描中…") : qsTr("重建")
                            interactive: !library.scanning
                            onClicked: settings.rebuildLibrary()
                        }
                    }
                }
            }

            // ======================= 快捷键 =======================
            Panel {
                order: 6
                CustomRow {
                    label: qsTr("快捷键")
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        elide: Text.ElideRight
                        text: qsTr("空格 播放 / 暂停   ·   Ctrl K 搜索   ·   Ctrl ← → 切歌   ·   Ctrl D 桌面歌词   ·   Alt ← 返回   ·   直接打字即搜索")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                }
            }
        }
    }

    function artistCountOf() { return library.generation >= 0 ? artistModel.rowCount() : 0 }
}
