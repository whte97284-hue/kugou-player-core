#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct KgmHeader {
    uint8_t  magic[16];     // 酷狗标识 Magic 头
    uint32_t header_len;    // 头部偏移大小 (通常为 0x400 / 1024 或 0x3C / 60)
    uint32_t crypto_ver;    // 加密版本
    uint32_t key_derive;    // 派生校验
    uint8_t  file_key[16];  // 16 字节专用异或 Key
};
#pragma pack(pop)

// 嗅探解密后的前导魔数，确定输出音频后缀名
std::wstring SniffAudioExtension(const uint8_t* buf, size_t len) {
    if (len >= 4 && buf[0] == 'f' && buf[1] == 'L' && buf[2] == 'a' && buf[3] == 'C') {
        return L".flac";
    }
    if (len >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        return L".mp3";
    }
    if (len >= 2 && buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0) {
        return L".mp3";
    }
    if (len >= 4 && buf[0] == 'O' && buf[1] == 'g' && buf[2] == 'g' && buf[3] == 'S') {
        return L".ogg";
    }
    return L".flac"; // 默认无损
}

class MaskMapper {
private:
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMapping = NULL;
    const uint8_t* pData = nullptr;
    uint32_t len = 0;

public:
    MaskMapper(const std::wstring& maskPath) {
        hFile = CreateFileW(maskPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;

        LARGE_INTEGER size;
        if (!GetFileSizeEx(hFile, &size)) return;
        len = static_cast<uint32_t>(size.QuadPart);

        hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMapping) return;

        pData = static_cast<const uint8_t*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0));
    }

    ~MaskMapper() {
        if (pData) UnmapViewOfFile(pData);
        if (hMapping) CloseHandle(hMapping);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }

    bool IsValid() const { return pData != nullptr && len > 0; }
    const uint8_t* GetData() const { return pData; }
    uint32_t GetLength() const { return len; }
};

