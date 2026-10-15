// ============================================================================
//  PlaylistProvider 实现 —— 歌单分享链接/ID → 明文曲目清单
//
//  协议来源 (2026-10-16 实测验证):
//
//   酷狗 (无需签名):
//     GET https://www2.kugou.kugou.com/yueku/v9/special/single/{specialId}-5-9999.html
//     页面内嵌  global.data = [ {...}, ... ];   ← 整个曲目数组
//     另有      global = { ... name: "歌单名" ... pic: "封面" ... };
//
//   网易云 (无需登录/无需加密, 两步式):
//     1) POST http://music.163.com/api/v6/playlist/detail   body: s=0&id={pid}&n=1000&t=0
//        → playlist.name / trackCount / trackIds[].id   (注意: trackIds 元素是对象)
//     2) GET  http://music.163.com/api/song/detail/?id={csv}&ids=%5B{csv}%5D
//        → songs[].name / ar[].name / dt / id / al.id
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include "playlist_provider.hpp"

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <regex>
#include <map>
#include <memory>

// windows.h 会把 min/max 定义成宏, 与 std::min/std::max 冲突
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#pragma comment(lib, "winhttp.lib")

namespace playlist_core {

// ============================================================================
//  轻量 JSON 解析 (与 lyric_provider 同思路: 够用即可,不做通用库)
// ============================================================================
namespace {

struct JVal;
using JValPtr = std::shared_ptr<JVal>;   // 简易共享指针, 避免手写引用计数

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;                                   // 原始 UTF-8 字节
    std::vector<JValPtr> arr;
    std::map<std::string, JValPtr> obj;

    const JVal* Get(const char* k) const {
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : it->second.get();
    }
    const JVal* At(size_t i) const {
        return i < arr.size() ? arr[i].get() : nullptr;
    }
    size_t Size() const { return type == Arr ? arr.size() : 0; }
    bool IsStr() const { return type == Str; }
    bool IsNum() const { return type == Num; }
    bool IsArr() const { return type == Arr; }
    bool IsObj() const { return type == Obj; }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    JValPtr Parse() {
        SkipWs();
        JValPtr v = ParseValue();
        return v;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void SkipWs() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i_; continue; }
            break;
        }
    }

    bool Literal(const char* lit) {
        size_t n = strlen(lit);
        if (s_.compare(i_, n, lit) == 0) { i_ += n; return true; }
        return false;
    }

    JValPtr ParseValue() {
        SkipWs();
        if (i_ >= s_.size()) return nullptr;
        char c = s_[i_];
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == 't') { if (Literal("true")) { auto v = std::make_shared<JVal>(); v->type = JVal::Bool; v->b = true; return v; } }
        if (c == 'f') { if (Literal("false")) { auto v = std::make_shared<JVal>(); v->type = JVal::Bool; v->b = false; return v; } }
        if (c == 'n') { if (Literal("null")) { return std::make_shared<JVal>(); } }
        return ParseNumber();
    }

    JValPtr ParseString() {
        auto v = std::make_shared<JVal>();
        v->type = JVal::Str;
        ++i_; // skip "
        std::string out;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') break;
            if (c == '\\' && i_ < s_.size()) {
                char e = s_[i_++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        // \uXXXX (含代理对)
                        unsigned cp = 0;
                        if (i_ + 4 <= s_.size()) {
                            cp = (unsigned)strtoul(s_.substr(i_, 4).c_str(), nullptr, 16);
                            i_ += 4;
                        }
                        if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 6 <= s_.size() &&
                            s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                            unsigned lo = (unsigned)strtoul(s_.substr(i_ + 2, 4).c_str(), nullptr, 16);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                i_ += 6;
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        v->str = std::move(out);
        return v;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    JValPtr ParseNumber() {
        auto v = std::make_shared<JVal>();
        v->type = JVal::Num;
        size_t start = i_;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') ++i_;
            else break;
        }
        v->num = strtod(s_.substr(start, i_ - start).c_str(), nullptr);
        return v;
    }

    JValPtr ParseArray() {
        auto v = std::make_shared<JVal>();
        v->type = JVal::Arr;
        ++i_; // [
        SkipWs();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; return v; }
        while (i_ < s_.size()) {
            JValPtr e = ParseValue();
            if (!e) break;
            v->arr.push_back(e);
            SkipWs();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
            break;
        }
        return v;
    }

    JValPtr ParseObject() {
        auto v = std::make_shared<JVal>();
        v->type = JVal::Obj;
        ++i_; // {
        SkipWs();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; return v; }
        while (i_ < s_.size()) {
            SkipWs();
            if (i_ >= s_.size() || s_[i_] != '"') break;
            JValPtr key = ParseString();
            SkipWs();
            if (i_ >= s_.size() || s_[i_] != ':') break;
            ++i_; // :
            JValPtr val = ParseValue();
            if (!val) break;
            v->obj[key->str] = val;
            SkipWs();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
            break;
        }
        return v;
    }
};

