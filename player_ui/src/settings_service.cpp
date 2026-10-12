#include "settings_service.h"

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThreadPool>
#include <QPointer>
#include <QUrl>

#include "library_service.h"

namespace {
QString CoverCacheDir() { return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs"; }
QString ScanCacheFile() { return LibraryService::dataDir() + "/scan.cache"; }

qint64 DirSize(const QString& dir) {
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
} // namespace

SettingsService::SettingsService(LibraryService* lib, QObject* parent) : QObject(parent), lib_(lib) {
    startScale_ = uiScale();
    needRescan_ = get("library/needRescan", false).toBool();
    // 扫描完成即视为已按新设置重扫
    connect(lib_, &LibraryService::scanningChanged, this, [this]() {
        if (!lib_->scanning() && needRescan_ && !lib_->lastScan().value("cancelled").toBool()) {
            needRescan_ = false;
            QSettings().setValue("library/needRescan", false);
            emit changed();
        }
        if (!lib_->scanning()) refreshStorage();
    });
    refreshStorage();
}

double SettingsService::StartupScale() { return QSettings().value("ui/scale", 1.0).toDouble(); }

void SettingsService::put(const char* key, const QVariant& v) {
    QSettings s;
    if (s.value(key) == v) return;
    s.setValue(key, v);
    emit changed();
}

void SettingsService::markRescan() {
    needRescan_ = true;
    QSettings().setValue("library/needRescan", true);
    emit changed();
}

void SettingsService::setSplitCue(bool v) {
    if (v == splitCue()) return;
    put("library/splitCue", v);
    markRescan();
}

void SettingsService::setCodePage(int v) {
    if (v == codePage()) return;
    put("library/codePage", v);
    markRescan();
}

void SettingsService::setReadId3v1(bool v) {
    if (v == readId3v1()) return;
    put("library/readId3v1", v);
    markRescan();
}

void SettingsService::setUiScale(double v) { put("ui/scale", v); }

QVariantList SettingsService::accentPresets() const {
    // 全部低饱和、中高明度:与暖炭黑底和谐,不出现电光色
    return {QColor("#c8a46e"), QColor("#c98b6b"), QColor("#9fb59a"), QColor("#c49a9e"), QColor("#a9a39a")};
}

QString SettingsService::dataDir() const { return QDir::toNativeSeparators(LibraryService::dataDir()); }

void SettingsService::refreshStorage() {
    QPointer<SettingsService> self(this);
    QThreadPool::globalInstance()->start([self]() {
        qint64 cover = DirSize(CoverCacheDir());
        qint64 scan = QFileInfo(ScanCacheFile()).size();
        QMetaObject::invokeMethod(self, [self, cover, scan]() {
            if (!self) return;
            self->coverBytes_ = cover;
            self->scanBytes_ = scan;
            emit self->storageChanged();
        }, Qt::QueuedConnection);
    });
}

void SettingsService::clearCoverCache() {
    QDir(CoverCacheDir()).removeRecursively();
    QDir().mkpath(CoverCacheDir());
    refreshStorage();
}

void SettingsService::rebuildLibrary() {
    QFile::remove(ScanCacheFile());
    lib_->rescan();
    refreshStorage();
}

void SettingsService::openDataDir() { QDesktopServices::openUrl(QUrl::fromLocalFile(LibraryService::dataDir())); }
