#include "frame_diag.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QSysInfo>
#include <algorithm>
#include <atomic>

namespace {
QStringList g_graphicsLines;
// Qt 动画驱动的模式:vsync (每帧按一个刷新周期推进动画时间,最顺) / timer (按挂钟时间推进,会抖)。
// 连续几帧超过 1.25 个刷新周期 (按主屏刷新率算) 它就切到 timer,要连续 10 帧准时才切回来
std::atomic<int> g_driverTimer{0};
std::atomic<int> g_driverSwitches{0};
QtMessageHandler g_prevHandler = nullptr;

void CaptureHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (ctx.category && qstrcmp(ctx.category, "qt.scenegraph.general") == 0) {
        if (msg.contains(QLatin1String("switched to timer mode"))) { g_driverTimer = 1; ++g_driverSwitches; return; }
        if (msg.contains(QLatin1String("switched to vsync mode"))) { g_driverTimer = 0; ++g_driverSwitches; return; }
        if (msg.contains(QLatin1String("using walltime"))) g_driverTimer = 1;
        if (g_graphicsLines.size() < 40) g_graphicsLines << msg.trimmed();
        return; // 启动信息只收进诊断,不往控制台刷
    }
    if (g_prevHandler) g_prevHandler(type, ctx, msg);
}

double Pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * v.size()))];
}

QString ApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
        case QSGRendererInterface::Software: return QStringLiteral("Software");
        case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
        case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
        case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
        case QSGRendererInterface::Metal: return QStringLiteral("Metal");
        default: return QStringLiteral("未知");
    }
}
} // namespace

void FrameDiag::CaptureGraphicsInfo() {
    QLoggingCategory::setFilterRules(QStringLiteral("qt.scenegraph.general=true"));
    g_prevHandler = qInstallMessageHandler(CaptureHandler);
}

FrameDiag::FrameDiag(QObject* parent) : QObject(parent) {
    clock_.start();
    probe_.setTimerType(Qt::PreciseTimer);
    probe_.setInterval(2);
    connect(&probe_, &QTimer::timeout, this, &FrameDiag::tick);
    report_.setInterval(250);
    connect(&report_, &QTimer::timeout, this, &FrameDiag::publish);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    logPath_ = QDir(dir).filePath(QStringLiteral("frame_diag.log"));
}