// ---- 宽窄字符串转换 ----
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

std::wstring TrimW(const std::wstring& s) {
    size_t b = 0, e = s.size();
    auto sp = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
               c == 0x3000 || c == L'"' || c == 0xA0;
    };
    while (b < e && sp(s[b])) ++b;
    while (e > b && sp(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// 该 JSON 字符串值按 UTF-8 转宽 (网易云/酷狗返回均为 UTF-8)
std::wstring JStr(const JVal* v) {
    if (!v || !v->IsStr()) return {};
    return TrimW(ToWide(v->str));
}

std::string JStrRaw(const JVal* v) {
    return (v && v->IsStr()) ? v->str : std::string();
}

// 数值字段 (可能是 number 也可能是 string)
double JNum(const JVal* v) {
    if (!v) return 0;
    if (v->IsNum()) return v->num;
    if (v->IsStr()) return strtod(v->str.c_str(), nullptr);
    return 0;
}

// 剥掉 HTML 里的 <em> 之类标签 (网易云歌名里可能带高亮标签)
std::wstring StripHtml(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    bool in = false;
    for (wchar_t c : s) {
        if (c == L'<') { in = true; continue; }
        if (c == L'>') { in = false; continue; }
        if (!in) out += c;
    }
    return out;
}

} // anonymous namespace

// ============================================================================
//  平台识别 & 输入归一化
// ============================================================================

const char* PlatformName(Platform p) {
    switch (p) {
        case Platform::Kugou:   return "kugou";
        case Platform::NetEase: return "netease";
        default:                return "unknown";
    }
}

Platform PlatformFromName(const std::string& name) {
    if (name == "kugou" || name == "kg")     return Platform::Kugou;
    if (name == "netease" || name == "wyy" || name == "163") return Platform::NetEase;
    return Platform::Unknown;
}

Platform PlaylistProvider::DetectPlatform(const std::string& input) {
    std::string low = input;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)tolower(c); });

    // 纯数字 → 无法从内容判断平台 (调用方需指定或由链接上下文决定)
    bool all_digit = !low.empty() &&
        std::all_of(low.begin(), low.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (all_digit) return Platform::Unknown;

    if (low.find("kugou.com") != std::string::npos) return Platform::Kugou;
    if (low.find("kgl") != std::string::npos)       return Platform::Kugou;
    if (low.find("music.163.com") != std::string::npos) return Platform::NetEase;
    if (low.find("163cn.tv") != std::string::npos)      return Platform::NetEase;
    if (low.find("y.music.163") != std::string::npos)   return Platform::NetEase;
    if (low.find("netease") != std::string::npos)       return Platform::NetEase;
    return Platform::Unknown;
}

// 从 URL 中提取指定参数值
static std::string ExtractQuery(const std::string& url, const std::string& key) {
    std::string pat1 = key + "=";
    size_t p = url.find("?" + pat1);
    if (p == std::string::npos) {
        if (url.compare(0, pat1.size(), pat1) == 0) p = 0;
        else {
            p = url.find("&" + pat1);
            if (p != std::string::npos) p += 1;
        }
    } else {
        p += 1;
    }
    if (p == std::string::npos) return {};
    size_t v = p + pat1.size();
    size_t e = url.find_first_of("&#", v);
    return url.substr(v, e == std::string::npos ? std::string::npos : e - v);
}

