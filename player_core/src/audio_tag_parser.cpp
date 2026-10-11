#include "../include/audio_tag_parser.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <vector>

std::wstring AudioTagParser::Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int req = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
    if (req <= 0) return L"";
    std::wstring wstr(req, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], req);
    return wstr;
}

std::string AudioTagParser::WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int req = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    if (req <= 0) return "";
    std::string str(req, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &str[0], req, NULL, NULL);
    return str;
}

std::wstring AudioTagParser::AnsiToWide(const std::string& str, unsigned int codePage) {
    if (str.empty()) return L"";
    int req = MultiByteToWideChar(codePage, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
    if (req <= 0) return L"";
    std::wstring wstr(req, 0);
    MultiByteToWideChar(codePage, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], req);
    return wstr;
}

std::wstring AudioTagParser::Utf16ToWide(const uint8_t* data, size_t len, bool bigEndian) {
    if (len < 2) return L"";
    std::wstring result;
    size_t start = 0;
    // 跳过 BOM
    if (len >= 2) {
        if (data[0] == 0xFF && data[1] == 0xFE) {
            bigEndian = false;
            start = 2;
        } else if (data[0] == 0xFE && data[1] == 0xFF) {
            bigEndian = true;
            start = 2;
        }
    }
    for (size_t i = start; i + 1 < len; i += 2) {
        wchar_t ch = bigEndian ? ((data[i] << 8) | data[i + 1]) : (data[i] | (data[i + 1] << 8));
        if (ch == 0) break;
        result += ch;
    }
    return result;
}

