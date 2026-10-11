#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "cover_provider.h"
#include "library_service.h"
#include "models.h"
#include "player_controller.h"
#include "window_helper.h"

// 离屏截图驱动 (开发验证用):--screenshot <输出目录>
// 依次调用 QML 根对象上的自动化函数并截图,用于在无显示器环境下检查排版与配色
static void RunScreenshots(QQmlApplicationEngine& engine, LibraryService& lib, PlayerController& player, const QString& outDir) {
    QDir().mkpath(outDir);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
    if (!window) return;

    struct Step {
        QString name;
        std::function<void()> action;
        int waitMs;
    };
    auto steps = std::make_shared<std::vector<Step>>();
    auto call = [window](const char* fn, QVariant arg = {}) {
        if (arg.isValid()) QMetaObject::invokeMethod(window, fn, Q_ARG(QVariant, arg));
        else QMetaObject::invokeMethod(window, fn);
    };
    steps->push_back({"01_albums", [] {}, 1500});
    steps->push_back({"02_album_detail", [call] { call("automationOpenAlbum", 0); }, 1500});
    steps->push_back({"03_playing", [&player, call] {
        player.playAlbum(0, 0);
        call("automationOpenAlbum", 0);
    }, 2500});
    steps->push_back({"04_albums_playing", [call] { call("automationBack"); }, 1500});
    steps->push_back({"05_palette", [call] { call("automationSearch", QStringLiteral("chen")); }, 1200});
    steps->push_back({"06_chain", [call] { call("automationCloseSearch"); call("automationShowChain"); }, 1200});
    steps->push_back({"09_now_playing", [call] { call("automationHideChain"); call("automationNowPlaying", true); }, 2200});
    steps->push_back({"10_back_to_wall", [call] { call("automationNowPlaying", false); }, 1200});
    steps->push_back({"11_np_ultraviolet", [call] {
        call("automationPlayTitle", QStringLiteral("Ultraviolet"));
        call("automationNowPlaying", true);
    }, 2600});
    steps->push_back({"12_wall_neon", [call] {
        call("automationNowPlaying", false);
        call("automationPlayTitle", QStringLiteral("Neon Tide"));
    }, 2600});
    steps->push_back({"13_np_forest", [call] {
        call("automationPlayTitle", QStringLiteral("森林物语"));
        call("automationNowPlaying", true);
    }, 2600});
    steps->push_back({"14_wall_forest", [call] { call("automationNowPlaying", false); }, 1400});
    steps->push_back({"08_wall_end", [call] { call("automationHideChain"); call("automationScrollEnd"); }, 1500});
    steps->push_back({"07_small_window", [window, call] {
        call("automationHideChain");
        window->resize(980, 640);
    }, 1500});

    auto run = std::make_shared<std::function<void(size_t)>>();
    *run = [=](size_t i) {
        if (i >= steps->size()) {
            QCoreApplication::quit();
            return;
        }
        (*steps)[i].action();
        QTimer::singleShot((*steps)[i].waitMs, window, [=]() {
            QImage img = window->grabWindow();
            img.save(outDir + "/" + (*steps)[i].name + ".png");
            (*run)(i + 1);
        });
    };
    auto start = [run]() { (*run)(0); };
    if (lib.scanning()) QObject::connect(&lib, &LibraryService::scanningChanged, window, [&lib, start]() {
        if (!lib.scanning()) QTimer::singleShot(800, start);
    });
    else QTimer::singleShot(800, start);
}

int main(int argc, char* argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    app.setOrganizationName("KugouPlayer");
    app.setApplicationName("KugouPlayer");
    QQuickStyle::setStyle("Basic");

    QString screenshotDir, libraryArg;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--screenshot") screenshotDir = args[i + 1];
        if (args[i] == "--library") libraryArg = args[i + 1];
    }

    LibraryService library;
    PlayerController player(&library);
    AlbumModel albums(&library);
    TrackListModel albumTracks(&library);
    SearchModel search(&library);
    WindowHelper windowHelper;

    QQmlApplicationEngine engine;
    engine.addImageProvider("cover", new CoverProvider);
    QQmlContext* ctx = engine.rootContext();
    ctx->setContextProperty("library", &library);
    ctx->setContextProperty("player", &player);
    ctx->setContextProperty("albumModel", &albums);
    ctx->setContextProperty("albumTracks", &albumTracks);
    ctx->setContextProperty("searchModel", &search);
    ctx->setContextProperty("windowHelper", &windowHelper);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    // 兼容 Qt 6.4 (loadFromModule 自 6.5 起才有):资源前缀在 CMake 中显式固定为 /qt/qml
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PlayerUi/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    if (!libraryArg.isEmpty()) library.addFolder(libraryArg);
    else library.rescan();

    if (!screenshotDir.isEmpty()) RunScreenshots(engine, library, player, screenshotDir);
    return app.exec();
}
