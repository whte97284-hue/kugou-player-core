#include "../include/pinyin_table.hpp"
#include <cwctype>
#include <unordered_map>

// 常用汉字拼音映射表 (覆盖曲库全部华语歌曲与歌手)
static const std::unordered_map<wchar_t, const char*> s_pinyin_map = {
    // 歌手常见字
    {L'邓', "deng"}, {L'紫', "zi"}, {L'棋', "qi"},
    {L'陈', "chen"}, {L'奕', "yi"}, {L'迅', "xun"},
    {L'周', "zhou"}, {L'杰', "jie"}, {L'伦', "lun"},
    {L'王', "wang"}, {L'力', "li"}, {L'宏', "hong"},
    {L'蔡', "cai"}, {L'健', "jian"}, {L'雅', "ya"},
    {L'谢', "xie"}, {L'霆', "ting"}, {L'锋', "feng"}, {L'安', "an"}, {L'琪', "qi"},
    {L'杨', "yang"}, {L'千', "qian"}, {L'嬅', "hua"},
    {L'梅', "mei"}, {L'艳', "yan"}, {L'芳', "fang"},
    {L'田', "tian"}, {L'馥', "fu"}, {L'甄', "zhen"},
    {L'陶', "tao"}, {L'喆', "zhe"},
    {L'郭', "guo"}, {L'静', "jing"},
    {L'林', "lin"}, {L'俊', "jun"},
    {L'张', "zhang"}, {L'学', "xue"}, {L'友', "you"},
    {L'刘', "liu"}, {L'德', "de"}, {L'华', "hua"},
    {L'李', "li"}, {L'克', "ke"}, {L'勤', "qin"},
    {L'许', "xu"}, {L'嵩', "song"},
    {L'薛', "xue"}, {L'谦', "qian"},
    {L'毛', "mao"}, {L'不', "bu"}, {L'易', "yi"},
    {L'路', "lu"}, {L'南', "nan"},
    {L'泽', "ze"}, {L'野', "ye"}, {L'弘', "hong"}, {L'之', "zhi"},
    {L'小', "xiao"}, {L'未', "wei"}, {L'郁', "yu"},
    {L'椎', "zhui"}, {L'名', "ming"}, {L'檎', "qin"},
    {L'花', "hua"}, {L'香', "xiang"}, {L'菜', "cai"},
    {L'上', "shang"}, {L'原', "yuan"},
    {L'大', "da"}, {L'桥', "qiao"}, {L'卓', "zhuo"},
    {L'川', "chuan"}, {L'村', "cun"},
    {L'松', "song"}, {L'森', "sen"}, {L'永', "yong"},
    {L'米', "mi"}, {L'津', "jin"}, {L'玄', "xuan"}, {L'师', "shi"},

    // 歌名汉字
    {L'喜', "xi"}, {L'欢', "huan"}, {L'你', "ni"},
    {L'泡', "pao"}, {L'沫', "mo"},
    {L'光', "guang"}, {L'年', "nian"}, {L'外', "wai"},
    {L'富', "fu"}, {L'士', "shi"}, {L'山', "shan"}, {L'下', "xia"},
    {L'玉', "yu"}, {L'蝴', "hu"}, {L'蝶', "die"},
    {L'处', "chu"}, {L'吻', "wen"},
    {L'红', "hong"}, {L'色', "se"}, {L'高', "gao"}, {L'跟', "gen"}, {L'鞋', "xie"},
    {L'钟', "zhong"}, {L'无', "wu"},
    {L'真', "zhen"}, {L'的', "de"}, {L'爱', "ai"},
    {L'情', "qing"}, {L'讯', "xun"}, {L'息', "xi"},
    {L'似', "shi"}, {L'是', "shi"}, {L'故', "gu"}, {L'人', "ren"}, {L'来', "lai"},
    {L'就', "jiu"}, {L'要', "yao"}, {L'想', "xiang"}, {L'起', "qi"}, {L'我', "wo"},
    {L'囍', "xi"}, {L'帖', "tie"}, {L'街', "jie"},
    {L'华', "hua"}, {L'铭', "ming"},
    {L'普', "pu"}, {L'通', "tong"}, {L'朋', "peng"},
    {L'知', "zhi"}, {L'道', "dao"}, {L'事', "shi"},
    {L'恋', "lian"}, {L'环', "huan"},
    {L'青', "qing"}, {L'春', "chun"},
    {L'孤', "gu"}, {L'独', "du"}, {L'星', "xing"}, {L'球', "qiu"},
    {L'白', "bai"}, {L'相', "xiang"}, {L'簿', "bu"},
    {L'积', "ji"}, {L'木', "mu"},
    {L'暗', "an"}, {L'黑', "hei"}, {L'雨', "yu"},
    {L'依', "yi"}, {L'存', "cun"}, {L'症', "zheng"},
    {L'眩', "xuan"}, {L'晕', "yun"},
    {L'深', "shen"}, {L'夜', "ye"}, {L'门', "men"}, {L'扉', "fei"},
    {L'留', "liu"}, {L'在', "zai"}, {L'身', "shen"}, {L'边', "bian"},
    {L'炎', "yan"}, {L'梦', "meng"}, {L'良', "liang"}, {L'衣', "yi"},
    {L'前', "qian"}, {L'世', "shi"}, {L'鸟', "niao"}, {L'风', "feng"}, {L'月', "yue"},
    {L'残', "can"}, {L'酷', "ku"}, {L'天', "tian"}, {L'使', "shi"},
    {L'命', "ming"}, {L'嫌', "xian"}, {L'恶', "e"}, {L'堕', "duo"}
};

