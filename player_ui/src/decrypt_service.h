#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class LibraryService;

// 解密服务:把三个解密内核 (KGM / NCM / KGG) 以「工具」的形式提供给界面
//  · 扫描结束发现未解密文件时,按策略询问 (默认) / 自动解密 / 不打扰
//  · 工具页:拖入或选择文件 / 文件夹,解密到指定目录,可一键把产物目录加入曲库
//  · 后台单线程依次处理 (机械盘上顺序读写最快),可随时取消;产物写入索引,下次扫描直接入库
class DecryptService : public QObject {
    Q_OBJECT
    // 0 = 每次询问 / 1 = 自动解密 / 2 = 不解密
    Q_PROPERTY(int policy READ policy WRITE setPolicy NOTIFY policyChanged)
    Q_PROPERTY(QString outputDir READ outputDir WRITE setOutputDir NOTIFY outputDirChanged)
    // 工具列表:id / name / exts / native / available / reason
    Q_PROPERTY(QVariantList decoders READ decoders CONSTANT)
    // 曲库中待解密的文件:total / bytes / kinds:[{id, name, count, available}]
    Q_PROPERTY(QVariantMap pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY jobChanged)
    Q_PROPERTY(QString current READ current NOTIFY progressChanged)
    // 最近一次任务的结果:name / kind / ok / message / output
    Q_PROPERTY(QVariantList results READ results NOTIFY jobChanged)
    Q_PROPERTY(int okCount READ okCount NOTIFY jobChanged)
    Q_PROPERTY(int failCount READ failCount NOTIFY jobChanged)

public:
    explicit DecryptService(LibraryService* lib, QObject* parent = nullptr);
    ~DecryptService() override;

    int policy() const { return policy_; }
    void setPolicy(int p);
    QString outputDir() const { return outputDir_; }
    void setOutputDir(const QString& d);
    QVariantList decoders() const;
    QVariantMap pending() const;
    bool busy() const { return busy_; }
    double progress() const { return progress_; }
    int done() const { return done_; }
    int total() const { return total_; }
    QString current() const { return current_; }
    QVariantList results() const { return results_; }
    int okCount() const { return ok_; }
    int failCount() const { return fail_; }

    // 解密曲库扫描发现的全部待解密文件,完成后自动重扫曲库
    Q_INVOKABLE void decryptPending();
    // 本次不解密:同一批文件不再提示
    Q_INVOKABLE void dismissPending();
    // 工具页:文件或文件夹 (递归查找加密文件)
    Q_INVOKABLE int decryptUrls(const QVariantList& urls);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void addOutputToLibrary();
    Q_INVOKABLE void openOutputFolder();
    Q_INVOKABLE void clearResults();
    Q_INVOKABLE void setOutputUrl(const QVariant& url);

signals:
    void policyChanged();
    void outputDirChanged();
    void pendingChanged();
    void jobChanged();
    void progressChanged();
    // 扫描发现了未解密文件且策略为「询问」
    void promptRequested();
    void finished(int ok, int failed);

private:
    struct Item {
        std::wstring path;
        std::string kind;
        uint64_t size = 0;
        uint64_t mtime = 0;
    };
    void onPendingChanged();
    void start(std::vector<Item> items, bool rescanAfter);
    // 已经答复过 (拒绝或已尝试解密) 的源文件:只有出现新文件时才再次询问
    void markSeen();
    bool hasUnseen() const;
    void loadSeen();
    void saveSeen() const;

    LibraryService* lib_;
    int policy_ = 0;
    QString outputDir_;
    QSet<uint> seen_;
    bool busy_ = false;
    double progress_ = 0;
    int done_ = 0;
    int total_ = 0;
    QString current_;
    QVariantList results_;
    int ok_ = 0;
    int fail_ = 0;
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> cancel_;
};