bool PlaylistProvider::ParseInput(const std::string& raw, Platform& plat, std::string& id) {
    std::string input = raw;
    // 去掉首尾空白
    size_t b = input.find_first_not_of(" \t\r\n");
    size_t e = input.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    input = input.substr(b, e - b + 1);

    std::string low = input;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)tolower(c); });

    // 1) 纯数字 ID
    bool all_digit = !input.empty() &&
        std::all_of(input.begin(), input.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (all_digit) {
        // 纯数字无法判断平台,交给调用方 (这里给 Unknown + 填入 id)
        plat = Platform::Unknown;
        id = input;
        return true;
    }

    // 2) 酷狗形态
    if (low.find("kugou.com") != std::string::npos || low.find("kgl") != std::string::npos) {
        plat = Platform::Kugou;
        std::smatch m;
        // /yy/special/single/1067062.html  或  /special/single/xxx-5-9999.html
        std::regex re_single(R"(special/single/(\d+))");
        if (std::regex_search(input, m, re_single)) { id = m[1].str(); return true; }
        // m.kugou.com/plist/list/123456
        std::regex re_plist(R"(plist/list/(\d+))");
        if (std::regex_search(input, m, re_plist)) { id = m[1].str(); return true; }
        // 分享页参数 (m.kugou.com/share/zlist.html?listid=xxx)
        for (const char* key : { "listid", "list_id", "specialid" }) {
            std::string v = ExtractQuery(input, key);
            if (!v.empty()) {
                std::regex re_num(R"((\d{4,}))");
                if (std::regex_search(v, m, re_num)) { id = m[1].str(); return true; }
            }
        }
        // global_collection_id=collection_3_xxx_yyy_0
        std::string gcid = ExtractQuery(input, "global_collection_id");
        if (!gcid.empty()) { id = gcid; return true; }
        // 兜底: 抓最后一段较长数字
        std::regex re_last(R"((\d{5,}))");
        if (std::regex_search(input, m, re_last)) { id = m[1].str(); return true; }
        return false;
    }

    // 3) 网易云形态
    if (low.find("163.com") != std::string::npos || low.find("163cn.tv") != std::string::npos) {
        plat = Platform::NetEase;
        // ?id=xxx
        std::string qid = ExtractQuery(input, "id");
        if (!qid.empty() && std::all_of(qid.begin(), qid.end(),
                [](char c){ return isdigit((unsigned char)c); })) {
            id = qid;
            return true;
        }
        // /playlist/1234567890/1/ (旧版)
        std::regex re_pl(R"(/playlist/(\d+))");
        std::smatch m;
        if (std::regex_search(input, m, re_pl)) { id = m[1].str(); return true; }
        // /m/playlist?id=
        std::regex re_digits(R"((\d{5,}))");
        if (std::regex_search(input, m, re_digits)) { id = m[1].str(); return true; }
        return false;
    }

    // 4) 自定义 scheme: "kugou:1067062" / "netease:7480897649"
    size_t colon = input.find(':');
    if (colon != std::string::npos && colon < 10) {
        std::string pfx = low.substr(0, colon);
        if (pfx == "kugou" || pfx == "kg") {
            plat = Platform::Kugou; id = input.substr(colon + 1); return !id.empty();
        }
        if (pfx == "netease" || pfx == "wyy" || pfx == "163") {
            plat = Platform::NetEase; id = input.substr(colon + 1); return !id.empty();
        }
    }

    return false;
}

