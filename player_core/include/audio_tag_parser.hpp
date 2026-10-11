#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

struct AudioMetadata {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring album_artist;
    std::wstring genre;
    std::wstring year;
    std::wstring track_number;
    std::wstring comment;

    uint32_t duration_ms = 0;
    uint32_t bitrate_kbps = 0;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    std::wstring format; // "FLAC", "MP3", "OGG", "NCM", etc.

    // 专辑封面实体
    bool has_cover = false;
    std::string cover_mime_type; // "image/jpeg", "image/png"
    std::vector<uint8_t> cover_data; // 原始二进制图像数据
    uint32_t cover_width = 0;
    uint32_t cover_height = 0;
};

class AudioTagParser {
public:
    // 解析本地物理文件
    static bool ParseFile(const std::wstring& filePath, AudioMetadata& outMeta);

    // 解析内存流
    static bool ParseMemory(const uint8_t* data, size_t size, const std::wstring& hintExt, AudioMetadata& outMeta,
                            uint64_t fullFileSize = 0, const uint8_t* tailData = nullptr, size_t tailSize = 0);

    // 辅助编码转换
    static std::wstring Utf8ToWide(const std::string& str);
    static std::string WideToUtf8(const std::wstring& wstr);
    static std::wstring Utf16ToWide(const uint8_t* data, size_t len, bool bigEndian);
    static std::wstring AnsiToWide(const std::string& str, unsigned int codePage = 936);

private:
    static bool ParseFlac(const uint8_t* data, size_t size, AudioMetadata& outMeta, uint64_t fullFileSize = 0);
    static bool ParseMp3(const uint8_t* data, size_t size, AudioMetadata& outMeta, uint64_t fullFileSize = 0, const uint8_t* tailData = nullptr, size_t tailSize = 0);
    static bool ParseOgg(const uint8_t* data, size_t size, AudioMetadata& outMeta, uint64_t fullFileSize = 0, const uint8_t* tailData = nullptr, size_t tailSize = 0);
    static bool ParseNcm(const uint8_t* data, size_t size, AudioMetadata& outMeta);
    static bool ParseWav(const uint8_t* data, size_t size, AudioMetadata& outMeta, uint64_t fullFileSize = 0);
};
