import QtQuick
import QtQuick.Dialogs
import PlayerUi

// 解密页 (胶囊嵌套):
//  ┌ 顶栏长胶囊:锁 · 标题 · 数量胶囊 · 处理方式胶囊 · 产物目录胶囊 ……… 导入胶囊 (文件 | 文件夹) · 搜索胶囊 ┐
//  # 全部  # KGM  # NCM  # KGG  # 失败  # 已完成                                   [ 解密全部 / 进度 ]
//  ┌ 列表面板:格式 · 歌名 歌手 ……… 大小 · 来源 · 行内操作胶囊 (解密 / 进行中 / 完成 / 失败重试) ┐
// 曲库扫描时选择「不解密」的文件、手动导入还没处理的文件,都在这个列表里待命
Item {
    id: page
    property int bottomInset: 140
    readonly property int pad: width > 1400 ? 56 : 40
    readonly property real contentW: Math.min(1120, width - pad * 2)
    readonly property var st: decrypt.stats
    readonly property var q: decrypt.queue

    FileDialog {
        id: filePicker
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("加密音频 (*.kgm *.kgma *.vpr *.ncm *.kgg)")]
        onAccepted: page.feed(selectedFiles)
    }
    FolderDialog { id: folderPicker; onAccepted: page.feed([selectedFolder]) }
    FolderDialog { id: outPicker; onAccepted: decrypt.setOutputUrl(selectedFolder) }
    function feed(urls) {
        const list = []
        for (let i = 0; i < urls.length; ++i) list.push(urls[i])
        const n = decrypt.addUrls(list)
        Nav.toast(n > 0 ? "lock" : "check", n > 0 ? qsTr("已加入 %1 个").arg(n) : qsTr("没有新的可解密文件"))
    }

    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (d) => { if (d.hasUrls) page.feed(d.urls); d.acceptProposedAction() }
    }

    // 通用胶囊:浅底 + 细描边,可选强调态
    component Capsule: Rectangle {
        property bool accent: false
        property bool hot: false
        height: 34
        radius: height / 2
        color: accent ? Theme.accentAlpha(hot ? 0.24 : 0.16) : (hot ? Theme.pressed : Theme.hover)
        border.width: 1
        border.color: accent ? Theme.accentAlpha(0.45) : Theme.hairline
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    Column {
        x: (page.width - page.contentW) / 2
        y: 52
        width: page.contentW
        spacing: 14

        // ======================= 顶栏长胶囊 =======================
        Rectangle {
            id: bar
            width: parent.width
            height: 60
            radius: 30
            color: Qt.rgba(1, 0.96, 0.9, 0.035)
            border.width: 1
            border.color: drop.containsDrag ? Theme.accentAlpha(0.7) : Theme.hairline
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }

            Row {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40; radius: 20
                    color: Theme.accentAlpha(0.16)
                    Icon { anchors.centerIn: parent; name: drop.containsDrag ? "unlock" : "lock"; size: 18; tint: Theme.accent }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("解密")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    rightPadding: 4
                }
                // 数量
                Capsule {
                    anchors.verticalCenter: parent.verticalCenter
                    width: cntRow.implicitWidth + 24
                    Row {
                        id: cntRow
                        anchors.centerIn: parent
                        spacing: 6
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "lock"; size: 13; tint: Theme.accent }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: (page.st.waiting || 0).toLocaleString(Qt.locale(), "f", 0)
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                }
                // 扫描时的处理方式 (下拉)
                Capsule {
                    id: policyCap
                    anchors.verticalCenter: parent.verticalCenter
                    width: polRow.implicitWidth + 26
                    hot: polMouse.containsMouse || policyMenu.visible
                    readonly property var labels: [qsTr("询问"), qsTr("自动"), qsTr("不解密")]
                    readonly property var icons: ["ask", "auto", "close"]
                    Row {
                        id: polRow
                        anchors.centerIn: parent
                        spacing: 6
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: policyCap.icons[decrypt.policy]; size: 13; opacity: 0.7 }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: policyCap.labels[decrypt.policy]
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevron"; size: 11; rotation: 90; opacity: 0.5 }
                    }
                    MouseArea { id: polMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: policyMenu.visible = !policyMenu.visible }
                }
                // 产物目录
                Capsule {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(260, outRow.implicitWidth + 26)
                    hot: outMouse.containsMouse
                    Row {
                        id: outRow
                        anchors.centerIn: parent
                        spacing: 6
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "folder-open"; size: 13; opacity: 0.7 }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, 200)
                            text: decrypt.outputDir.replace(/^.*[\\/]/, "")
                            elide: Text.ElideMiddle
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                    }
                    MouseArea { id: outMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: outPicker.open() }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                // 导入:外胶囊里嵌套两个圆钮 (文件 | 文件夹)
                Capsule {
                    anchors.verticalCenter: parent.verticalCenter
                    height: 40
                    width: impRow.implicitWidth + 10
                    accent: true
                    Row {
                        id: impRow
                        anchors.centerIn: parent
                        spacing: 2
                        IconButton { width: 32; height: 32; icon: "plus"; iconSize: 15; iconTint: Theme.accent; onClicked: filePicker.open() }
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 1; height: 16; color: Theme.accentAlpha(0.35) }
                        IconButton { width: 32; height: 32; icon: "folder-open"; iconSize: 15; iconTint: Theme.accent; onClicked: folderPicker.open() }
                    }
                }
                // 搜索
                Capsule {
                    anchors.verticalCenter: parent.verticalCenter
                    height: 40
                    width: search.activeFocus || search.text.length ? 240 : 180
                    Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                    border.color: search.activeFocus ? Theme.accentAlpha(0.6) : Theme.hairline
                    Icon { id: sIcon; x: 14; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 14; opacity: 0.5 }
                    TextInput {
                        id: search
                        anchors.left: sIcon.right
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.text
                        selectionColor: Theme.accentAlpha(0.4)
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        clip: true
                        onTextChanged: page.q.filterText = text
                        Keys.onEscapePressed: { text = ""; focus = false }
                    }
                }
            }
        }

        // ======================= 标签胶囊 + 主操作 =======================
        Item {
            width: parent.width
            height: 34
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Repeater {
                    model: [
                        { key: "", label: qsTr("全部"), n: page.st.all || 0 },
                        { key: "kgm", label: "KGM", n: page.st.kgm || 0 },
                        { key: "ncm", label: "NCM", n: page.st.ncm || 0 },
                        { key: "kgg", label: "KGG", n: page.st.kgg || 0 },
                        { key: "failed", label: qsTr("失败"), n: page.st.failed || 0 },
                        { key: "done", label: qsTr("已完成"), n: page.st.done || 0 }
                    ]
                    Capsule {
                        readonly property bool on: page.q.filterKind === modelData.key
                        visible: modelData.key === "" || modelData.n > 0
                        width: visible ? chipRow.implicitWidth + 24 : 0
                        height: 32
                        accent: on
                        hot: chipMouse.containsMouse
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "# " + modelData.label
                                color: on ? Theme.accent : Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.n
                                color: on ? Theme.accent : Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                            }
                        }
                        MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.q.filterKind = modelData.key }
                    }
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                // 清除已完成
                Capsule {
                    visible: (page.st.done || 0) > 0 && !decrypt.busy
                    width: 34
                    height: 32
                    hot: clrMouse.containsMouse
                    Icon { anchors.centerIn: parent; name: "trash"; size: 14; opacity: 0.6 }
                    MouseArea { id: clrMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: decrypt.clearFinished() }
                }
                // 主操作:空闲 = 解密当前可见的全部;进行中 = 进度胶囊 (悬停可取消)
                Rectangle {
                    id: mainAct
                    height: 32
                    radius: 16
                    width: decrypt.busy ? 150 : goRow.implicitWidth + 30
                    Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                    color: decrypt.busy ? Theme.hover : Theme.accent
                    border.width: decrypt.busy ? 1 : 0
                    border.color: Theme.hairline
                    clip: true
                    visible: decrypt.busy || page.q.count > 0
                    opacity: decrypt.busy || (page.st.waiting || 0) + (page.st.failed || 0) > 0 ? 1 : 0.4
                    // 进度填充
                    Rectangle {
                        visible: decrypt.busy
                        height: parent.height
                        radius: parent.radius
                        width: parent.width * decrypt.progress
                        color: Theme.accentAlpha(0.28)
                        Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                    }
                    Row {
                        id: goRow
                        anchors.centerIn: parent
                        spacing: 8
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: decrypt.busy ? (actMouse.containsMouse ? "close" : "lock") : (Theme.accentIsLight ? "unlock-dark" : "unlock")
                            size: 14
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: decrypt.busy ? decrypt.done + " / " + decrypt.total : qsTr("解密")
                            color: decrypt.busy ? Theme.text : Theme.onAccent
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                    MouseArea {
                        id: actMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: decrypt.busy ? decrypt.cancel() : decrypt.decryptQueue(true)
                    }
                }
            }
        }

        // ======================= 列表面板 =======================
        Rectangle {
            id: panel
            width: parent.width
            height: Math.max(200, page.height - y - 52 - page.bottomInset + 20)
            radius: 22
            color: Qt.rgba(1, 0.96, 0.9, 0.025)
            border.width: 1
            border.color: Theme.hairline
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 8
                model: page.q
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 400
                SmoothScroll { flickable: list }
                delegate: Item {
                    id: row
                    width: list.width
                    height: 56
                    readonly property bool hovered: rm.containsMouse || rmBtn.hovered || srcBtn.hovered || actM.containsMouse
                    Rectangle {
                        anchors.fill: parent
                        radius: 14
                        color: Theme.hover
                        opacity: row.hovered ? 1 : 0
                        Behavior on opacity { OpacityAnimator { duration: 120 } }
                    }
                    MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }

                    // 格式代号 (等宽,像时间列)
                    Text {
                        x: 18
                        width: 46
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.kind.toUpperCase()
                        color: Theme.textTertiary
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                    Row {
                        x: 74
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 74 - right.width - 20
                        spacing: 10
                        Text {
                            id: titleText
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width * 0.65)
                            text: model.title
                            elide: Text.ElideRight
                            color: model.status === 2 ? Theme.textSecondary : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width - titleText.width - 10)
                            text: model.artist
                            elide: Text.ElideRight
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                    }

                    Row {
                        id: right
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        IconButton {
                            id: rmBtn
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28; height: 28
                            icon: "close"
                            iconSize: 11
                            opacity: row.hovered && model.status !== 1 ? 1 : 0
                            onClicked: decrypt.removeFromQueue(model.path)
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 64
                            horizontalAlignment: Text.AlignRight
                            text: Theme.formatBytes(model.size)
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                        }
                        // 来源:曲库扫描 / 手动导入;点击打开源文件所在文件夹
                        Rectangle {
                            id: srcBtn
                            readonly property bool hovered: srcM.containsMouse
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28; height: 28; radius: 8
                            color: hovered ? Theme.pressed : Theme.hover
                            Icon { anchors.centerIn: parent; name: model.source === "library" ? "albums" : "plus"; size: 13; opacity: 0.6 }
                            MouseArea { id: srcM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: decrypt.openOutput(model.path) }
                        }
                        // 行内操作胶囊:待命 → 解密;进行中;完成 → 打开产物位置;失败 → 原因 + 重试
                        Rectangle {
                            id: act
                            anchors.verticalCenter: parent.verticalCenter
                            height: 30
                            radius: 9
                            width: Math.max(96, actRow.implicitWidth + 22)
                            readonly property bool failed: model.status === 3
                            readonly property bool done: model.status === 2
                            color: failed ? Theme.warnAlpha(actM.containsMouse ? 0.16 : 0.08)
                                          : done ? Theme.okAlpha(0.08) : Theme.accentAlpha(actM.containsMouse ? 0.18 : 0.08)
                            border.width: 1
                            border.color: failed ? Theme.warnAlpha(0.5) : done ? Theme.okAlpha(0.4) : Theme.accentAlpha(0.5)
                            Behavior on color { ColorAnimation { duration: Theme.fast } }
                            Row {
                                id: actRow
                                anchors.centerIn: parent
                                spacing: 6
                                Item {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 14; height: 14
                                    ShaderEffect {
                                        anchors.fill: parent
                                        visible: model.status === 1
                                        property color color: Theme.accent
                                        property color trackColor: Theme.accentAlpha(0.2)
                                        property real side: width
                                        property real arcWidth: 2
                                        property real trackWidth: 1
                                        property real start: 0
                                        property real span: 0.25
                                        fragmentShader: "qrc:/shaders/ring.frag.qsb"
                                        NumberAnimation on start { running: model.status === 1; from: 0; to: 1; duration: 900; loops: Animation.Infinite }
                                    }
                                    Icon {
                                        anchors.centerIn: parent
                                        visible: model.status !== 1
                                        name: model.status === 0 ? "unlock" : act.done ? "check" : (actM.containsMouse ? "refresh" : "close")
                                        size: 13
                                        tint: act.failed ? Theme.warn : act.done ? Theme.ok : Theme.accent
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, 180)
                                    text: model.status === 0 ? qsTr("解密")
                                        : model.status === 1 ? qsTr("解密中")
                                        : act.done ? model.output.replace(/^.*\./, "").toUpperCase()
                                        : model.message
                                    elide: Text.ElideRight
                                    color: act.failed ? Theme.warn : act.done ? Theme.ok : Theme.accent
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: act.done
                                    name: "external"
                                    size: 11
                                    tint: Theme.ok
                                }
                            }
                            MouseArea {
                                id: actM
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: model.status === 1 ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: {
                                    if (model.status === 2) decrypt.openOutput(model.output)
                                    else if (model.status !== 1) decrypt.decryptPaths([model.path])
                                }
                            }
                        }
                    }
                }
            }

            // 空列表:淡淡一把锁,点按或拖入导入
            Column {
                anchors.centerIn: parent
                spacing: 14
                visible: page.q.count === 0
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "lock"; size: 38; opacity: drop.containsDrag ? 0.6 : 0.16 }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: search.text.length === 0 && page.q.filterKind === ""
                    text: qsTr("拖入或导入 KGM · NCM · KGG")
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
            }
        }
    }

    // 处理方式下拉菜单
    MouseArea {
        anchors.fill: parent
        visible: policyMenu.visible
        onClicked: policyMenu.visible = false
    }
    Rectangle {
        id: policyMenu
        visible: false
        readonly property point at: policyCap.mapToItem(page, 0, policyCap.height + 8)
        x: at.x
        y: at.y
        width: 168
        height: pmCol.implicitHeight + 12
        radius: 14
        color: Theme.panel
        border.width: 1
        border.color: Qt.rgba(1, 0.96, 0.9, 0.09)
        Column {
            id: pmCol
            x: 6; y: 6
            width: parent.width - 12
            Repeater {
                model: [{ i: 0, icon: "ask", t: qsTr("每次询问") }, { i: 1, icon: "auto", t: qsTr("自动解密") }, { i: 2, icon: "close", t: qsTr("不解密,放入列表") }]
                Rectangle {
                    width: pmCol.width
                    height: 36
                    radius: 9
                    color: pmm.containsMouse ? Theme.hover : "transparent"
                    Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; name: modelData.icon; size: 14; tint: decrypt.policy === modelData.i ? Theme.accent : "transparent"; opacity: 0.85 }
                    Text {
                        x: 34
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.t
                        color: decrypt.policy === modelData.i ? Theme.accent : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                    MouseArea { id: pmm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { decrypt.policy = modelData.i; policyMenu.visible = false } }
                }
            }
        }
    }
}
