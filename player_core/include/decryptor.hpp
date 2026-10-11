#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
//  加密音频解密工具集
//   · kgm  酷狗 KGM / KGMA / VPR —— 原生 C++,kgm.mask 掩码表只映射一次、多线程共享
//   · ncm  网易云 NCM            —— 原生 C++ (AES 密钥块 → RC4 变体密钥流),产物自动写回标题/歌手/专辑/封面
//   · kgg  酷狗 KGG              —— 外部工具 kgg-dec.exe + infra.dll (仅 Windows);
//                                  源文件先复制到临时工作目录,绝不在曲库目录里写任何东西
//  三者实现同一个 Decryptor 接口,统一注册在 DecryptorRegistry 中:扫描流水线与界面的「工具」页都只认接口
// ============================================================================

enum class EncryptedKind { None, Kgm, Ncm, Kgg };

struct DecryptOptions {
    std::wstring output_dir;                      // 产物目录 (不存在时自动创建)
    bool write_tags = true;                       // NCM:把元数据与封面写入 FLAC / MP3 产物
    const std::atomic<bool>* cancel = nullptr;    // 置 true 时尽快中止 (不留半截产物)
    std::function<void(uint64_t done, uint64_t total)> progress; // 字节级进度 (可选)
};

struct DecryptResult {
    bool ok = false;
    std::wstring output;   // 产物完整路径
    std::wstring message;  // 失败原因 / 说明
};

class Decryptor {
public:
    virtual ~Decryptor() = default;
    virtual EncryptedKind Kind() const = 0;
    virtual const char* Id() const = 0;                     // "kgm" / "ncm" / "kgg"
    virtual std::wstring DisplayName() const = 0;
    virtual std::vector<std::wstring> Extensions() const = 0; // 小写含点,如 L".kgm"
    virtual bool Native() const = 0;                        // 原生实现 (否则依赖外部程序)
    // 当前环境能否使用 (掩码表 / 外部程序是否就位);不可用时 reason 给出原因
    virtual bool IsAvailable(std::wstring* reason = nullptr) const = 0;
    // 按文件头识别 (head 至少 16 字节时可靠)
    virtual bool Probe(const uint8_t* head, size_t n) const = 0;
    virtual DecryptResult Decrypt(const std::wstring& src, const DecryptOptions& opt) const = 0;
};

class DecryptorRegistry {
public:
    // 进程内单例,三个内置工具已注册
    static DecryptorRegistry& Instance();

    void Register(std::unique_ptr<Decryptor> d);
    const std::vector<std::unique_ptr<Decryptor>>& All() const { return list_; }
    const Decryptor* ById(const std::string& id) const;
    const Decryptor* ByExtension(const std::wstring& lowerExt) const;
    // 先按文件头识别 (改了扩展名的 KGM 也能认出),再退回扩展名
    const Decryptor* Detect(const std::wstring& path) const;

    // 外部资源位置:<tools>/kgm/kgm.mask、<tools>/kgg/kgg-dec.exe
    void SetToolsDir(const std::wstring& dir);
    std::wstring ToolsDir() const;
    // 显式指定 KGM 掩码表 (优先于 tools 目录)
    void SetKgmMaskPath(const std::wstring& path);
    std::wstring KgmMaskPath() const;

private:
    DecryptorRegistry();
    std::vector<std::unique_ptr<Decryptor>> list_;
    mutable std::mutex mtx_;
    std::wstring tools_dir_;
    std::wstring kgm_mask_;
};

// KGM 掩码解密内核 (单元测试用):out = x ^ (x << 4), x = c ^ key[i % 17] ^ mask[i % maskLen]
void KgmDecryptBlock(uint8_t* data, size_t len, uint64_t fileOffset, const uint8_t key17[17], const uint8_t* mask,
                     uint64_t maskLen);

// 识别扩展名是否为已知加密格式
bool IsEncryptedAudioExtension(const std::wstring& lowerExt);

// ============================================================================
//  解密产物索引:源文件 (路径 + 大小 + 修改时间) → 产物路径
//  扫描时据此把加密源直接映射为已解密的音频,无需再次解密;源文件变化后自动失效
// ============================================================================
class DecryptIndex {
public:
    bool Load(const std::wstring& file);
    bool Save(const std::wstring& file) const;
    // 命中且产物仍存在时返回产物路径,否则返回空
    std::wstring Find(const std::wstring& src, uint64_t size, uint64_t mtime) const;
    void Put(const std::wstring& src, uint64_t size, uint64_t mtime, const std::wstring& product);
    size_t Size() const;

private:
    struct Entry {
        uint64_t size = 0;
        uint64_t mtime = 0;
        std::wstring product;
    };
    mutable std::mutex mtx_;
    std::unordered_map<std::wstring, Entry> map_;
};
