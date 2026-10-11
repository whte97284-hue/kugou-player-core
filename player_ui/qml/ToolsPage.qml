import QtQuick
import QtQuick.Dialogs
import PlayerUi

// 解密:页面中央只有一个圆 (与空曲库引导同一视觉语言)
//  · 点击选择文件,拖入文件或文件夹;进行中变为进度环,悬停可取消
//  · 圆下方几行小字:支持的格式 (状态点)、产物目录、扫描时的处理方式;完成后列出结果
Item {
    id: page
    property int bottomInset: 140
    readonly property var pend: decrypt.pending
    readonly property bool hot: drop.containsDrag || circleMouse.containsMouse

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

    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (d) => { if (d.hasUrls) page.feed(d.urls); d.acceptProposedAction() }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: Math.max(height, col.y + col.height + page.bottomInset)
        boundsBehavior: Flickable.StopAtBounds
        SmoothScroll { flickable: flick }

        Column {
            id: col
            width: Math.min(560, page.width - 80)
            x: (page.width - width) / 2
            // 没有结果时整体居中,出现结果后平滑上移
            y: decrypt.results.length ? 72 : Math.max(72, (page.height - page.bottomInset - 330) / 2)
            Behavior on y { NumberAnimation { duration: 520; easing.type: Easing.OutQuint } }
            spacing: 0

            // ---------------- 圆 ----------------
            Item {
                id: dial
                anchors.horizontalCenter: parent.horizontalCenter
                width: 168; height: 168
                scale: circleMouse.pressed ? 0.97 : (page.hot && !decrypt.busy ? 1.03 : 1)
                Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: width / 2
                    color: Theme.accentAlpha(page.hot ? 0.08 : 0.035)
                    Behavior on color { ColorAnimation { duration: Theme.normal } }
                }
                ShaderEffect {
                    anchors.fill: parent
                    property color color: Theme.accent
                    property color trackColor: Theme.accentAlpha(decrypt.busy ? 0.26 : (page.hot ? 0.75 : 0.42))
                    property real side: width
                    property real arcWidth: 3
                    property real trackWidth: 1.5
                    property real start: 0
                    property real span: decrypt.busy ? Math.max(0.02, decrypt.progress) : 0
                    Behavior on span { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                    Behavior on trackColor { ColorAnimation { duration: Theme.normal } }
                    fragmentShader: "qrc:/shaders/ring.frag.qsb"
                }
                // 空闲:锁 (悬停 / 拖入时打开)
                Icon {
                    anchors.centerIn: parent
                    name: page.hot ? "unlock" : "lock"
                    size: 30
                    tint: Theme.accent
                    opacity: decrypt.busy ? 0 : 1
                    scale: decrypt.busy ? 0.6 : (page.hot ? 1.1 : 1)
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                    Behavior on scale { ScaleAnimator { duration: Theme.normal; easing.type: Easing.OutBack } }
                }
                // 进行中:计数,悬停变 ×
                Text {
                    anchors.centerIn: parent
                    text: decrypt.done + " / " + decrypt.total
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 26
                    font.weight: Font.Light
                    opacity: decrypt.busy && !circleMouse.containsMouse ? 1 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                }
                Icon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 22
                    opacity: decrypt.busy && circleMouse.containsMouse ? 0.85 : 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.fast } }
                }
                MouseArea {
                    id: circleMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: decrypt.busy ? decrypt.cancel() : filePicker.open()
                }
                // 选文件夹:圆右下角一个小入口
                IconButton {
                    x: parent.width - 30
                    y: parent.height - 34
                    width: 34; height: 34
                    icon: "folder-open"
                    iconSize: 15
                    restOpacity: 0.5
                    opacity: decrypt.busy ? 0 : 1
                    visible: opacity > 0
                    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
                    onClicked: folderPicker.open()
                }
            }

            Item { width: 1; height: 16 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: decrypt.current
                elide: Text.ElideMiddle
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
                opacity: decrypt.busy ? 1 : 0
                Behavior on opacity { OpacityAnimator { duration: Theme.normal } }
            }
            Item { width: 1; height: 16 }

            // ---------------- 格式 (状态点;不可用的悬停显示原因) ----------------
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 22
                Repeater {
                    model: decrypt.decoders
                    Row {
                        spacing: 7
                        opacity: modelData.available ? 1 : 0.45
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 5; height: 5; radius: 2.5
                            color: modelData.available ? Theme.ok : Theme.warn
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.id.toUpperCase()
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.letterSpacing: 1
                            MouseArea { id: fm; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.reason
                            color: Theme.warn
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            visible: !modelData.available && fm.containsMouse
                        }
                    }
                }
            }

            // ---------------- 曲库中待解密 ----------------
            Item { width: 1; height: (page.pend.total || 0) > 0 ? 22 : 0 }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: (page.pend.total || 0) > 0
                width: pendRow.implicitWidth + 30
                height: visible ? 36 : 0
                radius: 18
                color: pm.containsMouse ? Theme.accentAlpha(0.16) : Theme.accentAlpha(0.08)
                Behavior on color { ColorAnimation { duration: Theme.fast } }
                Row {
                    id: pendRow
                    anchors.centerIn: parent
                    spacing: 8
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "lock"; size: 14; tint: Theme.accent }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.pend.total || 0
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                }
                MouseArea {
                    id: pm
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !decrypt.busy
                    onClicked: decrypt.decryptPending()
                }
            }

            // ---------------- 产物目录 ----------------
            Item { width: 1; height: 30 }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "folder-open"; size: 13; opacity: 0.4 }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, col.width - 100)
                    text: decrypt.outputDir
                    elide: Text.ElideMiddle
                    color: om.containsMouse ? Theme.textSecondary : Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    MouseArea { id: om; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: outPicker.open() }
                }
                IconButton { anchors.verticalCenter: parent.verticalCenter; width: 26; height: 26; icon: "external"; iconSize: 12; restOpacity: 0.4; onClicked: decrypt.openOutputFolder() }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26; height: 26
                    icon: "library-add"
                    iconSize: 12
                    restOpacity: 0.4
                    onClicked: { decrypt.addOutputToLibrary(); Nav.toast("library-add", qsTr("已加入曲库")) }
                }
            }

            // ---------------- 扫描时:询问 · 自动 · 不解密 ----------------
            Item { width: 1; height: 4 }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                Repeater {
                    model: [qsTr("询问"), qsTr("自动"), qsTr("不解密")]
                    Text {
                        readonly property bool on: decrypt.policy === index
                        text: modelData
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 4
                        bottomPadding: 4
                        color: on ? Theme.accent : (sm.containsMouse ? Theme.textSecondary : Theme.textTertiary)
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: decrypt.policy = index }
                    }
                }
            }

            // ---------------- 结果 ----------------
            Item { width: 1; height: decrypt.results.length ? 34 : 0 }
            Repeater {
                model: decrypt.results
                delegate: Item {
                    width: col.width
                    height: 36
                    opacity: 0
                    Component.onCompleted: fade.start()
                    OpacityAnimator on opacity { id: fade; running: false; to: 1; duration: 260 }
                    Icon {
                        id: st
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.ok ? "check" : "close"
                        size: 13
                        tint: modelData.ok ? Theme.accent : Theme.warn
                    }
                    Text {
                        anchors.left: st.right
                        anchors.leftMargin: 12
                        anchors.right: msg.left
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        elide: Text.ElideMiddle
                        color: modelData.ok ? Theme.textSecondary : Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                    Text {
                        id: msg
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: modelData.ok ? 0 : Math.min(implicitWidth, parent.width * 0.4)
                        horizontalAlignment: Text.AlignRight
                        text: modelData.ok ? "" : modelData.message
                        elide: Text.ElideRight
                        color: Theme.warn
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
