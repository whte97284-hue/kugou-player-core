import QtQuick
import PlayerUi

// 32 段频谱:强调色渐变柱,顶部亮帽;暂停后由内核阻尼自然回落
Item {
    id: spec
    property int bars: 32
    property real gap: 3
    readonly property real barW: (width - gap * (bars - 1)) / bars
    // 每帧只把频谱数组转换一次,而不是每根柱子各转换一次
    readonly property var values: visible ? player.spectrum : []

    Row {
        anchors.bottom: parent.bottom
        spacing: spec.gap
        Repeater {
            model: spec.bars
            Item {
                width: spec.barW
                height: spec.height
                readonly property real v: spec.values[index] || 0
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    visible: v > 0.02
                    height: Math.max(2, parent.height * v)
                    radius: Math.min(2, width / 2)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.accent }
                        GradientStop { position: 1.0; color: Theme.accentAlpha(0.12) }
                    }
                }
            }
        }
    }
}
