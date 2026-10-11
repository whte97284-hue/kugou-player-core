import QtQuick
import PlayerUi

// 「正在播放」律动小图标:三根柱子,取播放器的三段律动值 (不读 32 段频谱数组,零转换开销)
Row {
    id: bars
    property real barWidth: 3
    property real maxHeight: 14
    property color color: Theme.accent
    height: maxHeight
    spacing: 2
    Repeater {
        model: 3
        Rectangle {
            anchors.bottom: parent.bottom
            width: bars.barWidth
            radius: width / 2
            color: bars.color
            height: Math.max(3, bars.maxHeight * (player.playing ? (index === 0 ? player.pulse0 : index === 1 ? player.pulse1 : player.pulse2) * 0.85 + 0.15 : 0.22))
        }
    }
}