bool ProcessSingleFile(const fs::path& inputPath, const MaskMapper& mask, const fs::path& outputDir, fs::path& finalOutputPath) {
    if (!mask.IsValid()) return false;

    // 1. 打开源物理文件 (宽字符支持中文特殊路径)
    HANDLE hIn = CreateFileW(inputPath.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hIn == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[ERROR] 无法打开输入文件: " << inputPath.wstring() << std::endl;
        return false;
    }

    // 2. 读取并验证头部
    KgmHeader header;
    DWORD bytesRead = 0;
    if (!ReadFile(hIn, &header, sizeof(KgmHeader), &bytesRead, NULL) || bytesRead < sizeof(KgmHeader)) {
        std::wcerr << L"[ERROR] 文件头部损坏或过小: " << inputPath.wstring() << std::endl;
        CloseHandle(hIn);
        return false;
    }

    // 3. 构建 17 字节扩展密钥 (最后 1 字节补 0)
    uint8_t extKey[17];
    std::memcpy(extKey, header.file_key, 16);
    extKey[16] = 0x00;

    // 4. 定位密文音频起始偏移
    LARGE_INTEGER offset;
    offset.QuadPart = header.header_len;
    if (!SetFilePointerEx(hIn, offset, NULL, FILE_BEGIN)) {
        CloseHandle(hIn);
        return false;
    }

    // 5. 循环解密缓冲区
    const size_t CHUNK_SIZE = 128 * 1024; // 128KB 块
    std::vector<uint8_t> buffer(CHUNK_SIZE);

    if (!ReadFile(hIn, buffer.data(), static_cast<DWORD>(CHUNK_SIZE), &bytesRead, NULL) || bytesRead == 0) {
        CloseHandle(hIn);
        return false;
    }

    const uint8_t* pMaskData = mask.GetData();
    uint32_t maskLen = mask.GetLength();

    // 解密第 1 块以嗅探输出扩展名
    for (DWORD i = 0; i < bytesRead; ++i) {
        uint8_t k = extKey[i % 17];
        uint8_t m = pMaskData[i % maskLen];
        uint8_t c = buffer[i];
        uint8_t temp = c ^ k;
        uint8_t low = (m & 0x0F) ^ (temp & 0x0F);
        uint8_t high = (temp >> 4) ^ (m >> 4) ^ low;
        buffer[i] = ((high & 0x0F) << 4) | (low & 0x0F);
    }

    std::wstring ext = SniffAudioExtension(buffer.data(), bytesRead);
    std::wstring stem = inputPath.stem().wstring();
    fs::path targetPath = outputDir / (stem + ext);

    // 6. 创建目标输出文件
    HANDLE hOut = CreateFileW(targetPath.wstring().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[ERROR] 无法创建输出目标: " << targetPath.wstring() << std::endl;
        CloseHandle(hIn);
        return false;
    }

    DWORD bytesWritten = 0;
    WriteFile(hOut, buffer.data(), bytesRead, &bytesWritten, NULL);
    uint64_t totalBytes = bytesRead;

    // 7. 处理后续全部音频密文流
    while (ReadFile(hIn, buffer.data(), static_cast<DWORD>(CHUNK_SIZE), &bytesRead, NULL) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; ++i) {
            uint64_t curOffset = totalBytes + i;
            uint8_t k = extKey[curOffset % 17];
            uint8_t m = pMaskData[curOffset % maskLen];
            uint8_t c = buffer[i];
            uint8_t temp = c ^ k;
            uint8_t low = (m & 0x0F) ^ (temp & 0x0F);
            uint8_t high = (temp >> 4) ^ (m >> 4) ^ low;
            buffer[i] = ((high & 0x0F) << 4) | (low & 0x0F);
        }
        WriteFile(hOut, buffer.data(), bytesRead, &bytesWritten, NULL);
        totalBytes += bytesRead;
    }

    CloseHandle(hIn);
    CloseHandle(hOut);

    finalOutputPath = targetPath;
    std::wcout << L"[SUCCESS] 解码成功: " << inputPath.filename().wstring()
               << L" -> " << targetPath.filename().wstring()
               << L" (" << totalBytes << L" 字节)" << std::endl;
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    // 设置控制台 UTF-8 输出
    SetConsoleOutputCP(CP_UTF8);

    fs::path maskPath;
    fs::path inputPath;
    fs::path outputDir;

    if (argc >= 4) {
        // 用法 1: kgm-dec-cpp.exe <input_path> <mask_path> <output_dir>
        inputPath = argv[1];
        maskPath = argv[2];
        outputDir = argv[3];
    } else {
        // 用法 2: 缺省模式 (兼容原 Go 版 CLI 工作方式: 当前目录 kgm.mask，输出至 ./kgm-vpr-out)
        maskPath = L"./kgm.mask";
        outputDir = L"./kgm-vpr-out";
        if (argc >= 2) {
            inputPath = argv[1];
        } else {
            inputPath = L".";
        }
    }

    if (!fs::exists(maskPath)) {
        std::wcerr << L"[ERROR] 找不到掩码文件: " << maskPath.wstring() << std::endl;
        return 1;
    }

    MaskMapper mask(maskPath.wstring());
    if (!mask.IsValid()) {
        std::wcerr << L"[ERROR] 加载掩码失败，请检查文件权限与完整性" << std::endl;
        return 1;
    }

    fs::create_directories(outputDir);

    int successCount = 0;
    int failCount = 0;

    if (fs::is_directory(inputPath)) {
        for (const auto& entry : fs::directory_iterator(inputPath)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                if (ext == ".kgm" || ext == ".kgma" || ext == ".vpr") {
                    fs::path out;
                    if (ProcessSingleFile(entry.path(), mask, outputDir, out)) {
                        successCount++;
                    } else {
                        failCount++;
                    }
                }
            }
        }
    } else if (fs::is_regular_file(inputPath)) {
        fs::path out;
        if (ProcessSingleFile(inputPath, mask, outputDir, out)) {
            successCount++;
        } else {
            failCount++;
        }
    } else {
        std::wcerr << L"[ERROR] 无效的输入路径: " << inputPath.wstring() << std::endl;
        return 1;
    }

    std::wcout << L"处理完成: 成功 " << successCount << L", 失败 " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