void FrameDiag::attach(QQuickWindow* window) {
    window_ = window;
    if (!window) return;
    // 渲染线程上的信号:直接连接,只记时间戳
    connect(window, &QQuickWindow::frameSwapped, this, [this]() {
        if (!enabled_) return;
        std::lock_guard<std::mutex> l(mtx_);
        swaps_.push_back(clock_.nsecsElapsed());
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRendering, this, [this]() {
        if (enabled_) renderStart_ = clock_.nsecsElapsed();
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering, this, [this]() {
        if (!enabled_ || !renderStart_) return;
        std::lock_guard<std::mutex> l(mtx_);
        renderMs_.push_back((clock_.nsecsElapsed() - renderStart_) / 1e6);
    }, Qt::DirectConnection);
}

QString FrameDiag::graphics() const {
    QString api = window_ && window_->rendererInterface() ? ApiName(window_->rendererInterface()->graphicsApi()) : QString();
    // 适配器名在 scenegraph 启动日志里 (如 "Adapter 0: 'NVIDIA GeForce RTX 3060' (vendor 0x10de ...) active swapchain")
    QString adapter;
    for (const QString& l : g_graphicsLines) {
        if (l.contains(QStringLiteral("(using)")) || l.contains(QStringLiteral("active")) || l.startsWith(QStringLiteral("GL_RENDERER"))) {
            adapter = l;
            break;
        }
    }
    if (adapter.isEmpty()) {
        for (const QString& l : g_graphicsLines)
            if (l.contains(QStringLiteral("Adapter")) || l.contains(QStringLiteral("GL_RENDERER"))) { adapter = l; break; }
    }
    return adapter.isEmpty() ? api : api + QStringLiteral(" · ") + adapter;
}

void FrameDiag::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    {
        std::lock_guard<std::mutex> l(mtx_);
        swaps_.clear();
        renderMs_.clear();
    }
    second_.clear();
    secondRender_.clear();
    recent_.clear();
    lastSwap_ = 0;
    stallMax_ = stallWindowMax_ = secondStall_ = 0;
    if (on) {
        lastProbe_ = clock_.nsecsElapsed();
        probe_.start();
        report_.start();
        log_.setFileName(logPath_);
        if (log_.open(QIODevice::Append | QIODevice::Text)) {
            QString head = QStringLiteral("\n==== %1  %2  Qt %3\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                                                        QSysInfo::prettyProductName(), QString::fromLatin1(qVersion()));
            head += QStringLiteral("graphics: %1\n").arg(graphics());
            for (const QString& l : g_graphicsLines) head += QStringLiteral("  sg: %1\n").arg(l);
            if (window_ && window_->screen())
                head += QStringLiteral("screen: %1  %2 Hz  dpr %3\n").arg(window_->screen()->name())
                            .arg(window_->screen()->refreshRate()).arg(window_->devicePixelRatio());
            for (const QScreen* sc : QGuiApplication::screens())
                head += QStringLiteral("  monitor %1%2: %3 Hz %4x%5\n").arg(sc->name(), sc == QGuiApplication::primaryScreen() ? " (primary)" : "")
                            .arg(sc->refreshRate()).arg(sc->size().width()).arg(sc->size().height());
            head += QStringLiteral("time      fps  p50   p99   max   janks render gui_stall  state\n");
            log_.write(head.toUtf8());
            log_.flush();
        }
    } else {
        probe_.stop();
        report_.stop();
        log_.close();
    }
    emit enabledChanged();
    emit statsChanged();
}

void FrameDiag::tick() {
    const qint64 now = clock_.nsecsElapsed();
    const double late = (now - lastProbe_) / 1e6 - probe_.interval();
    lastProbe_ = now;
    if (late > stallWindowMax_) stallWindowMax_ = late;
    if (late > secondStall_) secondStall_ = late;
    ++stateSamples_;
    for (auto it = state_.cbegin(); it != state_.cend(); ++it)
        if (it.value().typeId() == QMetaType::Bool && it.value().toBool()) ++stateOn_[it.key()];
}

void FrameDiag::publish() {
    std::vector<qint64> swaps;
    std::vector<double> render;
    {
        std::lock_guard<std::mutex> l(mtx_);
        swaps.swap(swaps_);
        render.swap(renderMs_);
    }
    for (qint64 t : swaps) {
        if (lastSwap_) {
            const double d = (t - lastSwap_) / 1e6;
            // 静止时不出帧是正常的:超过 250ms 的间隔视为空闲,不计入
            if (d < 250) {
                second_.push_back(d);
                recent_.append(d);
            }
        }
        lastSwap_ = t;
    }
    while (recent_.size() > 120) recent_.removeFirst();
    secondRender_.insert(secondRender_.end(), render.begin(), render.end());

    const double hz = window_ && window_->screen() ? window_->screen()->refreshRate() : 60.0;
    const double frame = 1000.0 / (hz > 1 ? hz : 60.0);
    std::vector<double> recentVals;
    for (const QVariant& v : recent_) recentVals.push_back(v.toDouble());
    int janks = 0;
    for (double d : recentVals) janks += d > frame * 1.5;
    QVariantMap s;
    s["fps"] = recentVals.empty() ? 0 : qRound(1000.0 / std::max(1.0, Pct(recentVals, 0.5)));
    s["p50"] = Pct(recentVals, 0.5);
    s["p99"] = Pct(recentVals, 0.99);
    s["max"] = recentVals.empty() ? 0.0 : *std::max_element(recentVals.begin(), recentVals.end());
    s["janks"] = janks;
    s["hz"] = hz;
    s["frame"] = frame;
    s["stall"] = stallWindowMax_;
    s["render"] = render.empty() ? 0.0 : Pct(render, 0.9);
    s["timerMode"] = g_driverTimer.load() != 0;
    s["switches"] = g_driverSwitches.load();
    const QScreen* primary = QGuiApplication::primaryScreen();
    s["primaryHz"] = primary ? primary->refreshRate() : 0.0;
    stats_ = s;
    stallWindowMax_ = 0;
    emit statsChanged();

    if (++reports_ % 4 != 0) return;
    // 每秒一行日志
    if (log_.isOpen() && !second_.empty()) {
        int j = 0;
        for (double d : second_) j += d > frame * 1.5;
        QStringList st;
        // 布尔状态按本秒内为真的比例写:全程为真只写名字,部分时间为真写百分比
        for (auto it = state_.cbegin(); it != state_.cend(); ++it) {
            const QVariant& v = it.value();
            if (v.typeId() == QMetaType::Bool) {
                const int on = stateOn_.value(it.key());
                if (!on || !stateSamples_) continue;
                const int pct = qRound(100.0 * on / stateSamples_);
                st << (pct >= 98 ? it.key() : QStringLiteral("%1:%2%").arg(it.key()).arg(pct));
            } else if (!v.toString().isEmpty()) {
                st << it.key() + "=" + v.toString();
            }
        }
        if (window_ && !window_->isActive()) st << QStringLiteral("inactive");
        if (g_driverTimer.load()) st << QStringLiteral("DRIVER=timer");
        const QString line = QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8  %9\n")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
            .arg(second_.size(), 4)
            .arg(Pct(second_, 0.5), 5, 'f', 1)
            .arg(Pct(second_, 0.99), 5, 'f', 1)
            .arg(*std::max_element(second_.begin(), second_.end()), 5, 'f', 1)
            .arg(j, 5)
            .arg(Pct(secondRender_, 0.9), 6, 'f', 1)
            .arg(secondStall_, 8, 'f', 1)
            .arg(st.join(' '));
        log_.write(line.toUtf8());
        log_.flush();
    }
    second_.clear();
    secondRender_.clear();
    secondStall_ = 0;
    stateOn_.clear();
    stateSamples_ = 0;
}
