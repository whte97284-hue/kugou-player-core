#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>

class LibraryService;

// 设置:全部即时写入 QSettings 并即时生效 (界面缩放除外,需要重启)
// 使用方在用到时直接读 QSettings,因此这里只负责界面绑定与少量动作 (清缓存 / 重建 / 打开目录)
class SettingsService : public QObject {
    Q_OBJECT
    // ---- 播放 ----
    Q_PROPERTY(bool fade READ fade WRITE setFade NOTIFY changed)            // 暂停 / 继续时淡出淡入
    Q_PROPERTY(bool resume READ resume WRITE setResume NOTIFY changed)      // 启动时恢复上次的队列与位置
    Q_PROPERTY(int lyricsSource READ lyricsSource WRITE setLyricsSource NOTIFY changed) // 0 .lrc 优先 / 1 内嵌优先
    // ---- 曲库 ----
    Q_PROPERTY(int scanThreads READ scanThreads WRITE setScanThreads NOTIFY changed) // 0 = 自动
    Q_PROPERTY(bool splitCue READ splitCue WRITE setSplitCue NOTIFY changed)
    Q_PROPERTY(int codePage READ codePage WRITE setCodePage NOTIFY changed)          // 936 / 950 / 932
    Q_PROPERTY(bool readId3v1 READ readId3v1 WRITE setReadId3v1 NOTIFY changed)
    // 影响解析结果的设置改过、尚未重扫
    Q_PROPERTY(bool needRescan READ needRescan NOTIFY changed)
    // ---- 转换 ----
    Q_PROPERTY(bool writeTags READ writeTags WRITE setWriteTags NOTIFY changed)
    // ---- 外观 ----
    Q_PROPERTY(int accent READ accent WRITE setAccent NOTIFY changed)          // -1 跟随封面,0..n 固定色
    Q_PROPERTY(QVariantList accentPresets READ accentPresets CONSTANT)
    Q_PROPERTY(int background READ background WRITE setBackground NOTIFY changed) // 0 流动 / 1 静止 / 2 纯色
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(double uiScale READ uiScale WRITE setUiScale NOTIFY changed)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
    // ---- 存储 ----
    Q_PROPERTY(qint64 coverCacheBytes READ coverCacheBytes NOTIFY storageChanged)
    Q_PROPERTY(qint64 scanCacheBytes READ scanCacheBytes NOTIFY storageChanged)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit SettingsService(LibraryService* lib, QObject* parent = nullptr);

    bool fade() const { return get("player/fade", true).toBool(); }
    void setFade(bool v) { put("player/fade", v); }
    bool resume() const { return get("player/resume", true).toBool(); }
    void setResume(bool v) { put("player/resume", v); }
    int lyricsSource() const { return get("player/lyricsSource", 0).toInt(); }
    void setLyricsSource(int v) { put("player/lyricsSource", v); }

    int scanThreads() const { return get("library/threads", 0).toInt(); }
    void setScanThreads(int v) { put("library/threads", v); }
    bool splitCue() const { return get("library/splitCue", true).toBool(); }
    void setSplitCue(bool v);
    int codePage() const { return get("library/codePage", 936).toInt(); }
    void setCodePage(int v);
    bool readId3v1() const { return get("library/readId3v1", true).toBool(); }
    void setReadId3v1(bool v);
    bool needRescan() const { return needRescan_; }

    bool writeTags() const { return get("decrypt/writeTags", true).toBool(); }
    void setWriteTags(bool v) { put("decrypt/writeTags", v); }

    int accent() const { return get("ui/accent", -1).toInt(); }
    void setAccent(int v) { put("ui/accent", v); }
    QVariantList accentPresets() const;
    int background() const { return get("ui/background", 0).toInt(); }
    void setBackground(int v) { put("ui/background", v); }
    bool reducedMotion() const { return get("ui/reducedMotion", false).toBool(); }
    void setReducedMotion(bool v) { put("ui/reducedMotion", v); }
    double uiScale() const { return get("ui/scale", 1.0).toDouble(); }
    void setUiScale(double v);
    bool restartNeeded() const { return uiScale() != startScale_; }

    qint64 coverCacheBytes() const { return coverBytes_; }
    qint64 scanCacheBytes() const { return scanBytes_; }
    QString dataDir() const;
    QString version() const { return QStringLiteral("0.4"); }

    Q_INVOKABLE void refreshStorage();
    Q_INVOKABLE void clearCoverCache();
    // 删除扫描缓存并全量重扫 (解析相关设置改动后使用)
    Q_INVOKABLE void rebuildLibrary();
    Q_INVOKABLE void openDataDir();

    // 启动时读取界面缩放 (须在创建 QGuiApplication 之前调用)
    static double StartupScale();

signals:
    void changed();
    void storageChanged();

private:
    static QVariant get(const char* key, const QVariant& def) { return QSettings().value(key, def); }
    void put(const char* key, const QVariant& v);
    void markRescan();

    LibraryService* lib_;
    bool needRescan_ = false;
    double startScale_ = 1.0;
    qint64 coverBytes_ = 0;
    qint64 scanBytes_ = 0;
};
