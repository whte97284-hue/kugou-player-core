import QtQuick
import PlayerUi

// 扫描进度胶囊:扫描时出现,完成后显示结果并在 3 秒后淡出
Rectangle {
    id: toast
    property bool shown: enabled && (library.scanning || hold.running)
    width: Math.max(120, label.implicitWidth + 50)
    height: 38
    radius: 19
    color: Theme.panel
    border.width: 1
    border.color: Theme.hairline
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { OpacityAnimator { duration: Theme.normal } }

    Timer { id: hold; interval: 3200 }
    Connections {
        target: library
        function onScanningChanged() { if (!library.scanning) hold.restart() }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 8; height: 8; radius: 4
        color: Theme.accent
        SequentialAnimation on opacity {
            running: library.scanning
            loops: Animation.Infinite
            OpacityAnimator { from: 1; to: 0.25; duration: 600 }
            OpacityAnimator { from: 0.25; to: 1; duration: 600 }
        }
    }
    Text {
        id: label
        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        // 扫描中:已识别 / 总数;完成:曲目数 (纯数字,配合左侧状态点)
        text: library.scanning ? (library.scanTotal > 0 ? library.scanDone + " / " + library.scanTotal : "···")
                               : library.trackCount.toLocaleString(Qt.locale(), "f", 0)
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: 13
    }
    // 底部细进度线
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        x: toast.radius
        height: 2
        radius: 1
        width: (toast.width - toast.radius * 2) * library.progress
        color: Theme.accent
        visible: library.scanning
    }
}
