import QtQuick
import PlayerUi

// 专辑封面面板 —— 从专辑墙横幅的「封面」按钮弹出
//
// 为什么按「专辑」补:曲库里没有「专辑封面」这个独立实体。
// 专辑封面借用该专辑里第一首有封面的曲目 (library_service.cpp),一首都没有就用第一首占位。
// 所以一张专辑常常只有一两首带图 —— 专辑墙看着有封面,其余曲目拷到别的播放器就没图了。
// 这里一次把整张专辑的每一首都补齐。
//
// 无字化:只用明暗 / 边框 / 勾号表达状态,面板里不出现任何文字。
Item {
    id: root
    property bool shown: false
    // 弹出锚点:面板右上角对齐这个点 (root 坐标系)
    property real anchorRight: width - 40
    property real anchorBottom: height - 40
    anchors.fill: parent
    visible: pop.opacity > 0
    z: 6

    property var albums: []
    property var picked: ({})   // albumIndex -> true
    // 底部播放栏高度:面板不能压到它下面,否则底部按钮点不到
    property int bottomInset: 140
    readonly property bool busy: covers.busy
    readonly property int selCount: {
        let n = 0
        for (const k in picked) if (picked[k]) n++
        return n
    }

    function reload() { albums = covers.albumStates() }
    function openAt(item) {
        if (item) {
            const p = item.mapToItem(root, 0, item.height)
            anchorRight = p.x + item.width
            anchorBottom = p.y + 12
        }
        reload()
        picked = ({})
        shown = true
        covers.checkStale()   // 后台核对"快照说没封面但磁盘已有",有则提示重建
    }
    function close() { shown = false }
    function toggle(i, missing) {
        if (missing <= 0 || busy) return
        const p = Object.assign({}, picked)
        if (p[i]) delete p[i]
        else p[i] = true
        picked = p
    }
    function run() {
        if (busy) { covers.cancel(); return }
        const ids = []
        for (const k in picked) if (picked[k]) ids.push(Number(k))
        if (ids.length > 0) covers.fetchAlbums(ids)
        else covers.fetchAlbums([])   // 空 = 全部
    }
    function runAll() { if (busy) covers.cancel(); else covers.fetchAlbums([]) }

    // 补完封面会重扫曲库:版本号变了就重新取一次状态
    Connections {
        target: library
        function onLibraryChanged() { if (root.shown) root.reload() }
    }
    Connections {
        target: covers
        function onFinished(ok, failed) { root.reload() }
    }

    // 点面板外收起
    MouseArea {
        anchors.fill: parent
        enabled: root.shown
        acceptedButtons: Qt.AllButtons
        onPressed: root.close()
        onWheel: (w) => w.accepted = true
    }

    // 面板连同投影一起从按钮处滑出:向下平移 + 轻微放大 + 淡入
    Item {
        id: pop
        readonly property int pad: 18
        readonly property int headH: 46
        readonly property int footH: 58
        readonly property int cols: 4
        readonly property int gap: 10
        width: Math.min(520, root.width - 40)
        readonly property real cell: (width - pad * 2 - gap * (cols - 1)) / cols
        readonly property int rows: Math.max(1, Math.ceil(root.albums.length / cols))
        // 高度跟着行数走:专辑少时不留大片空白,多了才滚动
        readonly property real contentH: headH + rows * cell + (rows - 1) * gap + footH + 8
        // 安全区:顶部留 20,底部让开播放栏 —— 保证底部按钮始终可点
        readonly property int safeTop: 20
        readonly property real safeBottom: Math.max(safeTop + 240, root.height - root.bottomInset - 8)
        readonly property real maxH: safeBottom - safeTop
        height: Math.min(contentH, maxH)
        x: Math.max(20, Math.min(root.width - width - 20, root.anchorRight - width))
        y: Math.max(pop.safeTop, Math.min(pop.safeBottom - height, root.anchorBottom))
        transformOrigin: Item.TopRight
        opacity: root.shown ? 1 : 0
        scale: root.shown ? 1 : 0.97
        Behavior on opacity { OpacityAnimator { duration: root.shown ? 240 : 140; easing.type: Easing.OutCubic } }
        Behavior on scale { ScaleAnimator { duration: root.shown ? 480 : 160; easing.type: root.shown ? Easing.OutQuint : Easing.InCubic } }
        transform: Translate {
            y: root.shown ? 0 : -14
            Behavior on y { NumberAnimation { duration: root.shown ? 520 : 160; easing.type: root.shown ? Easing.OutQuint : Easing.InCubic } }
        }
        SoftShadow { target: card; strength: 0.85; spread: 30; offsetY: 16 }

        Rectangle {
            id: card
            anchors.fill: parent
            radius: Theme.radiusL
            color: Theme.panel
            border.width: 1
            border.color: Qt.rgba(1, 0.96, 0.9, 0.08)
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }
            Keys.onEscapePressed: root.close()

            // ---- 标题:只放图标,不放文字 ----
            Icon {
                id: heading
                x: pop.pad
                y: 16
                name: "album-open"
                size: 18
                opacity: 0.9
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.top: parent.top
                anchors.topMargin: 8
                width: 32; height: 32
                icon: "close"
                iconSize: 16
                onClicked: root.close()
            }

            // ---- 专辑网格 ----
            GridView {
                id: grid
                x: pop.pad
                y: pop.headH
                width: card.width - pop.pad * 2
                height: Math.min(card.height - pop.headH - pop.footH,
                                 pop.rows * pop.cell + (pop.rows - 1) * pop.gap)
                clip: true
                cellWidth: pop.cell
                cellHeight: pop.cell
                model: root.albums
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    id: cellItem
                    width: grid.cellWidth
                    height: grid.cellHeight
                    readonly property int albumIndex: modelData.index
                    readonly property int missing: modelData.missing
                    readonly property bool on: !!root.picked[albumIndex]
                    readonly property bool dead: modelData.fixable === 0   // 这张补不了(缺的是 WAV/DSF 等)

                    RoundedImage {
                        anchors.fill: parent
                        anchors.margins: 3
                        source: modelData.cover
                        radius: Theme.radiusS
                        decodeSize: 128          // 格子只有 ~110px,解大了白费
                        fadeIn: false
                        opacity: cellItem.dead ? 0.3 : 1
                    }

                    // 缺封面的圈一圈,悬停变亮;选中换成强调色
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 3
                        radius: Theme.radiusS
                        color: "transparent"
                        border.width: cellItem.on ? 2 : 1
                        border.color: cellItem.on ? Theme.accent
                                    : cellItem.dead ? Qt.rgba(0.6, 0.6, 0.6, 0.3)
                                    : (hover.hovered ? Qt.rgba(1, 1, 1, 0.55) : Qt.rgba(1, 1, 1, 0.22))
                        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
                    }

                    // 缺几首:右下角小点(无字提示,1 个点 = 全部缺,2 个 = 部分)
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 9
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 9
                        spacing: 3
                        Repeater {
                            model: cellItem.dead ? 0 : (cellItem.missing >= 2 ? 2 : 1)
                            Rectangle { width: 5; height: 5; radius: 2.5; color: Theme.accent }
                        }
                        // 补不了的:一根灰横杠
                        Rectangle {
                            visible: cellItem.dead
                            width: 10; height: 2; radius: 1
                            color: Qt.rgba(1, 1, 1, 0.35)
                        }
                    }

                    // 选中:右上角勾
                    Rectangle {
                        width: 20; height: 20; radius: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        color: Theme.accent
                        visible: cellItem.on
                        scale: cellItem.on ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutBack } }
                        Icon {
                            anchors.centerIn: parent
                            name: "check"
                            size: 12
                            tint: Theme.onAccent
                        }
                    }

                    HoverHandler { id: hover; enabled: !root.busy && !cellItem.dead }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !root.busy && !cellItem.dead
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.toggle(cellItem.albumIndex, cellItem.missing)
                    }
                }
            }

            // 没有缺封面的专辑:居中一个勾,不打扰
            Icon {
                anchors.centerIn: grid
                name: "check"
                size: 34
                opacity: root.albums.length === 0 ? 0.18 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Theme.normal } }
            }

            // ---- 底部条:固定在卡片底边,永远可见可点 ----
            Rectangle {
                id: foot
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: pop.footH
                color: "transparent"
                // 上方一条分隔线,让底部条和网格分开
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Theme.hairline
                    visible: grid.height > 0
                }

                Capsule {
                    id: errCap
                    anchors.left: parent.left
                    anchors.leftMargin: pop.pad
                    anchors.verticalCenter: parent.verticalCenter
                    visible: (covers.failures.length > 0 || covers.skips.length > 0) && !root.busy
                    text: covers.failures.length > 0
                          ? qsTr("失败 %1 · 跳过 %2").arg(covers.failures.length).arg(covers.skips.length)
                          : qsTr("跳过 %1").arg(covers.skips.length)
                    onClicked: covers.openFailureLog()
                }

                // 需要重建索引:磁盘上已有封面但曲库还没认到 —— 给一个直接入口
                Capsule {
                    id: rebuildCap
                    anchors.left: errCap.visible ? errCap.right : parent.left
                    anchors.leftMargin: errCap.visible ? 8 : pop.pad
                    anchors.verticalCenter: parent.verticalCenter
                    visible: covers.needRebuild && !root.busy
                    accent: true
                    pad: 12
                    text: qsTr("重建索引")
                    onClicked: settings.rebuildLibrary()
                }
            }
            IconButton {
                id: selBtn
                anchors.right: allCap.left
                anchors.rightMargin: 10
                anchors.verticalCenter: foot.verticalCenter
                visible: root.selCount > 0 && !root.busy
                width: 44; height: 44
                filled: true
                icon: "import"
                iconSize: 18
                tip: qsTr("补齐选中的 %1 张").arg(root.selCount)
                onClicked: root.run()
            }
            Capsule {
                id: allCap
                anchors.right: parent.right
                anchors.rightMargin: pop.pad
                anchors.verticalCenter: foot.verticalCenter
                accent: true
                text: root.busy ? qsTr("取消") : qsTr("全部补全")
                onClicked: root.runAll()
            }

            // 进行中的进度:细条贴在卡片底边 (无字化,只靠长度表达)
            Item {
                id: bar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 0
                height: 3
                visible: root.busy
                Rectangle {
                    anchors.fill: parent
                    radius: 1.5
                    color: Theme.accentAlpha(0.12)
                    opacity: root.busy ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.normal } }
                }
                Rectangle {
                    height: parent.height
                    radius: 1.5
                    color: Theme.accent
                    width: parent.width * Math.max(0, Math.min(1, covers.progress))
                    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                }
            }
        }
    }
}
