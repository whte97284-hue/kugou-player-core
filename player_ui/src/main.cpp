#include <QDir>
#include <cmath>
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
#include "playlist_service.h"
#include "decrypt_service.h"
#include "settings_service.h"
#include "decryptor.hpp"
#include "window_helper.h"

// 离屏截图驱动 (开发验证用):--screenshot <输出目录>
// 依次调用 QML 根对象上的自动化函数并截图,用于在无显示器环境下检查排版与配色
// flowDir 非空时改为验证「首次使用」流程:空曲库引导 → 添加文件夹 → 识别进度 → 专辑墙 → 打开专辑点歌播放
static void RunScreenshots(QQmlApplicationEngine& engine, LibraryService& lib, PlayerController& player, const QString& outDir,
                           const QString& flowDir) {
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
    if (!flowDir.isEmpty()) {
        steps->push_back({"f1_empty", [] {}, 1200});
        steps->push_back({"f2_importing", [&lib, flowDir] { lib.addFolder(flowDir); }, 1100});
        steps->push_back({"f3_imported", [] {}, 4000});
        steps->push_back({"f4_folders", [call] { call("automationLibraryPanel", true); }, 900});
        steps->push_back({"f5_album", [call] {
            call("automationLibraryPanel", false);
            call("automationOpenAlbum", 1);
        }, 1400});
        steps->push_back({"f6_playing", [&player] { player.playAlbum(1, 2); }, 2200});
        steps->push_back({"f7_removed", [&lib, call] {
            call("automationBack");
            if (!lib.folders().isEmpty()) lib.removeFolder(lib.folders().first());
        }, 2500});
    } else {
    steps->push_back({"00_decrypt_prompt", [] {}, 900});
    steps->push_back({"01_albums", [call] { call("automationDecryptPrompt", false); }, 1500});
    steps->push_back({"02_album_detail", [call] { call("automationOpenAlbum", 0); }, 1500});
    // 封面飞回专辑墙的中途与落地 (检查飞行副本与卡片是否对齐、有无重影)
    steps->push_back({"02a_wall", [call] { call("automationBack"); }, 900});
    steps->push_back({"02a_card_album", [call] { call("automationOpenAlbum", 3); }, 1300});
    steps->push_back({"02b_close_mid", [call] { call("automationBack"); }, 260});
    steps->push_back({"02c_closed", [] {}, 900});
    steps->push_back({"02d_reopen", [call] { call("automationOpenAlbum", 0); }, 1300});
    steps->push_back({"03_playing", [&player, call] {
        player.playAlbum(0, 0);
    }, 2000});
    steps->push_back({"04_menu", [window] {
        QMetaObject::invokeMethod(window, "automationTrackMenu", Q_ARG(QVariant, 760), Q_ARG(QVariant, 420));
    }, 900});
    steps->push_back({"05_artists", [window, call] {
        QMetaObject::invokeMethod(window, "automationBack");
        call("automationGo", QStringLiteral("artists"));
    }, 1600});
    steps->push_back({"06_artist_detail", [call] { call("automationOpenArtist", QStringLiteral("久石让")); }, 1600});
    steps->push_back({"07_songs", [call] { call("automationGo", QStringLiteral("songs")); }, 1600});
    steps->push_back({"08_playlists", [&engine, &lib, call] {
        // 演示数据:两个歌单 + 几首收藏
        auto* pl = qobject_cast<PlaylistService*>(engine.rootContext()->contextProperty("playlists").value<QObject*>());
        auto snap = lib.snapshot();
        if (pl && pl->playlists().size() <= 1 && snap->songs.size() > 40) {
            QVariantList a, b;
            for (size_t i = 0; i < snap->songs.size(); i += 3) a.append(double(snap->songs[i]->id));
            for (size_t i = 1; i < 14; ++i) b.append(double(snap->songs[i * 2]->id));
            pl->addTracks(pl->create(QStringLiteral("深夜循环")), a);
            pl->addTracks(pl->create(QStringLiteral("通勤路上")), b);
            pl->create(QStringLiteral("空歌单"));
            for (int i = 5; i < 11; ++i) pl->toggleFavorite(double(snap->songs[i]->id));
        }
        call("automationGo", QStringLiteral("playlists"));
    }, 1400});
    steps->push_back({"08b_playlist_detail", [&engine, call] {
        auto* pl = qobject_cast<PlaylistService*>(engine.rootContext()->contextProperty("playlists").value<QObject*>());
        if (pl && pl->playlists().size() > 1) call("automationOpenPlaylist", pl->playlists()[1].toMap()["id"]);
    }, 1400});
    steps->push_back({"09_search", [call] { call("automationSearch", QStringLiteral("sen")); }, 1400});
    steps->push_back({"10_search_cn", [call] { call("automationSearch", QStringLiteral("陈奕迅")); }, 1400});
    steps->push_back({"11_now_playing", [call] {
        call("automationGo", QStringLiteral("albums"));
        call("automationPlayTitle", QStringLiteral("森林物语"));
        call("automationNowPlaying", true);
    }, 2400});
    steps->push_back({"12_wall", [call] { call("automationNowPlaying", false); }, 1400});
    steps->push_back({"13_wall_end", [call] { call("automationScrollEnd"); }, 1500});
    steps->push_back({"15_tools", [call] { call("automationGo", QStringLiteral("tools")); }, 1400});
    steps->push_back({"16_tools_decrypting", [&engine] {
        auto* d = qobject_cast<DecryptService*>(engine.rootContext()->contextProperty("decrypt").value<QObject*>());
        if (d) d->decryptPending();
    }, 60});
    steps->push_back({"17_tools_done", [] {}, 3000});
    steps->push_back({"18_songs_after_decrypt", [call] { call("automationGo", QStringLiteral("songs")); }, 1400});
    steps->push_back({"19_settings", [window, call] {
        window->resize(1320, 1180);
        call("automationGo", QStringLiteral("settings"));
    }, 1600});
    steps->push_back({"14_small_window", [window, call] {
        call("automationHideChain");
        window->resize(980, 640);
    }, 1500});
    }

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
    // 只在首次扫描完成后启动一次 (之后的重扫,例如解密完成触发的,不能再次启动截图序列)
    auto started = std::make_shared<bool>(false);
    auto once = [start, started]() {
        if (*started) return;
        *started = true;
        start();
    };
    if (!flowDir.isEmpty()) QTimer::singleShot(800, once);
    else if (lib.scanning()) QObject::connect(&lib, &LibraryService::scanningChanged, window, [&lib, once]() {
        if (!lib.scanning()) QTimer::singleShot(800, once);
    });
    else QTimer::singleShot(800, once);
}

