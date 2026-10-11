import QtQuick
import PlayerUi

// 曲库为空时的引导与首次导入视图
//  · 空闲:标题 + 选择文件夹主按钮 + 拖放提示 + 支持格式
//  · 导入中:大号计数 (已识别 / 总数) + 细进度线 + 当前文件 + 取消
//  · 扫完却没找到音乐:说明原因并可重新选择
Item {
    id: ob
    signal pickFolder()

    readonly property bool importing: library.scanning
    readonly property bool foundNothing: !importing && library.folders.length > 0 && library.trackCount === 0

    // ---------------- 空闲 / 未找到 ----------------
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -30
        spacing: 0
        opacity: ob.importing ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }

        // 唱片标识:强调色细环 + 中心点,缓慢呼吸
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 92; height: 92
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.accentAlpha(0.35)
            }
            Rectangle {
                anchors.centerIn: parent
                width: 58; height: 58; radius: 29
                color: Theme.accentAlpha(0.10)
                border.width: 1
                border.color: Theme.accentAlpha(0.55)
                SequentialAnimation on scale {
                    loops: Animation.Infinite
                    running: ob.visible && !ob.importing
                    ScaleAnimator { from: 1; to: 1.08; duration: 1800; easing.type: Easing.InOutSine }
                    ScaleAnimator { from: 1.08; to: 1; duration: 1800; easing.type: Easing.InOutSine }
                }
            }
            Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: Theme.accent }
        }

        Item { width: 1; height: 34 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ob.foundNothing ? qsTr("这里没有找到可播放的音乐") : qsTr("把你的音乐带进来")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 30
            font.weight: Font.Bold
        }
        Item { width: 1; height: 12 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: ob.foundNothing
                  ? qsTr("已扫描所选文件夹,但没有识别到音频文件。换一个文件夹试试。")
                  : qsTr("选择存放音乐的文件夹,子文件夹会一并扫描。标签、封面、CUE 分轨都会自动识别。")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }

        Item { width: 1; height: 36 }
        // 主按钮
        Rectangle {
            id: primary
            anchors.horizontalCenter: parent.horizontalCenter
            width: btnRow.implicitWidth + 56
            height: 50
            radius: 25
            color: Theme.accent
            scale: btnMouse.pressed ? 0.97 : (btnMouse.containsMouse ? 1.02 : 1)
            Behavior on scale { ScaleAnimator { duration: Theme.fast; easing.type: Easing.OutCubic } }
            Row {
                id: btnRow
                anchors.centerIn: parent
                spacing: 10
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: Theme.accentIsLight ? "folder-dark" : "folder"
                    size: 18
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("选择音乐文件夹")
                    color: Theme.onAccent
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
            }
            MouseArea {
                id: btnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: ob.pickFolder()
            }
        }
        Item { width: 1; height: 16 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("也可以直接把文件夹拖进窗口")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }

        Item { width: 1; height: 48 }
        // 支持格式:一行细字,中点分隔
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "FLAC  ·  WAV  ·  AIFF  ·  ALAC  ·  APE  ·  DSF / DFF  ·  MP3  ·  AAC  ·  OGG / OPUS  ·  CUE  ·  KGM / NCM"
            color: Theme.textTertiary
            opacity: 0.8
            font.family: Theme.fontFamily
            font.pixelSize: 11
            font.letterSpacing: 1
        }
    }

    // ---------------- 导入中 ----------------
    Column {
        id: prog
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -30
        width: Math.min(520, ob.width - 80)
        spacing: 0
        opacity: ob.importing ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { OpacityAnimator { duration: Theme.normal } }

        // 数字平滑滚动,而不是每次跳变
        property real shown: library.scanDone
        Behavior on shown { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Text {
            text: library.scanTotal > 0 ? qsTr("正在识别你的音乐") : qsTr("正在查找音乐文件")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }
        Item { width: 1; height: 10 }
        Row {
            spacing: 10
            Text {
                text: Math.round(prog.shown).toLocaleString(Qt.locale(), "f", 0)
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 64
                font.weight: Font.Bold
            }
            Text {
                anchors.baseline: parent.children[0].baseline
                visible: library.scanTotal > 0
                text: "/ " + library.scanTotal.toLocaleString(Qt.locale(), "f", 0) + qsTr(" 个文件")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 18
            }
        }
        Item { width: 1; height: 22 }
        // 进度线:总数未知时是一段往复移动的光带
        Item {
            width: parent.width
            height: 3
            clip: true
            Rectangle { anchors.fill: parent; radius: 1.5; color: Theme.hairline }
            Rectangle {
                visible: library.scanTotal > 0
                height: parent.height
                radius: 1.5
                width: parent.width * library.progress
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
            }
            Rectangle {
                id: seekerBar
                visible: library.scanTotal === 0
                width: parent.width * 0.28
                height: parent.height
                radius: 1.5
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.accentAlpha(0) }
                    GradientStop { position: 0.5; color: Theme.accent }
                    GradientStop { position: 1; color: Theme.accentAlpha(0) }
                }
                XAnimator on x {
                    from: -seekerBar.width; to: prog.width
                    duration: 1400
                    loops: Animation.Infinite
                    running: seekerBar.visible && ob.importing
                }
            }
        }
        Item { width: 1; height: 14 }
        Item {
            width: parent.width
            height: 20
            Text {
                anchors.left: parent.left
                anchors.right: cancelText.left
                anchors.rightMargin: 20
                text: library.scanFile
                elide: Text.ElideMiddle
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            Text {
                id: cancelText
                anchors.right: parent.right
                text: qsTr("取消")
                color: cancelMouse.containsMouse ? Theme.text : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 13
                MouseArea {
                    id: cancelMouse
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: library.cancelScan()
                }
            }
        }
    }
}