bool PlaylistProvider::FollowRedirect(const std::string& url, std::string& final_url,
                                      std::string* error, int max_hops) {
    std::string cur = url;
    for (int hop = 0; hop < max_hops; ++hop) {
        std::wstring wurl = ToWide(cur);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        const size_t cap = wurl.size() + 64;
        std::wstring hostbuf(256, L'\0'), pathbuf(cap, L'\0');
        uc.lpszHostName = &hostbuf[0];   uc.dwHostNameLength = 255;
        uc.lpszUrlPath = &pathbuf[0];    uc.dwUrlPathLength = (DWORD)(cap - 1);
        if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
            if (error) *error = "URL 解析失败";
            return false;
        }
        hostbuf.resize(uc.dwHostNameLength);
        pathbuf.resize(uc.dwUrlPathLength);
        bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        HINTERNET ses = WinHttpOpen(L"KugouPlayer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) { if (error) *error = "WinHttpOpen 失败"; return false; }
        WinHttpSetTimeouts(ses, 8000, 8000, 8000, 8000);
        HINTERNET con = WinHttpConnect(ses, hostbuf.c_str(), uc.nPort, 0);
        if (!con) { WinHttpCloseHandle(ses); if (error) *error = "连接失败"; return false; }
        HINTERNET req = WinHttpOpenRequest(con, L"GET", pathbuf.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
            if (error) *error = "请求创建失败"; return false; }
        // 不自动跟随, 手动读 Location
        DWORD opt = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &opt, sizeof(opt));
        WinHttpAddRequestHeaders(req, L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                                 (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

        bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                  WinHttpReceiveResponse(req, nullptr);
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);

        std::string location;
        if (ok && status >= 300 && status < 400) {
            DWORD llen = 0;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                nullptr, &llen, WINHTTP_NO_HEADER_INDEX);
            if (llen > 0) {
                std::wstring wloc(llen / sizeof(wchar_t), L'\0');
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        &wloc[0], &llen, WINHTTP_NO_HEADER_INDEX))
                    location = ToUtf8(TrimW(wloc));
            }
        }
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);

        if (!ok) { if (error) *error = "重定向请求失败"; return false; }
        if (location.empty()) { final_url = cur; return true; }

        // 相对跳转补全
        if (location.compare(0, 2, "//") == 0) {
            location = std::string(https ? "https:" : "http:") + location;
        } else if (location[0] == '/') {
            char scheme[16]{};
            _snprintf_s(scheme, sizeof(scheme), _TRUNCATE, "%s://", https ? "https" : "http");
            location = std::string(scheme) + ToUtf8(hostbuf) + location;
        }
        cur = location;
    }
    final_url = cur;
    return true;
}

bool PlaylistProvider::ResolveInput(const std::string& input, Platform& plat, std::string& id,
                                    std::string* error) const {
    if (!ParseInput(input, plat, id)) {
        if (error) *error = "无法识别的链接或 ID";
        return false;
    }
    // 平台未知 + 是纯数字 → 失败 (必须能判断平台)
    if (plat == Platform::Unknown) {
        // 可能输入是短链, 试着跟随重定向再判断一次
        if (input.find("://") != std::string::npos) {
            std::string fin;
            if (FollowRedirect(input, fin, error)) {
                Platform p2 = Platform::Unknown; std::string i2;
                if (ParseInput(fin, p2, i2) && p2 != Platform::Unknown) {
                    plat = p2; id = i2; return true;
                }
            }
        }
        if (error) *error = "无法判断所属平台 (纯数字 ID 需要指定平台)";
        return false;
    }
    return true;
}

// ============================================================================
//  酷狗: 网页版内嵌 global.data
// ============================================================================

