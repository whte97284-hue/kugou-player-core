import QtQuick

// 白色 SVG 图标,按物理像素光栅化保证清晰;明暗通过 opacity 控制
Image {
    property string name
    property int size: 20
    width: size
    height: size
    source: name ? "qrc:/icons/" + name + ".svg" : ""
    sourceSize: Qt.size(size * 2, size * 2)
    smooth: true
    mipmap: true
    asynchronous: false
}