int main(int argc, char* argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QCoreApplication::setOrganizationName("KugouPlayer");
    QCoreApplication::setApplicationName("KugouPlayer");
    // 界面缩放 (设置页):必须在创建应用对象之前生效
    const double scale = SettingsService::StartupScale();
    if (scale > 0.5 && scale < 3.0 && std::abs(scale - 1.0) > 0.01 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");

    QString screenshotDir, libraryArg, flowArg;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--screenshot") screenshotDir = args[i + 1];
        if (args[i] == "--library") libraryArg = args[i + 1];
        if (args[i] == "--flow") flowArg = args[i + 1];
    }

    // 解密工具的外部资源 (kgm.mask / kgg-dec.exe) 与程序放在一起
    DecryptorRegistry::Instance().SetToolsDir((QCoreApplication::applicationDirPath() + "/tools").toStdWString());
    LibraryService library;
    PlayerController player(&library);
    AlbumModel albums(&library);
    TrackListModel albumTracks(&library);
    SearchModel search(&library);
    PlaylistService playlists(&library);
    DecryptService decrypt(&library);
    SettingsService settings(&library);
    ArtistModel artists(&library);
    TrackCollectionModel songs(&library, &playlists);
    TrackCollectionModel artistTracks(&library, &playlists);
    TrackCollectionModel playlistTracks(&library, &playlists);
    artistTracks.setSource("artist");
    playlistTracks.setSource("playlist");
    WindowHelper windowHelper;

    QQmlApplicationEngine engine;
    engine.addImageProvider("cover", new CoverProvider);
    QQmlContext* ctx = engine.rootContext();
    ctx->setContextProperty("library", &library);
    ctx->setContextProperty("player", &player);
    ctx->setContextProperty("albumModel", &albums);
    ctx->setContextProperty("albumTracks", &albumTracks);
    ctx->setContextProperty("searchModel", &search);
    ctx->setContextProperty("playlists", &playlists);
    ctx->setContextProperty("decrypt", &decrypt);
    ctx->setContextProperty("settings", &settings);
    ctx->setContextProperty("artistModel", &artists);
    ctx->setContextProperty("songsModel", &songs);
    ctx->setContextProperty("artistTracks", &artistTracks);
    ctx->setContextProperty("playlistTracks", &playlistTracks);
    ctx->setContextProperty("windowHelper", &windowHelper);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    // 兼容 Qt 6.4 (loadFromModule 自 6.5 起才有):资源前缀在 CMake 中显式固定为 /qt/qml
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PlayerUi/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    player.attachWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)));

    if (!libraryArg.isEmpty()) library.addFolder(libraryArg);
    else library.rescan();

    if (!screenshotDir.isEmpty()) RunScreenshots(engine, library, player, screenshotDir, flowArg);
    return app.exec();
}
