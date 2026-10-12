import QtQuick

// 柔和投影:预模糊的 9 宫格贴图,任意尺寸拉伸零开销
BorderImage {
    property Item target
    property real spread: 26
    property real offsetY: 14
    property real strength: 1.0
    x: target ? target.x - spread : 0
    y: target ? target.y - spread + offsetY : 0
    width: target ? target.width + spread * 2 : 0
    height: target ? target.height + spread * 2 : 0
    source: "qrc:/images/shadow.png"
    border { left: 64; top: 64; right: 64; bottom: 64 }
    // 跟随目标的透明度:共享元素飞行时目标被隐藏,投影不该单独留在原地
    opacity: strength * (target ? target.opacity : 1)
    smooth: true
}
