#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

class PinyinHelper {
public:
    // 获取中文字符串的完整拼音 (小写无音调，如 "邓紫棋" -> "dengziqi")
    static std::string GetFullPinyin(const std::wstring& text);

    // 获取拼音首字母缩写 (小写，如 "邓紫棋" -> "dzq")
    static std::string GetPinyinAcronym(const std::wstring& text);

    // 判断搜索词 query 是否与 target 匹配 (支持中文、全拼、拼音首字母匹配)
    static bool Matches(const std::wstring& target, const std::wstring& query);

private:
    static const char* GetCharPinyin(wchar_t ch);
};