static uint32_t ReadUint32BE(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static uint32_t ReadUint32LE(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static uint32_t ReadUint24BE(const uint8_t* p) {
    return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
}

static std::vector<uint8_t> Base64Decode(const std::string& in) {
    std::vector<uint8_t> out;
    std::vector<int> T(256, -1);
    const std::string b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) T[b[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(uint8_t((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

bool AudioTagParser::ParseFlac(const uint8_t* data, size_t size, AudioMetadata& outMeta, uint64_t fullFileSize) {
    if (size < 4 || std::memcmp(data, "fLaC", 4) != 0) return false;
    outMeta.format = L"FLAC";

    size_t offset = 4;
    bool isLast = false;

    while (offset + 4 <= size && !isLast) {
        uint8_t headerByte = data[offset];
        isLast = (headerByte & 0x80) != 0;
        uint8_t blockType = headerByte & 0x7F;
        uint32_t blockLen = ReadUint24BE(data + offset + 1);
        offset += 4;

        if (offset + blockLen > size) break;
        const uint8_t* blockData = data + offset;

        if (blockType == 0 && blockLen >= 34) {
            // STREAMINFO 提取硬件音频参数与物理样本总数
            uint32_t b10 = ReadUint32BE(blockData + 10);
            uint32_t b14 = ReadUint32BE(blockData + 14);
            
            outMeta.sample_rate = b10 >> 12;
            outMeta.channels = ((b10 >> 9) & 0x07) + 1;
            outMeta.bits_per_sample = ((b10 >> 4) & 0x1F) + 1;
            uint64_t totalSamples = (uint64_t(b10 & 0x0F) << 32) | b14;

            if (outMeta.sample_rate > 0) {
                outMeta.duration_ms = static_cast<uint32_t>((totalSamples * 1000) / outMeta.sample_rate);
            }
        } else if (blockType == 4 && blockLen >= 4) {
            // VORBIS_COMMENT
            size_t cOff = 0;
            uint32_t vendorLen = ReadUint32LE(blockData + cOff);
            cOff += 4 + vendorLen;

            if (cOff + 4 <= blockLen) {
                uint32_t numComments = ReadUint32LE(blockData + cOff);
                cOff += 4;

                for (uint32_t i = 0; i < numComments && cOff + 4 <= blockLen; ++i) {
                    uint32_t cLen = ReadUint32LE(blockData + cOff);
                    cOff += 4;
                    if (cOff + cLen > blockLen) break;

                    std::string comment(reinterpret_cast<const char*>(blockData + cOff), cLen);
                    cOff += cLen;

                    size_t eqPos = comment.find('=');
                    if (eqPos != std::string::npos) {
                        std::string key = comment.substr(0, eqPos);
                        std::string val = comment.substr(eqPos + 1);
                        std::transform(key.begin(), key.end(), key.begin(), ::toupper);

                        if (key == "TITLE") outMeta.title = Utf8ToWide(val);
                        else if (key == "ARTIST") outMeta.artist = Utf8ToWide(val);
                        else if (key == "ALBUM") outMeta.album = Utf8ToWide(val);
                        else if (key == "GENRE") outMeta.genre = Utf8ToWide(val);
                        else if (key == "DATE" || key == "YEAR") outMeta.year = Utf8ToWide(val);
                        else if (key == "TRACKNUMBER") outMeta.track_number = Utf8ToWide(val);
                    }
                }
            }
        } else if (blockType == 6 && blockLen >= 32) {
            // PICTURE (内嵌专辑封面)
            size_t pOff = 0;
            uint32_t picType = ReadUint32BE(blockData + pOff);
            pOff += 4;
            uint32_t mimeLen = ReadUint32BE(blockData + pOff);
            pOff += 4;

            if (pOff + mimeLen <= blockLen) {
                std::string mime(reinterpret_cast<const char*>(blockData + pOff), mimeLen);
                pOff += mimeLen;

                uint32_t descLen = ReadUint32BE(blockData + pOff);
                pOff += 4 + descLen;

                if (pOff + 16 <= blockLen) {
                    outMeta.cover_width = ReadUint32BE(blockData + pOff);
                    outMeta.cover_height = ReadUint32BE(blockData + pOff + 4);
                    pOff += 16;

                    if (pOff + 4 <= blockLen) {
                        uint32_t picDataLen = ReadUint32BE(blockData + pOff);
                        pOff += 4;
                        if (pOff + picDataLen <= blockLen && picDataLen > 0) {
                            outMeta.has_cover = true;
                            outMeta.cover_mime_type = mime;
                            outMeta.cover_data.assign(blockData + pOff, blockData + pOff + picDataLen);
                        }
                    }
                }
            }
        }

        offset += blockLen;
    }

    if (outMeta.duration_ms > 0 && outMeta.bitrate_kbps == 0 && fullFileSize > 0) {
        outMeta.bitrate_kbps = static_cast<uint32_t>((fullFileSize * 8) / outMeta.duration_ms);
    }
    return true;
}

bool AudioTagParser::ParseMp3(const uint8_t* data, size_t size, AudioMetadata& outMeta,
                             uint64_t fullFileSize, const uint8_t* tailData, size_t tailSize) {
    outMeta.format = L"MP3";
    outMeta.bits_per_sample = 16; // 感知编码标准基准 PCM 位深

    size_t audioScanOffset = 0;
    bool hasId3v1 = false;

    // 1. 检查并解析 ID3v1 (位于文件末尾 128 字节)
    const uint8_t* pId3v1 = nullptr;
    if (tailData != nullptr && tailSize >= 128) {
        if (std::memcmp(tailData + tailSize - 128, "TAG", 3) == 0) {
            pId3v1 = tailData + tailSize - 128;
            hasId3v1 = true;
        }
    } else if (size >= 128) {
        if (std::memcmp(data + size - 128, "TAG", 3) == 0) {
            pId3v1 = data + size - 128;
            hasId3v1 = true;
        }
    }

    // 2. 解析 ID3v2 标签头与内嵌信息
    if (size >= 10 && std::memcmp(data, "ID3", 3) == 0) {
        uint8_t version = data[3];
        uint8_t flags = data[5];
        uint32_t tagSize = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | ((data[8] & 0x7F) << 7) | (data[9] & 0x7F);
        size_t id3TotalHeaderLen = 10 + tagSize;
        if (flags & 0x10) {
            id3TotalHeaderLen += 10; // Footer present
        }
        audioScanOffset = id3TotalHeaderLen;

        size_t maxOffset = std::min<size_t>(size, 10 + tagSize);
        size_t offset = 10;

        while (offset + 10 <= maxOffset) {
            char frameId[5] = {0};
            std::memcpy(frameId, data + offset, 4);

            if (frameId[0] == 0) break; // 遇到填充 0x00

            uint32_t frameSize = (version == 4) ?
                (((data[offset + 4] & 0x7F) << 21) | ((data[offset + 5] & 0x7F) << 14) | ((data[offset + 6] & 0x7F) << 7) | (data[offset + 7] & 0x7F)) :
                ReadUint32BE(data + offset + 4);

            offset += 10;
            if (offset + frameSize > maxOffset || frameSize == 0) break;

            const uint8_t* fData = data + offset;
            std::string fId(frameId);

            if (fId[0] == 'T' && frameSize > 1) {
                uint8_t enc = fData[0];
                std::wstring textVal;
                if (enc == 0) {
                    textVal = AnsiToWide(std::string(reinterpret_cast<const char*>(fData + 1), frameSize - 1));
                } else if (enc == 1) {
                    textVal = Utf16ToWide(fData + 1, frameSize - 1, false);
                } else if (enc == 2) {
                    textVal = Utf16ToWide(fData + 1, frameSize - 1, true);
                } else if (enc == 3) {
                    textVal = Utf8ToWide(std::string(reinterpret_cast<const char*>(fData + 1), frameSize - 1));
                }

                while (!textVal.empty() && textVal.back() == L'\0') textVal.pop_back();

                if (fId == "TIT2") outMeta.title = textVal;
                else if (fId == "TPE1") outMeta.artist = textVal;
                else if (fId == "TALB") outMeta.album = textVal;
                else if (fId == "TCON") outMeta.genre = textVal;
                else if (fId == "TYER" || fId == "TDRC") outMeta.year = textVal;
                else if (fId == "TRCK") outMeta.track_number = textVal;
            } else if (fId == "APIC" && frameSize > 5) {
                uint8_t enc = fData[0];
                size_t p = 1;
                std::string mime;
                while (p < frameSize && fData[p] != 0) {
                    mime += static_cast<char>(fData[p++]);
                }
                p++;
                if (p < frameSize) {
                    uint8_t picType = fData[p++];
                    if (enc == 0 || enc == 3) {
                        while (p < frameSize && fData[p] != 0) p++;
                        p++;
                    } else {
                        while (p + 1 < frameSize && !(fData[p] == 0 && fData[p + 1] == 0)) p += 2;
                        p += 2;
                    }

                    if (p < frameSize) {
                        size_t imgBytes = frameSize - p;
                        outMeta.has_cover = true;
                        outMeta.cover_mime_type = mime.empty() ? "image/jpeg" : mime;
                        outMeta.cover_data.assign(fData + p, fData + p + imgBytes);
                    }
                }
            }

            offset += frameSize;
        }
    }

    // 若 ID3v2 缺失，回退使用 ID3v1
    if (pId3v1 != nullptr) {
        if (outMeta.title.empty()) {
            std::string t(reinterpret_cast<const char*>(pId3v1 + 3), 30);
            while (!t.empty() && (t.back() == ' ' || t.back() == 0)) t.pop_back();
            if (!t.empty()) outMeta.title = AnsiToWide(t);
        }
        if (outMeta.artist.empty()) {
            std::string a(reinterpret_cast<const char*>(pId3v1 + 33), 30);
            while (!a.empty() && (a.back() == ' ' || a.back() == 0)) a.pop_back();
            if (!a.empty()) outMeta.artist = AnsiToWide(a);
        }
        if (outMeta.album.empty()) {
            std::string al(reinterpret_cast<const char*>(pId3v1 + 63), 30);
            while (!al.empty() && (al.back() == ' ' || al.back() == 0)) al.pop_back();
            if (!al.empty()) outMeta.album = AnsiToWide(al);
        }
        if (outMeta.year.empty()) {
            std::string y(reinterpret_cast<const char*>(pId3v1 + 93), 4);
            while (!y.empty() && (y.back() == ' ' || y.back() == 0)) y.pop_back();
            if (!y.empty()) outMeta.year = AnsiToWide(y);
        }
    }

    // 3. 【真实物理管线】扫描 MPEG Audio Frame Header 提取流参数与计算真实时长
    // 从 audioScanOffset 开始，跨过可能的 0x00 padding
    for (size_t i = audioScanOffset; i + 4 <= size && i < audioScanOffset + 65536; ++i) {
        if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0) {
            uint8_t b1 = data[i + 1];
            uint8_t b2 = data[i + 2];
            uint8_t b3 = data[i + 3];

            uint8_t ver = (b1 >> 3) & 0x03; // 3: MPEG 1, 2: MPEG 2, 0: MPEG 2.5
            uint8_t layer = (b1 >> 1) & 0x03; // 1: Layer III
            if (ver == 1 || layer == 0) continue; // 非法保留位

            uint8_t brIdx = (b2 >> 4) & 0x0F;
            uint8_t srIdx = (b2 >> 2) & 0x03;
            if (brIdx == 0x0F || brIdx == 0 || srIdx == 0x03) continue;

            uint8_t padding = (b2 >> 1) & 0x01;
            uint8_t chMode = (b3 >> 6) & 0x03;

            int sampleRate = 0;
            if (ver == 3) { // MPEG 1
                static const int srTable[] = { 44100, 48000, 32000 };
                sampleRate = srTable[srIdx];
            } else if (ver == 2) { // MPEG 2
                static const int srTable[] = { 22050, 24000, 16000 };
                sampleRate = srTable[srIdx];
            } else if (ver == 0) { // MPEG 2.5
                static const int srTable[] = { 11025, 12000, 8000 };
                sampleRate = srTable[srIdx];
            }

            int bitrate = 0;
            if (ver == 3 && layer == 1) { // MPEG 1 Layer 3
                static const int brTable[] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
                bitrate = brTable[brIdx];
            } else if ((ver == 2 || ver == 0) && layer == 1) { // MPEG 2/2.5 Layer 3
                static const int brTable[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
                bitrate = brTable[brIdx];
            } else {
                static const int brTableL2[] = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 };
                bitrate = brTableL2[brIdx];
            }

            if (sampleRate == 0 || bitrate == 0) continue;

            outMeta.sample_rate = sampleRate;
            outMeta.channels = (chMode == 3) ? 1 : 2;
            outMeta.bitrate_kbps = bitrate;

            int samplesPerFrame = (ver == 3) ? 1152 : 576;
            if (layer == 3) samplesPerFrame = 384;

            // 检查首帧的 Xing / Info 头部
            size_t xingOffset = (ver == 3) ? (chMode == 3 ? 21 : 36) : (chMode == 3 ? 13 : 21);
            bool foundVbrHeader = false;

            if (i + xingOffset + 8 <= size) {
                if (std::memcmp(&data[i + xingOffset], "Xing", 4) == 0 ||
                    std::memcmp(&data[i + xingOffset], "Info", 4) == 0) {
                    foundVbrHeader = true;
                    uint32_t flags = ReadUint32BE(&data[i + xingOffset + 4]);
                    size_t p = i + xingOffset + 8;
                    if ((flags & 0x01) && p + 4 <= size) {
                        uint32_t frames = ReadUint32BE(&data[p]);
                        p += 4;
                        outMeta.duration_ms = static_cast<uint32_t>((static_cast<uint64_t>(frames) * samplesPerFrame * 1000) / sampleRate);
                    }
                    if ((flags & 0x02) && p + 4 <= size) {
                        uint32_t totalBytes = ReadUint32BE(&data[p]);
                        if (outMeta.duration_ms > 0 && totalBytes > 0) {
                            outMeta.bitrate_kbps = static_cast<uint32_t>((static_cast<uint64_t>(totalBytes) * 8) / outMeta.duration_ms);
                        }
                    }
                }
            }

            // 检查 VBRI 头部
            if (!foundVbrHeader && i + 36 + 18 <= size && std::memcmp(&data[i + 36], "VBRI", 4) == 0) {
                foundVbrHeader = true;
                uint32_t frames = ReadUint32BE(&data[i + 36 + 14]);
                outMeta.duration_ms = static_cast<uint32_t>((static_cast<uint64_t>(frames) * samplesPerFrame * 1000) / sampleRate);
                uint32_t totalBytes = ReadUint32BE(&data[i + 36 + 10]);
                if (outMeta.duration_ms > 0 && totalBytes > 0) {
                    outMeta.bitrate_kbps = static_cast<uint32_t>((static_cast<uint64_t>(totalBytes) * 8) / outMeta.duration_ms);
                }
            }

            // 若为纯 CBR 流（无 Xing/VBRI 头），依据有效净音频负载大小精确计算物理时长
            if (!foundVbrHeader && outMeta.duration_ms == 0) {
                uint64_t effectiveFileSize = fullFileSize > 0 ? fullFileSize : size;
                uint64_t id3v1Bytes = hasId3v1 ? 128 : 0;
                if (effectiveFileSize > i + id3v1Bytes) {
                    uint64_t audioPayloadBytes = effectiveFileSize - i - id3v1Bytes;
                    outMeta.duration_ms = static_cast<uint32_t>((audioPayloadBytes * 8 * 1000) / (static_cast<uint64_t>(bitrate) * 1000));
                }
            }

            return true;
        }
    }

    // 若未定位到帧头但 ID3v2 解析成功，仍标定成功
    return (audioScanOffset > 0);
}

bool AudioTagParser::ParseOgg(const uint8_t* data, size_t size, AudioMetadata& outMeta,
                             uint64_t fullFileSize, const uint8_t* tailData, size_t tailSize) {
    if (size < 4 || std::memcmp(data, "OggS", 4) != 0) return false;
    outMeta.format = L"OGG";
    outMeta.bits_per_sample = 16;

    // 1. 查找 \x01vorbis 识别头 (Identification Header)
    for (size_t i = 0; i + 30 <= size && i < 16384; ++i) {
        if (data[i] == 1 && std::memcmp(&data[i + 1], "vorbis", 6) == 0) {
            outMeta.channels = data[i + 11];
            outMeta.sample_rate = ReadUint32LE(&data[i + 12]);
            uint32_t brNom = ReadUint32LE(&data[i + 20]);
            if (brNom > 0) {
                outMeta.bitrate_kbps = brNom / 1000;
            }
            break;
        }
    }

    // 2. 查找 \x03vorbis 注释头 (Comment Header)
    for (size_t i = 0; i + 7 <= size && i < 65536; ++i) {
        if (data[i] == 3 && std::memcmp(&data[i + 1], "vorbis", 6) == 0) {
            size_t cOff = i + 7;
            if (cOff + 4 > size) break;
            uint32_t vendorLen = ReadUint32LE(data + cOff);
            cOff += 4 + vendorLen;

            if (cOff + 4 <= size) {
                uint32_t numComments = ReadUint32LE(data + cOff);
                cOff += 4;

                for (uint32_t c = 0; c < numComments && cOff + 4 <= size; ++c) {
                    uint32_t cLen = ReadUint32LE(data + cOff);
                    cOff += 4;
                    if (cOff + cLen > size) break;

                    std::string comment(reinterpret_cast<const char*>(data + cOff), cLen);
                    cOff += cLen;

                    size_t eqPos = comment.find('=');
                    if (eqPos != std::string::npos) {
                        std::string key = comment.substr(0, eqPos);
                        std::string val = comment.substr(eqPos + 1);
                        std::transform(key.begin(), key.end(), key.begin(), ::toupper);

                        if (key == "TITLE") outMeta.title = Utf8ToWide(val);
                        else if (key == "ARTIST") outMeta.artist = Utf8ToWide(val);
                        else if (key == "ALBUM") outMeta.album = Utf8ToWide(val);
                        else if (key == "GENRE") outMeta.genre = Utf8ToWide(val);
                        else if (key == "DATE" || key == "YEAR") outMeta.year = Utf8ToWide(val);
                        else if (key == "TRACKNUMBER") outMeta.track_number = Utf8ToWide(val);
                        else if (key == "METADATA_BLOCK_PICTURE" && !outMeta.has_cover) {
                            std::vector<uint8_t> picBlock = Base64Decode(val);
                            if (picBlock.size() >= 32) {
                                size_t pOff = 4; // skip type
                                uint32_t mimeLen = ReadUint32BE(picBlock.data() + pOff);
                                pOff += 4;
                                if (pOff + mimeLen <= picBlock.size()) {
                                    std::string mime(reinterpret_cast<const char*>(picBlock.data() + pOff), mimeLen);
                                    pOff += mimeLen;
                                    uint32_t descLen = ReadUint32BE(picBlock.data() + pOff);
                                    pOff += 4 + descLen + 16;
                                    if (pOff + 4 <= picBlock.size()) {
                                        uint32_t pLen = ReadUint32BE(picBlock.data() + pOff);
                                        pOff += 4;
                                        if (pOff + pLen <= picBlock.size() && pLen > 0) {
                                            outMeta.has_cover = true;
                                            outMeta.cover_mime_type = mime;
                                            outMeta.cover_data.assign(picBlock.data() + pOff, picBlock.data() + pOff + pLen);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
    }

    // 3. 扫描末尾 OggS 页面的 granule position 提取物理毫秒级时长
    const uint8_t* scanTail = (tailData != nullptr && tailSize > 0) ? tailData : data;
    size_t scanTailLen = (tailData != nullptr && tailSize > 0) ? tailSize : size;

    int64_t maxGranule = 0;
    if (scanTailLen >= 14) {
        for (size_t i = 0; i + 14 <= scanTailLen; ++i) {
            if (scanTail[i] == 'O' && scanTail[i+1] == 'g' && scanTail[i+2] == 'g' && scanTail[i+3] == 'S') {
                int64_t gran = 0;
                for (int b = 0; b < 8; ++b) {
                    gran |= (int64_t(scanTail[i + 6 + b]) << (b * 8));
                }
                if (gran > maxGranule && gran != -1) {
                    maxGranule = gran;
                }
            }
        }
    }

    if (maxGranule > 0 && outMeta.sample_rate > 0) {
        outMeta.duration_ms = static_cast<uint32_t>((maxGranule * 1000) / outMeta.sample_rate);
    }

    if (outMeta.duration_ms > 0 && outMeta.bitrate_kbps == 0 && fullFileSize > 0) {
        outMeta.bitrate_kbps = static_cast<uint32_t>((fullFileSize * 8) / outMeta.duration_ms);
    }

    return true;
}

bool AudioTagParser::ParseNcm(const uint8_t* data, size_t size, AudioMetadata& outMeta) {
    if (size < 10 || std::memcmp(data, "CTENFDAM", 8) != 0) return false;
    outMeta.format = L"NCM";

    size_t off = 10;
    if (off + 4 > size) return false;
    uint32_t keyLen = ReadUint32LE(data + off);
    off += 4 + keyLen;

    if (off + 4 > size) return false;
    uint32_t metaLen = ReadUint32LE(data + off);
    off += 4 + metaLen;

    off += 9; // CRC(4) + gap(5)
    if (off + 4 > size) return false;
    uint32_t imgLen = ReadUint32LE(data + off);
    off += 4;

    if (off + imgLen <= size && imgLen > 0) {
        outMeta.has_cover = true;
        outMeta.cover_mime_type = "image/jpeg";
        outMeta.cover_data.assign(data + off, data + off + imgLen);
    }
    return true;
}

bool AudioTagParser::ParseWav(const uint8_t* data, size_t size, AudioMetadata& outMeta, uint64_t fullFileSize) {
    if (size < 12) return false;
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return false;

    outMeta.format = L"WAV";

    size_t off = 12;
    uint32_t dataBytes = 0;
    uint32_t byteRate = 0;

    while (off + 8 <= size) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, data + off, 4);
        uint32_t chunkSize = ReadUint32LE(data + off + 4);
        off += 8;

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16 && off + 16 <= size) {
            outMeta.channels = data[off + 2] | (uint16_t(data[off + 3]) << 8);
            outMeta.sample_rate = ReadUint32LE(data + off + 4);
            byteRate = ReadUint32LE(data + off + 8);
            outMeta.bits_per_sample = data[off + 14] | (uint16_t(data[off + 15]) << 8);
            if (byteRate > 0) {
                outMeta.bitrate_kbps = (byteRate * 8) / 1000;
            }
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataBytes = chunkSize;
        } else if (std::memcmp(chunkId, "LIST", 4) == 0 && chunkSize >= 4 && off + 4 <= size) {
            if (std::memcmp(data + off, "INFO", 4) == 0) {
                size_t subOff = off + 4;
                size_t listEnd = off + chunkSize;
                while (subOff + 8 <= listEnd && subOff + 8 <= size) {
                    char subId[5] = {0};
                    std::memcpy(subId, data + subOff, 4);
                    uint32_t subSize = ReadUint32LE(data + subOff + 4);
                    subOff += 8;
                    if (subOff + subSize > size || subOff + subSize > listEnd) break;

                    std::string text(reinterpret_cast<const char*>(data + subOff), subSize);
                    while (!text.empty() && (text.back() == '\0' || text.back() == ' ')) text.pop_back();

                    if (std::memcmp(subId, "INAM", 4) == 0) outMeta.title = Utf8ToWide(text);
                    else if (std::memcmp(subId, "IART", 4) == 0) outMeta.artist = Utf8ToWide(text);
                    else if (std::memcmp(subId, "IPRD", 4) == 0) outMeta.album = Utf8ToWide(text);
                    else if (std::memcmp(subId, "IGNR", 4) == 0) outMeta.genre = Utf8ToWide(text);
                    else if (std::memcmp(subId, "ICRD", 4) == 0) outMeta.year = Utf8ToWide(text);

                    subOff += subSize;
                    if (subSize % 2 != 0) subOff++;
                }
            }
        } else if (std::memcmp(chunkId, "id3 ", 4) == 0 || std::memcmp(chunkId, "ID3 ", 4) == 0) {
            if (chunkSize > 10 && off + chunkSize <= size) {
                AudioMetadata id3Meta;
                if (ParseMp3(data + off, chunkSize, id3Meta)) {
                    if (outMeta.title.empty()) outMeta.title = id3Meta.title;
                    if (outMeta.artist.empty()) outMeta.artist = id3Meta.artist;
                    if (outMeta.album.empty()) outMeta.album = id3Meta.album;
                    if (outMeta.year.empty()) outMeta.year = id3Meta.year;
                    if (id3Meta.has_cover) {
                        outMeta.has_cover = true;
                        outMeta.cover_mime_type = id3Meta.cover_mime_type;
                        outMeta.cover_data = std::move(id3Meta.cover_data);
                    }
                }
            }
        }

        off += chunkSize;
        if (chunkSize % 2 != 0) off++;
    }

    if (byteRate > 0) {
        uint64_t targetDataSize = (dataBytes > 0) ? dataBytes : (fullFileSize > 44 ? (fullFileSize - 44) : 0);
        outMeta.duration_ms = static_cast<uint32_t>((targetDataSize * 1000) / byteRate);
    }

    return (outMeta.sample_rate > 0 && outMeta.channels > 0);
}

bool AudioTagParser::ParseMemory(const uint8_t* data, size_t size, const std::wstring& hintExt, AudioMetadata& outMeta,
                                uint64_t fullFileSize, const uint8_t* tailData, size_t tailSize) {
    if (size < 4) return false;

    if (std::memcmp(data, "fLaC", 4) == 0) {
        return ParseFlac(data, size, outMeta, fullFileSize);
    }
    if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0) {
        return ParseWav(data, size, outMeta, fullFileSize);
    }
    if (size >= 10 && std::memcmp(data, "ID3", 3) == 0) {
        return ParseMp3(data, size, outMeta, fullFileSize, tailData, tailSize);
    }
    if (size >= 8 && std::memcmp(data, "CTENFDAM", 8) == 0) {
        return ParseNcm(data, size, outMeta);
    }
    if (std::memcmp(data, "OggS", 4) == 0) {
        return ParseOgg(data, size, outMeta, fullFileSize, tailData, tailSize);
    }

    if (hintExt == L".flac") return ParseFlac(data, size, outMeta, fullFileSize);
    if (hintExt == L".wav") return ParseWav(data, size, outMeta, fullFileSize);
    if (hintExt == L".mp3") return ParseMp3(data, size, outMeta, fullFileSize, tailData, tailSize);
    if (hintExt == L".ogg") return ParseOgg(data, size, outMeta, fullFileSize, tailData, tailSize);
    if (hintExt == L".kgma" || hintExt == L".kgm") {
        outMeta.format = L"KGMA";
        outMeta.bits_per_sample = 16;
        return true;
    }
    return false;
}

bool AudioTagParser::ParseFile(const std::wstring& filePath, AudioMetadata& outMeta) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fSize;
    GetFileSizeEx(hFile, &fSize);
    uint64_t fullFileSize = static_cast<uint64_t>(fSize.QuadPart);

    size_t dot = filePath.rfind(L'.');
    std::wstring ext = (dot != std::wstring::npos) ? filePath.substr(dot) : L"";
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    // 智能预读：默认读取前 512KB，覆盖绝大部分元数据与 ID3/FLAC 标签
    DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(fullFileSize, 512 * 1024));
    std::vector<uint8_t> buffer(toRead);
    DWORD bytesRead = 0;

    if (!ReadFile(hFile, buffer.data(), toRead, &bytesRead, NULL) || bytesRead == 0) {
        CloseHandle(hFile);
        return false;
    }

    // 针对超大 ID3v2 标签（带高清大封面）的自适应扩容读取
    if (bytesRead >= 10 && std::memcmp(buffer.data(), "ID3", 3) == 0) {
        uint32_t tagSize = (uint32_t(buffer[6] & 0x7F) << 21) |
                           (uint32_t(buffer[7] & 0x7F) << 14) |
                           (uint32_t(buffer[8] & 0x7F) << 7) |
                           uint32_t(buffer[9] & 0x7F);
        uint32_t totalId3 = tagSize + 10;
        if (totalId3 > bytesRead && totalId3 <= 16 * 1024 * 1024 && totalId3 <= fullFileSize) {
            DWORD remaining = totalId3 - bytesRead;
            size_t oldSize = buffer.size();
            buffer.resize(totalId3);
            DWORD extraRead = 0;
            ReadFile(hFile, buffer.data() + oldSize, remaining, &extraRead, NULL);
            bytesRead += extraRead;
        }
    }

    // 针对 FLAC 超大 METADATA_BLOCK_PICTURE 的自适应补读
    if (bytesRead >= 4 && std::memcmp(buffer.data(), "fLaC", 4) == 0) {
        size_t off = 4;
        while (off + 4 <= bytesRead) {
            uint8_t headerByte = buffer[off];
            bool isLast = (headerByte & 0x80) != 0;
            uint32_t blockLen = ReadUint24BE(buffer.data() + off + 1);
            off += 4;
            if (off + blockLen > bytesRead) {
                uint32_t needed = static_cast<uint32_t>(off + blockLen);
                if (needed <= 16 * 1024 * 1024 && needed <= fullFileSize) {
                    DWORD remaining = needed - bytesRead;
                    size_t oldSize = buffer.size();
                    buffer.resize(needed);
                    DWORD extraRead = 0;
                    ReadFile(hFile, buffer.data() + oldSize, remaining, &extraRead, NULL);
                    bytesRead += extraRead;
                }
                break;
            }
            off += blockLen;
            if (isLast) break;
        }
    }

    // 仅在 OGG 格式下才读取末尾 64KB 用于 granule position 探测（FLAC/WAV/MP3 绝不跳末尾，免除磁盘寻道震荡）
    std::vector<uint8_t> tailBuffer;
    if (ext == L".ogg" && fullFileSize > bytesRead) {
        DWORD tailLen = static_cast<DWORD>(std::min<uint64_t>(fullFileSize - bytesRead, 65536));
        tailBuffer.resize(tailLen);
        LARGE_INTEGER tailPos;
        tailPos.QuadPart = fullFileSize - tailLen;
        SetFilePointerEx(hFile, tailPos, NULL, FILE_BEGIN);
        DWORD tailRead = 0;
        ReadFile(hFile, tailBuffer.data(), tailLen, &tailRead, NULL);
    }

    CloseHandle(hFile);

    bool ok = ParseMemory(buffer.data(), bytesRead, ext, outMeta, fullFileSize, tailBuffer.data(), tailBuffer.size());

    if (outMeta.duration_ms > 0 && outMeta.bitrate_kbps == 0 && fullFileSize > 0) {
        outMeta.bitrate_kbps = static_cast<uint32_t>((fullFileSize * 8) / outMeta.duration_ms);
    }

    return ok;
}
