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
        if (!anim.running) { pos = current(); goal = pos }
        goal = clamp(goal - angle / 120 * step)
        anim.start()
    }

    // 逐帧指数追随目标 (和浏览器的平滑滚动同一思路):
    //  · 连续滚动时只是把目标往前推,当前速度不会被打断重来,没有一格一格的顿挫;
    //  · 按真实帧间隔计算,60 / 144Hz 下速度一致;
    //  · 内部用浮点位置,写回时对齐到物理像素,文字和封面不会在亚像素间抖动
    property real pos: 0
    readonly property real tau: duration / 4600   // 时间常数 (秒):340ms 约等于 74ms,约 0.3s 基本停稳
    property FrameAnimation anim: FrameAnimation {
        onTriggered: {
            const g = h.clamp(h.goal)
            const k = 1 - Math.exp(-frameTime / h.tau)
            h.pos += (g - h.pos) * k
            if (Math.abs(g - h.pos) < 0.25) { h.pos = g; stop() }
            const dpr = h.flickable.Window.window ? h.flickable.Window.window.devicePixelRatio : 1
            h.apply(Math.round(h.pos * dpr) / dpr)
        }
    }
    // 用户拖动或代码跳转时停止动画
    property Connections stopOnDrag: Connections {
        target: h.flickable
        function onMovementStarted() { anim.stop() }
    }
    function stop() { anim.stop() }
}
