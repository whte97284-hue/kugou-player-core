pragma Singleton
import QtQuick

// 全局导航总线:任意组件发出意图,由 Main 统一处理 (页面切换、右键菜单、轻提示)
QtObject {
    signal openAlbum(int index)
    signal openArtist(int index)
    // 带来源图块:头像从来源位置飞入详情页
    signal openArtistFrom(int index, Item item)
    signal openPlaylist(string id)
    // ctx: { playlistId } 等上下文 (在歌单中时菜单多出「从歌单移除」)
    signal trackMenu(real trackId, real x, real y, var ctx)
    signal toast(string icon, string text)
    // 新建歌单后,详情页打开时直接进入改名状态
    property string renameNext: ""

    // 富文本高亮:query 出现在 text 中时以强调色加粗
    function highlight(text, query, color) {
        const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        const q = String(query || "").trim()
        if (!q.length) return esc(text)
        const i = String(text).toLowerCase().indexOf(q.toLowerCase())
        if (i < 0) return esc(text)
        return esc(text.substring(0, i)) + "<font color=\"" + color + "\"><b>" + esc(text.substr(i, q.length)) + "</b></font>"
             + esc(text.substring(i + q.length))
    }
}
