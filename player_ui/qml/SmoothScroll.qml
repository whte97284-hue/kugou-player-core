import QtQuick

// 平滑滚轮:鼠标滚轮每一格以缓出曲线滑动,连续滚动时目标累加 (不会一顿一顿);
// 触控板 (像素增量) 直接跟手。挂在 Flickable 内部:SmoothScroll { flickable: list }
WheelHandler {
    id: h
    property Flickable flickable: parent
    property bool horizontal: false
    property real step: 120          // 每格滚动像素
    property int duration: 340
    signal scrolled()
    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    // 横向条内容不够滚时不拦截滚轮,交给外层页面纵向滚动
    enabled: !horizontal || maxPos > minPos + 1

    readonly property real minPos: horizontal ? flickable.originX - flickable.leftMargin : flickable.originY - flickable.topMargin
    readonly property real maxPos: Math.max(minPos, horizontal
        ? flickable.originX + flickable.contentWidth - flickable.width + flickable.rightMargin
        : flickable.originY + flickable.contentHeight - flickable.height + flickable.bottomMargin)
    property real goal: 0

    function clamp(v) { return Math.max(minPos, Math.min(maxPos, v)) }
    function current() { return horizontal ? flickable.contentX : flickable.contentY }
    function apply(v) { if (horizontal) flickable.contentX = v; else flickable.contentY = v }

    onWheel: (e) => {
        if (!flickable) return
        h.scrolled()
        const pixel = horizontal ? (e.pixelDelta.x || e.pixelDelta.y) : e.pixelDelta.y
        const angle = horizontal ? (e.angleDelta.x || e.angleDelta.y) : e.angleDelta.y
        if (pixel !== 0 && e.device.type === PointerDevice.TouchPad) {
            anim.stop()
            apply(clamp(current() - pixel))
            return
        }
        if (angle === 0) return
        const base = anim.running ? goal : current()
        goal = clamp(base - angle / 120 * step)
        anim.from = current()
        anim.to = goal
        anim.restart()
    }

    property NumberAnimation anim: NumberAnimation {
        target: h.flickable
        property: h.horizontal ? "contentX" : "contentY"
        duration: h.duration
        easing.type: Easing.OutCubic
    }
    // 用户拖动或代码跳转时停止动画
    property Connections stopOnDrag: Connections {
        target: h.flickable
        function onMovementStarted() { anim.stop() }
    }
    function stop() { anim.stop() }
}