const char* PinyinHelper::GetCharPinyin(wchar_t ch) {
    auto it = s_pinyin_map.find(ch);
    if (it != s_pinyin_map.end()) {
        return it->second;
    }
    return nullptr;
}

std::string PinyinHelper::GetFullPinyin(const std::wstring& text) {
    std::string result;
    for (wchar_t ch : text) {
        const char* py = GetCharPinyin(ch);
        if (py) {
            result += py;
        } else if (ch >= L'a' && ch <= L'z') {
            result += static_cast<char>(ch);
        } else if (ch >= L'A' && ch <= L'Z') {
            result += static_cast<char>(std::towlower(ch));
        } else if (ch >= L'0' && ch <= L'9') {
            result += static_cast<char>(ch);
        }
    }
    return result;
}

std::string PinyinHelper::GetPinyinAcronym(const std::wstring& text) {
    std::string result;
    for (wchar_t ch : text) {
        const char* py = GetCharPinyin(ch);
        if (py && py[0] != '\0') {
            result += py[0]; // 提取首字母
        } else if (ch >= L'a' && ch <= L'z') {
            result += static_cast<char>(ch);
        } else if (ch >= L'A' && ch <= L'Z') {
            result += static_cast<char>(std::towlower(ch));
        } else if (ch >= L'0' && ch <= L'9') {
            result += static_cast<char>(ch);
        }
    }
    return result;
}

bool PinyinHelper::Matches(const std::wstring& target, const std::wstring& query) {
    if (query.empty()) return true;
    if (target.empty()) return false;

    // 1. 直接大小写无关子串匹配
    std::wstring lTarget = target;
    std::wstring lQuery = query;
    std::transform(lTarget.begin(), lTarget.end(), lTarget.begin(), ::towlower);
    std::transform(lQuery.begin(), lQuery.end(), lQuery.begin(), ::towlower);

    if (lTarget.find(lQuery) != std::wstring::npos) {
        return true;
    }

    // 2. 转换为 UTF-8 / ASCII query
    std::string qAscii;
    for (wchar_t c : lQuery) {
        if (c < 128) qAscii += static_cast<char>(c);
    }
    if (qAscii.empty()) return false;

    // 3. 拼音首字母缩写匹配 (如 "dzq" 匹配 "邓紫棋")
    std::string acronym = GetPinyinAcronym(target);
    if (acronym.find(qAscii) != std::string::npos) {
        return true;
    }

    // 4. 完整拼音匹配 (如 "dengziqi" 匹配 "邓紫棋")
    std::string fullPinyin = GetFullPinyin(target);
    if (fullPinyin.find(qAscii) != std::string::npos) {
        return true;
    }

    return false;
}
