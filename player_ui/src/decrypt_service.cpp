#include "decrypt_service.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <map>

#include "audio_ingestion_pipeline.hpp"
#include "decryptor.hpp"
#include "library_service.h"
#include "platform_io.hpp"

namespace {
QString Q(const std::wstring& w) { return QString::fromStdWString(w); }
QString FileName(const std::wstring& p) { return QFileInfo(Q(p)).fileName(); }
} // namespace

DecryptService::DecryptService(LibraryService* lib, QObject* parent) : QObject(parent), lib_(lib) {
    QSettings s;
    policy_ = std::clamp(s.value("decrypt/policy", 0).toInt(), 0, 2);
    outputDir_ = s.value("decrypt/outputDir").toString();
    if (outputDir_.isEmpty())
        outputDir_ = QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/Decrypted");
    loadSeen();
    connect(lib_, &LibraryService::pendingEncryptedChanged, this, &DecryptService::onPendingChanged);
}

DecryptService::~DecryptService() {
    if (cancel_) cancel_->store(true);
    if (worker_.joinable()) worker_.join();
}

void DecryptService::setPolicy(int p) {
    p = std::clamp(p, 0, 2);
    if (p == policy_) return;
    policy_ = p;
    QSettings().setValue("decrypt/policy", p);
    emit policyChanged();
}

void DecryptService::setOutputDir(const QString& d) {
    QString n = QDir::toNativeSeparators(QDir::cleanPath(d));
    if (n.isEmpty() || n == outputDir_) return;
    outputDir_ = n;
    QSettings().setValue("decrypt/outputDir", n);
    emit outputDirChanged();
}

void DecryptService::setOutputUrl(const QVariant& url) {
    QUrl u = url.toUrl();
    setOutputDir(u.isLocalFile() ? u.toLocalFile() : url.toString());
}

QVariantList DecryptService::decoders() const {
    QVariantList out;
    for (const auto& d : DecryptorRegistry::Instance().All()) {
        std::wstring reason;
        bool ok = d->IsAvailable(&reason);
        QStringList exts;
        for (const auto& e : d->Extensions()) exts << Q(e).mid(1).toUpper();
        QVariantMap m;
        m["id"] = QString::fromLatin1(d->Id());
        m["name"] = Q(d->DisplayName());
        m["exts"] = exts;
        m["native"] = d->Native();
        m["available"] = ok;
        m["reason"] = Q(reason);
        out.append(m);
    }
    return out;
}

QVariantMap DecryptService::pending() const {
    const auto& list = lib_->pendingEncrypted();
    std::map<std::string, int> counts;
    qint64 bytes = 0;
    for (const auto& e : list) {
        counts[e.kind]++;
        bytes += static_cast<qint64>(e.size);
    }
    QVariantList kinds;
    for (const auto& d : DecryptorRegistry::Instance().All()) {
        auto it = counts.find(d->Id());
        if (it == counts.end()) continue;
        QVariantMap k;
        k["id"] = QString::fromLatin1(d->Id());
        k["name"] = Q(d->DisplayName());
        k["count"] = it->second;
        k["available"] = d->IsAvailable();
        kinds.append(k);
    }
    QVariantMap m;
    m["total"] = static_cast<int>(list.size());
    m["bytes"] = bytes;
    m["kinds"] = kinds;
    return m;
}

namespace {
QString SeenFile() { return LibraryService::dataDir() + "/decrypt.seen"; }
} // namespace

void DecryptService::loadSeen() {
    QFile f(SeenFile());
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const QByteArray& line : f.readAll().split('\n'))
        if (!line.isEmpty()) seen_.insert(line.toUInt(nullptr, 16));
}

void DecryptService::saveSeen() const {
    QSaveFile f(SeenFile());
    if (!f.open(QIODevice::WriteOnly)) return;
    QByteArray out;
    for (uint h : seen_) out += QByteArray::number(h, 16) + '\n';
    f.write(out);
    f.commit();
}

void DecryptService::markSeen() {
    for (const auto& e : lib_->pendingEncrypted()) seen_.insert(qHash(Q(e.path)));
    saveSeen();
}

bool DecryptService::hasUnseen() const {
    for (const auto& e : lib_->pendingEncrypted())
        if (!seen_.contains(qHash(Q(e.path)))) return true;
    return false;
}

void DecryptService::onPendingChanged() {
    emit pendingChanged();
    if (lib_->pendingEncrypted().empty() || busy_ || !hasUnseen()) return;
    // 自动模式也只处理新出现的文件:解密失败的不会反复重试
    if (policy_ == 0) emit promptRequested();
    else if (policy_ == 1) decryptPending();
}

void DecryptService::dismissPending() { markSeen(); }

void DecryptService::decryptPending() {
    std::vector<Item> items;
    for (const auto& e : lib_->pendingEncrypted()) items.push_back({e.path, e.kind, e.size, e.mtime});
    markSeen();
    start(std::move(items), true);
}

