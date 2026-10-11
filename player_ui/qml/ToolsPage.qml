import QtQuick
import QtQuick.Dialogs
import PlayerUi

// 解密工具页:三个解密内核作为工具摆在顶部 (状态一眼可见);
// 中间是投放区:拖入 / 选择文件或文件夹即开始解密,进行中变成进度环;
// 下方:产物目录 (打开 / 更换 / 加入曲库)、曲库中待解密的文件、扫描时的处理方式、本次结果
Item {
    id: page
    property int bottomInset: 140
    readonly property int sidePadding: width > 1400 ? 56 : 40
    readonly property real contentW: Math.min(980, width - sidePadding * 2)
    readonly property var pend: decrypt.pending

    FileDialog {
        id: filePicker
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("加密音频 (*.kgm *.kgma *.vpr *.ncm *.kgg)")]
        onAccepted: page.feed(selectedFiles)
    }
    FolderDialog {
        id: folderPicker
        onAccepted: page.feed([selectedFolder])
    }
    FolderDialog {
        id: outPicker
        onAccepted: decrypt.setOutputUrl(selectedFolder)
    }
    function feed(urls) {
        const list = []
        for (let i = 0; i < urls.length; ++i) list.push(urls[i])
        if (decrypt.decryptUrls(list) === 0) Nav.toast("lock", qsTr("没有找到可解密的文件"))
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.height + page.bottomInset + 40
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        SmoothScroll { flickable: flick }

        Column {
            id: col
            x: (page.width - page.contentW) / 2
            y: 60
            width: page.contentW
            spacing: 0

            // ---------------- 工具卡 ----------------
            Row {
                id: cards
                width: parent.width
                spacing: 16
                Repeater {
                    model: decrypt.decoders
                    delegate: Rectangle {
                        id: tool
                        width: (cards.width - cards.spacing * 2) / 3
                        height: 150
                        radius: 18
                        color: tm.containsMouse ? Theme.pressed : Theme.hover
                        border.width: 1
                        border.color: modelData.available ? Theme.hairline : Theme.warnAlpha(0.3)
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                        // 依次浮现
                        opacity: 0
                        transform: Translate { id: rise; y: 14 }
                        Component.onCompleted: appear.start()
                        SequentialAnimation {
                            id: appear
                            PauseAnimation { duration: 60 + index * 70 }
                            ParallelAnimation {
                                OpacityAnimator { target: tool; to: 1; duration: 380; easing.type: Easing.OutCubic }
                                NumberAnimation { target: rise; property: "y"; to: 0; duration: 480; easing.type: Easing.OutQuint }
                            }
                        }

                        Text {
                            x: 22; y: 18
                            text: modelData.id.toUpperCase()
                            color: modelData.available ? Theme.accent : Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: 30
                            font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }
                        // 原生内核 / 外部程序
                        Icon {
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            y: 24
                            name: modelData.native ? "cpu" : "external"
                            size: 18
                            opacity: 0.45
                        }
                        Text {
                            x: 22; y: 62
                            text: modelData.name
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                        }
                        Row {
                            x: 22
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 20
                            spacing: 6
                            Repeater {
                                model: modelData.exts
                                QualityBadge { text: modelData; premium: false }
                            }
                        }
                        // 状态:可用为绿点;不可用为琥珀点 + 原因
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 22
                            spacing: 6
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !modelData.available
                                text: modelData.reason
                                color: Theme.warn
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 7; height: 7; radius: 3.5
                                color: modelData.available ? Theme.ok : Theme.warn
                            }
                        }
                        MouseArea { id: tm; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    }
                }
            }

            Item { width: 1; height: 22 }

            // ---------------- 投放区 ----------------
            Rectangle {
                id: zone
                width: parent.width
                height: 220
                radius: 22
                color: dropHover ? Theme.accentAlpha(0.08) : "transparent"
                border.width: 1.5
                border.color: dropHover || zm.containsMouse ? Theme.accentAlpha(0.6) : Theme.hairline
                Behavior on color { ColorAnimation { duration: Theme.normal } }
                readonly property bool dropHover: drop.containsDrag

                DropArea {
                    id: drop
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (d) => { if (d.hasUrls) page.feed(d.urls); d.acceptProposedAction() }
                }
                MouseArea { id: zm; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }

                // 空闲:锁 + 两个入口 (文件 / 文件夹)
                Column {
                    anchors.centerIn: parent
                    spacing: 20
                    opacity: decrypt.busy ? 0 : 1
                    visible: opacity > 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: zone.dropHover ? "unlock" : "lock"
                        size: 40
                        tint: zone.dropHover ? Theme.accent : "transparent"
                        opacity: zone.dropHover ? 1 : 0.4
                        scale: zone.dropHover ? 1.12 : 1
                        Behavior on scale { ScaleAnimator { duration: 260; easing.type: Easing.OutBack } }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 12
                        IconButton { width: 48; height: 48; outlined: true; icon: "note"; iconSize: 18; onClicked: filePicker.open() }
                        IconButton { width: 48; height: 48; outlined: true; icon: "folder-open"; iconSize: 18; onClicked: folderPicker.open() }
                    }
                }

                // 进行中:进度环 + 计数 + 当前文件;悬停环心变 × 可取消
                Item {
                    anchors.centerIn: parent
                    width: 130; height: 130
                    anchors.verticalCenterOffset: -12
                    opacity: decrypt.busy ? 1 : 0
                    visible: opacity > 0
                    scale: decrypt.busy ? 1 : 0.85
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                    Behavior on scale { ScaleAnimator { duration: 420; easing.type: Easing.OutBack } }
                    ShaderEffect {
                        anchors.fill: parent
                        property color color: Theme.accent
                        property color trackColor: Theme.accentAlpha(0.22)
                        property real side: width
                        property real arcWidth: 3
                        property real trackWidth: 1.5
                        property real start: 0
                        property real span: decrypt.progress
                        Behavior on span { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                        fragmentShader: "qrc:/shaders/ring.frag.qsb"
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !cm.containsMouse
                        text: decrypt.done + " / " + decrypt.total
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.weight: Font.Light
                    }
                    Icon { anchors.centerIn: parent; visible: cm.containsMouse; name: "close"; size: 20 }
                    MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: decrypt.cancel() }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    width: parent.width - 60
                    horizontalAlignment: Text.AlignHCenter
                    visible: decrypt.busy
                    text: decrypt.current
                    elide: Text.ElideMiddle
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
            }

            Item { width: 1; height: 16 }

            // ---------------- 产物目录 ----------------
            Rectangle {
                width: parent.width
                height: 56
                radius: 16
                color: Theme.hover
                Icon { id: outIcon; x: 18; anchors.verticalCenter: parent.verticalCenter; name: "folder-open"; size: 18; opacity: 0.55 }
                Text {
                    anchors.left: outIcon.right
                    anchors.leftMargin: 12
                    anchors.right: outButtons.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: decrypt.outputDir
                    elide: Text.ElideMiddle
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
                Row {
                    id: outButtons
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    IconButton { width: 38; height: 38; icon: "edit"; iconSize: 15; tip: qsTr("更换"); onClicked: outPicker.open() }
                    IconButton { width: 38; height: 38; icon: "external"; iconSize: 15; onClicked: decrypt.openOutputFolder() }
                    IconButton {
                        width: 38; height: 38
                        icon: "library-add"
                        iconSize: 16
                        onClicked: { decrypt.addOutputToLibrary(); Nav.toast("library-add", qsTr("已加入曲库")) }
                    }
                }
            }

            // ---------------- 曲库中待解密 ----------------
            Item { width: 1; height: (page.pend.total || 0) > 0 ? 12 : 0 }
            Rectangle {
                visible: (page.pend.total || 0) > 0
                width: parent.width
                height: visible ? 64 : 0
                radius: 16
                color: Theme.accentAlpha(0.08)
                border.width: 1
                border.color: Theme.accentAlpha(0.25)
                Icon { id: pl; x: 20; anchors.verticalCenter: parent.verticalCenter; name: "lock"; size: 18; tint: Theme.accent }
                Text {
                    id: pn
                    anchors.left: pl.right
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: (page.pend.total || 0).toLocaleString(Qt.locale(), "f", 0)
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.left: pn.right
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Repeater {
                        model: page.pend.kinds || []
                        QualityBadge { text: modelData.id.toUpperCase() + " " + modelData.count; premium: false }
                    }
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 44
                    filled: true
                    icon: "unlock"
                    iconSize: 18
                    enabled: !decrypt.busy
                    onClicked: decrypt.decryptPending()
                }
            }

            // ---------------- 扫描时的处理方式 ----------------
            Item { width: 1; height: 22 }
            Row {
                spacing: 14
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "refresh"; size: 15; opacity: 0.4 }
                Rectangle {
                    id: seg
                    width: 3 * 96 + 8
                    height: 40
                    radius: 20
                    color: Theme.hover
                    border.width: 1
                    border.color: Theme.hairline
                    readonly property var opts: [
                        { icon: "ask", label: qsTr("询问") },
                        { icon: "auto", label: qsTr("自动") },
                        { icon: "close", label: qsTr("不解密") }
                    ]
                    // 选中滑块
                    Rectangle {
                        x: 4 + decrypt.policy * 96
                        y: 4
                        width: 96; height: 32
                        radius: 16
                        color: Theme.accentAlpha(0.2)
                        Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutQuint } }
                    }
                    Row {
                        x: 4; y: 4
                        Repeater {
                            model: seg.opts
                            Item {
                                width: 96; height: 32
                                readonly property bool on: decrypt.policy === index
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: modelData.icon
                                        size: 13
                                        tint: on ? Theme.accent : "transparent"
                                        opacity: on ? 1 : 0.5
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.label
                                        color: on ? Theme.text : Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                    }
                                }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: decrypt.policy = index }
                            }
                        }
                    }
                }
            }

            // ---------------- 结果 ----------------
            Item { width: 1; height: decrypt.results.length ? 26 : 0 }
            Row {
                visible: decrypt.results.length > 0
                spacing: 16
                height: visible ? 28 : 0
                Row {
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "check"; size: 14; tint: Theme.ok }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: decrypt.okCount; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                }
                Row {
                    visible: decrypt.failCount > 0
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "close"; size: 13; tint: Theme.warn }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: decrypt.failCount; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !decrypt.busy
                    width: 28; height: 28
                    icon: "trash"
                    iconSize: 13
                    onClicked: decrypt.clearResults()
                }
            }
            Repeater {
                model: decrypt.results
                delegate: Item {
                    width: col.width
                    height: 46
                    opacity: 0
                    Component.onCompleted: fade.start()
                    OpacityAnimator on opacity { id: fade; running: false; to: 1; duration: 260 }
                    Icon {
                        id: st
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.ok ? "check" : "close"
                        size: 15
                        tint: modelData.ok ? Theme.ok : Theme.warn
                    }
                    QualityBadge {
                        id: kb
                        anchors.left: st.right
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.kind.toUpperCase()
                        premium: false
                    }
                    Text {
                        anchors.left: kb.right
                        anchors.leftMargin: 12
                        anchors.right: msg.left
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        elide: Text.ElideMiddle
                        color: modelData.ok ? Theme.text : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                    Text {
                        id: msg
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width * 0.4)
                        horizontalAlignment: Text.AlignRight
                        text: modelData.ok ? modelData.output.replace(/^.*[\\/]/, "") : modelData.message
                        elide: Text.ElideMiddle
                        color: modelData.ok ? Theme.textTertiary : Theme.warn
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline; opacity: 0.6 }
                }
            }
        }
    }
}