bool PlaylistProvider::FetchKugou(const std::string& id, PlaylistResult& out) const {
    out.meta.source = "kugou";
    out.meta.id = id;

    // ⚠ 必须用 http 明文, 不能用 https:
    //   www2.kugou.kugou.com 是 CDN 节点, 其 TLS 证书 hostname 与域名不匹配
    //   (python ssl 严格校验报 Hostname mismatch; WinHTTP 报 12175
    //    ERROR_WINHTTP_SECURE_FAILURE, 且 SECURITY_FLAGS 对发送阶段失败无效)
    //   实测该页面 http 明文返回 200 且数据完整 —— 公开歌单数据无需加密传输。
    std::string url = "http://www2.kugou.kugou.com/yueku/v9/special/single/" + id + "-5-9999.html";
    std::string body;
    if (!http_(url, body)) {
        out.error = "酷狗页面请求失败 (网络或接口变更)";
        return false;
    }
    // 兼容: 若返回内容里没有 JSON, 可能是页面结构变了
    if (body.find("global.data") == std::string::npos) {
        out.error = "酷狗页面结构变更: 未找到 global.data";
        return false;
    }

    // ---- 抠曲目数组 ----
    size_t sp = body.find("global.data = [");
    if (sp == std::string::npos) { out.error = "未匹配到 global.data 数组"; return false; }
    sp += strlen("global.data = ");
    // 从 [ 开始做括号配对, 找匹配的 ]
    int depth = 0;
    bool instr = false, esc = false;
    size_t se = std::string::npos;
    for (size_t k = sp; k < body.size(); ++k) {
        char c = body[k];
        if (instr) {
            if (esc) { esc = false; }
            else if (c == '\\') { esc = true; }
            else if (c == '"') { instr = false; }
        } else {
            if (c == '"') instr = true;
            else if (c == '[') ++depth;
            else if (c == ']') { if (--depth == 0) { se = k + 1; break; } }
        }
    }
    if (se == std::string::npos) { out.error = "global.data 数组不完整"; return false; }

    std::string arr_str = body.substr(sp, se - sp);
    JsonParser jp(arr_str);
    JValPtr arr = jp.Parse();
    if (!arr || !arr->IsArr()) { out.error = "global.data 解析失败"; return false; }

    // ---- 抠歌单名/封面 ----
    {
        size_t gp = body.find("global = {");
        if (gp != std::string::npos) {
            size_t ge = body.find("};", gp);
            if (ge != std::string::npos) {
                std::string meta_str = body.substr(gp + strlen("global = "), ge - gp);
                std::regex re_name(R"(name:\s*"([^"]*)\")");
                std::regex re_pic(R"(pic:\s*"([^"]*)\")");
                std::smatch m;
                if (std::regex_search(meta_str, m, re_name)) out.meta.name = ToWide(m[1].str());
                if (std::regex_search(meta_str, m, re_pic))  out.meta.cover_url = m[1].str();
            }
        }
    }

    // ---- 逐条曲目 ----
    for (size_t k = 0; k < arr->Size(); ++k) {
        const JVal* s = arr->At(k);
        if (!s || !s->IsObj()) continue;
        PlaylistTrack t;
        t.source = "kugou";
        t.title  = StripHtml(JStr(s->Get("songname")));
        t.artist = StripHtml(JStr(s->Get("singername")));
        t.album  = JStr(s->Get("album_name"));
        // 时长: duration 为毫秒 (酷狗部分字段是秒 *1000 的整值)
        double dur = JNum(s->Get("duration"));
        t.duration_ms = (uint32_t)(dur > 0 ? (long long)(dur + 0.5) : 0);

        t.id = JStrRaw(s->Get("hash"));
        t.kugou_hash_320  = JStrRaw(s->Get("hash_320"));
        t.kugou_hash_flac = JStrRaw(s->Get("hash_flac"));
        t.kugou_audio_id  = JStrRaw(s->Get("audio_id"));
        t.kugou_album_id  = JStrRaw(s->Get("album_id"));

        if (t.title.empty()) continue;   // 无歌名视为无效条目
        if (t.duration_ms > 0) ++out.with_duration;
        if (!t.album.empty())  ++out.with_album;
        out.tracks.push_back(std::move(t));
    }

    out.meta.total = (int)out.tracks.size();
    if (out.tracks.empty()) { out.error = "歌单为空或全部条目无效"; return false; }
    out.ok = true;
    return true;
}

// ============================================================================
//  网易云: playlist/detail + song/detail 两步式
// ============================================================================

bool PlaylistProvider::FetchNetease(const std::string& id, PlaylistResult& out) const {
    out.meta.source = "netease";
    out.meta.id = id;

    // ---- 第一步: 取歌单元信息 + trackIds ----
    std::string url = "http://music.163.com/api/v6/playlist/detail?id=" + UrlEncode(id) +
                      "&n=1000&s=0&t=0";
    std::string body;
    if (!http_(url, body)) { out.error = "网易云歌单请求失败 (网络或接口变更)"; return false; }

    JsonParser jp(body);
    JValPtr root = jp.Parse();
    if (!root || !root->IsObj()) { out.error = "网易云歌单响应解析失败"; return false; }
    const JVal* pl = root->Get("playlist");
    if (!pl || !pl->IsObj()) {
        double code = JNum(root->Get("code"));
        out.error = (code == 404) ? "歌单不存在或已删除"
                                   : "网易云未返回歌单数据 (可能需要登录或接口变更)";
        return false;
    }

    out.meta.name = StripHtml(JStr(pl->Get("name")));
    out.meta.cover_url = JStrRaw(pl->Get("coverImgUrl"));
    out.meta.description = JStr(pl->Get("description"));
    if (const JVal* cr = pl->Get("creator"))
        out.meta.author = JStr(cr->Get("nickname"));
    out.meta.total = (int)JNum(pl->Get("trackCount"));

    // trackIds: 元素是对象 {"id":123,...} (也可能是裸数字, 两种都兼容)
    const JVal* tids = pl->Get("trackIds");
    std::vector<std::string> ids;
    if (tids && tids->IsArr()) {
        for (size_t k = 0; k < tids->Size(); ++k) {
            const JVal* e = tids->At(k);
            if (!e) continue;
            if (e->IsObj()) {
                double v = JNum(e->Get("id"));
                if (v > 0) ids.push_back(std::to_string((long long)v));
            } else if (e->IsNum()) {
                ids.push_back(std::to_string((long long)e->num));
            } else if (e->IsStr() && !e->str.empty()) {
                ids.push_back(e->str);
            }
        }
    }

    // 若 playlist 里直接带了 tracks (新版小节歌单可能只给前几首), 先收下
    if (const JVal* tr = pl->Get("tracks"); tr && tr->IsArr() && tr->Size() > 0) {
        for (size_t k = 0; k < tr->Size(); ++k) {
            const JVal* s = tr->At(k);
            if (!s || !s->IsObj()) continue;
            PlaylistTrack t;
            t.source = "netease";
            t.title  = StripHtml(JStr(s->Get("name")));
            const JVal* ar = s->Get("ar") ? s->Get("ar") : s->Get("artists");
            if (ar && ar->IsArr()) {
                std::wstring a;
                for (size_t j = 0; j < ar->Size(); ++j) {
                    const JVal* one = ar->At(j);
                    if (!one) continue;
                    if (!a.empty()) a += L" / ";
                    a += JStr(one->Get("name"));
                }
                t.artist = a;
            }
            const JVal* al = s->Get("al") ? s->Get("al") : s->Get("album");
            if (al) {
                t.album = JStr(al->Get("name"));
                t.netease_album_id = std::to_string((long long)JNum(al->Get("id")));
            }
            double dt = JNum(s->Get("dt"));
            if (dt <= 0) dt = JNum(s->Get("duration"));
            t.duration_ms = (uint32_t)(dt > 0 ? (long long)(dt + 0.5) : 0);
            t.id = std::to_string((long long)JNum(s->Get("id")));
            if (!t.title.empty()) out.tracks.push_back(std::move(t));
        }
    }

    // ---- 第二步: 分批补全 (每批 200, 实测安全) ----
    // 若第一步已把 tracks 全给全了, 可跳过; 这里以 trackIds 为准做完整补全
    if (!ids.empty()) {
        std::vector<PlaylistTrack> full;
        const size_t kBatch = 200;
        for (size_t start = 0; start < ids.size(); start += kBatch) {
            size_t end = std::min(start + kBatch, ids.size());
            std::string csv;
            for (size_t k = start; k < end; ++k) {
                if (!csv.empty()) csv += ",";
                csv += ids[k];
            }
            std::string u2 = "http://music.163.com/api/song/detail/?id=" + csv +
                             "&ids=" + UrlEncode("[" + csv + "]");
            std::string b2;
            if (!http_(u2, b2)) continue;   // 单批失败不致命, 继续下一批
            JsonParser jp2(b2);
            JValPtr r2 = jp2.Parse();
            if (!r2 || !r2->IsObj()) continue;
            const JVal* songs = r2->Get("songs");
            if (!songs || !songs->IsArr()) continue;
            for (size_t k = 0; k < songs->Size(); ++k) {
                const JVal* s = songs->At(k);
                if (!s || !s->IsObj()) continue;
                PlaylistTrack t;
                t.source = "netease";
                t.title  = StripHtml(JStr(s->Get("name")));
                const JVal* ar = s->Get("ar") ? s->Get("ar") : s->Get("artists");
                if (ar && ar->IsArr()) {
                    std::wstring a;
                    for (size_t j = 0; j < ar->Size(); ++j) {
                        const JVal* one = ar->At(j);
                        if (!one) continue;
                        if (!a.empty()) a += L" / ";
                        a += JStr(one->Get("name"));
                    }
                    t.artist = a;
                }
                const JVal* al = s->Get("al") ? s->Get("al") : s->Get("album");
                if (al) {
                    t.album = JStr(al->Get("name"));
                    t.netease_album_id = std::to_string((long long)JNum(al->Get("id")));
                }
                double dt = JNum(s->Get("dt"));
                if (dt <= 0) dt = JNum(s->Get("duration"));
                t.duration_ms = (uint32_t)(dt > 0 ? (long long)(dt + 0.5) : 0);
                t.id = std::to_string((long long)JNum(s->Get("id")));
                if (!t.title.empty()) full.push_back(std::move(t));
            }
        }
        // 补全结果优先 (字段更全); 若补全失败则保留第一步的 tracks
        if (!full.empty()) out.tracks.swap(full);
    }

    if (out.tracks.empty()) { out.error = "歌单为空或曲目全部无效"; return false; }

    out.with_duration = 0; out.with_album = 0;
    for (const auto& t : out.tracks) {
        if (t.duration_ms > 0) ++out.with_duration;
        if (!t.album.empty())  ++out.with_album;
    }
    // 元信息里的 total 若与实际不符 (歌单有下架曲目), 以实际为准但保留标称值
    out.ok = true;
    return true;
}

// ============================================================================
//  主入口
// ============================================================================

PlaylistResult PlaylistProvider::Parse(const std::string& input) const {
    Platform plat = Platform::Unknown;
    std::string id;
    std::string err;
    if (!ResolveInput(input, plat, id, &err)) {
        PlaylistResult r;
        r.error = err;
        return r;
    }
    return ParseAs(plat, id);
}

PlaylistResult PlaylistProvider::ParseAs(Platform plat, const std::string& id) const {
    PlaylistResult r;
    switch (plat) {
        case Platform::Kugou:   FetchKugou(id, r); break;
        case Platform::NetEase: FetchNetease(id, r); break;
        default:
            r.error = "平台未指定或不支持";
            break;
    }
    return r;
}

// ============================================================================
//  工具函数
// ============================================================================

PlaylistProvider::PlaylistProvider(HttpGetFn http)
    : http_(http ? http : &PlaylistProvider::DefaultHttpGet) {}

std::string PlaylistProvider::UrlEncode(const std::string& utf8) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(utf8.size() * 3);
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::wstring PlaylistProvider::Utf8ToWide(const std::string& s) { return ToWide(s); }
std::string PlaylistProvider::WideToUtf8(const std::wstring& s) { return ToUtf8(s); }
std::wstring PlaylistProvider::Trim(const std::wstring& s) { return TrimW(s); }

bool PlaylistProvider::DefaultHttpGet(const std::string& url, std::string& body) {
    std::wstring wurl = ToWide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    // ⚠ 缓冲区必须够大: 网易云 song/detail 一次拼 200 个 ID 时 URL 可达 ~5KB.
    //   本机旧实现用 path[4096]/extra[2048] → 长 URL 被静默截断 → 请求参数丢失.
    //   这里直接按 URL 长度动态分配。
    const size_t cap = wurl.size() + 64;
    std::wstring hostbuf(256, L'\0');
    std::wstring pathbuf(cap, L'\0');
    std::wstring extrabuf(cap, L'\0');
    uc.lpszHostName = &hostbuf[0];        uc.dwHostNameLength = 255;
    uc.lpszUrlPath = &pathbuf[0];         uc.dwUrlPathLength = (DWORD)(cap - 1);
    uc.lpszExtraInfo = &extrabuf[0];      uc.dwExtraInfoLength = (DWORD)(cap - 1);
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) return false;
    hostbuf.resize(uc.dwHostNameLength);
    pathbuf.resize(uc.dwUrlPathLength);
    extrabuf.resize(uc.dwExtraInfoLength);

    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring full_path = pathbuf + extrabuf;

    HINTERNET ses = WinHttpOpen(L"KugouPlayer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    WinHttpSetTimeouts(ses, 8000, 8000, 10000, 10000);

    HINTERNET con = WinHttpConnect(ses, hostbuf.c_str(), uc.nPort, 0);
    if (!con) { WinHttpCloseHandle(ses); return false; }

    HINTERNET req = WinHttpOpenRequest(con, L"GET", full_path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false; }

    WinHttpAddRequestHeaders(req, L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                 L"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36",
                             (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    // 网易云需要 Referer
    if (url.find("163.com") != std::string::npos)
        WinHttpAddRequestHeaders(req, L"Referer: https://music.163.com/",
                                 (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(req, nullptr);
    if (ok) {
        DWORD status = 0, slen = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
        ok = (status >= 200 && status < 300);
    }
    if (ok) {
        body.clear();
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; break; }
            if (avail == 0) break;
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(req, &chunk[0], avail, &read) || read == 0) { ok = false; break; }
            chunk.resize(read);
            body += chunk;
        } while (avail > 0);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

} // namespace playlist_core