int DecryptService::decryptUrls(const QVariantList& urls) {
    std::vector<Item> items;
    auto addFile = [&](const QString& path) {
        std::wstring w = QDir::toNativeSeparators(path).toStdWString();
        const Decryptor* d = DecryptorRegistry::Instance().Detect(w);
        if (!d) return;
        Item it;
        it.path = w;
        it.kind = d->Id();
        platform::GetFileInfo(w, it.size, it.mtime);
        items.push_back(std::move(it));
    };
    for (const QVariant& v : urls) {
        QUrl u = v.toUrl();
        QString local = u.isLocalFile() ? u.toLocalFile() : v.toString();
        QFileInfo fi(local);
        if (fi.isDir()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString f = it.next();
                if (IsEncryptedAudioExtension(("." + QFileInfo(f).suffix().toLower()).toStdWString())) addFile(f);
            }
        } else if (fi.isFile()) {
            addFile(fi.absoluteFilePath());
        }
    }
    const int n = static_cast<int>(items.size());
    if (n > 0) start(std::move(items), false);
    return n;
}

void DecryptService::cancel() {
    if (cancel_) cancel_->store(true);
}

void DecryptService::clearResults() {
    if (busy_) return;
    results_.clear();
    ok_ = fail_ = 0;
    total_ = 0;
    emit jobChanged();
}

void DecryptService::addOutputToLibrary() { lib_->addFolder(outputDir_); }

void DecryptService::openOutputFolder() {
    QDir().mkpath(outputDir_);
    QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir_));
}

void DecryptService::start(std::vector<Item> items, bool rescanAfter) {
    if (busy_ || items.empty()) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    total_ = static_cast<int>(items.size());
    done_ = 0;
    progress_ = 0;
    current_.clear();
    results_.clear();
    ok_ = fail_ = 0;
    emit jobChanged();
    emit progressChanged();

    cancel_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = cancel_;
    const std::wstring outDir = outputDir_.toStdWString();
    const std::wstring indexPath = (LibraryService::dataDir() + "/decrypt.index").toStdWString();
    QStringList folders = lib_->folders();
    QPointer<DecryptService> self(this);

    worker_ = std::thread([self, items = std::move(items), cancel, outDir, indexPath, rescanAfter, folders]() {
        DecryptIndex index;
        index.Load(indexPath);
        QElapsedTimer clock;
        clock.start();
        qint64 lastPost = -100;
        bool touchesLibrary = rescanAfter;
        const double n = static_cast<double>(items.size());
        for (size_t i = 0; i < items.size() && !cancel->load(); ++i) {
            const Item& it = items[i];
            const QString name = FileName(it.path);
            QMetaObject::invokeMethod(self, [self, name, i]() {
                if (!self) return;
                self->current_ = name;
                self->done_ = static_cast<int>(i);
                emit self->progressChanged();
            }, Qt::QueuedConnection);

            DecryptOptions o;
            o.output_dir = outDir;
            o.cancel = cancel.get();
            o.progress = [&](uint64_t done, uint64_t total) {
                qint64 now = clock.elapsed();
                if (now - lastPost < 50) return;
                lastPost = now;
                double v = (static_cast<double>(i) + (total ? double(done) / double(total) : 0.0)) / n;
                QMetaObject::invokeMethod(self, [self, v]() {
                    if (!self) return;
                    self->progress_ = v;
                    emit self->progressChanged();
                }, Qt::QueuedConnection);
            };
            const Decryptor* d = DecryptorRegistry::Instance().ById(it.kind);
            DecryptResult r;
            if (d) r = d->Decrypt(it.path, o);
            else r.message = L"未知格式";
            if (r.ok) {
                index.Put(it.path, it.size, it.mtime, r.output);
                // 源文件在曲库里:解密后需要重扫才能入库
                QString src = QDir::fromNativeSeparators(Q(it.path));
                for (const QString& f : folders)
                    if (src.startsWith(QDir::fromNativeSeparators(f) + "/", Qt::CaseInsensitive)) touchesLibrary = true;
            }
            QVariantMap m;
            m["name"] = name;
            m["kind"] = QString::fromStdString(it.kind);
            m["ok"] = r.ok;
            m["message"] = Q(r.message);
            m["output"] = Q(r.output);
            QMetaObject::invokeMethod(self, [self, m, i, n]() {
                if (!self) return;
                self->results_.append(m);
                (m.value("ok").toBool() ? self->ok_ : self->fail_)++;
                self->done_ = static_cast<int>(i) + 1;
                self->progress_ = (i + 1) / n;
                emit self->jobChanged();
                emit self->progressChanged();
            }, Qt::QueuedConnection);
        }
        index.Save(indexPath);
        QMetaObject::invokeMethod(self, [self, touchesLibrary]() {
            if (!self) return;
            self->busy_ = false;
            self->current_.clear();
            emit self->jobChanged();
            emit self->progressChanged();
            emit self->finished(self->ok_, self->fail_);
            if (touchesLibrary && self->ok_ > 0) self->lib_->rescan();
        }, Qt::QueuedConnection);
    });
}
