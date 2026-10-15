#include <QDir>
#include <cmath>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFileInfo>
#include <QFile>
#include <QPainter>

#include "cover_provider.h"
#include "library_service.h"
#include "models.h"
#include "player_controller.h"
#include "playlist_service.h"
#include "decrypt_service.h"
#include "settings_service.h"
#include "decryptor.hpp"
#include "window_helper.h"
#include "library_tools.h"
#include "taskbar_helper.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cwchar>
#endif
#include <QIcon>
#include <QThreadPool>

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
    if (qEnvironmentVariableIsSet("KP_TEST_ORGANIZE")) {
        // 回归:整理 (移动) 后立即点播放、切歌、查重处理后再播放,不应崩溃
        auto tools = [&engine]() { return qobject_cast<LibraryTools*>(engine.rootContext()->contextProperty("libtools").value<QObject*>()); };
        steps->push_back({"o1_playing", [&player] { player.playAlbum(0, 0); }, 800});
        steps->push_back({"o2_planned", [tools, outDir] {
            tools()->setTargetUrl(QDir(outDir).absoluteFilePath("organized"));
            tools()->buildPlan();
        }, 3000});
        steps->push_back({"o3_executed", [tools] { tools()->execute(); }, 8000});
        steps->push_back({"o4_play_after", [&player] { player.playAlbum(2, 1); }, 1500});
        steps->push_back({"o5_next", [&player] { player.next(); player.next(); }, 1500});
        steps->push_back({"o6_dedupe", [tools, call] { call("automationGo", QStringLiteral("dedupe")); tools()->findDuplicates(); }, 6000});
        steps->push_back({"o7_applied", [tools] { tools()->applyDedupe(1); }, 6000});
        steps->push_back({"o8_play_after", [&player] { player.playAlbum(1, 0); player.next(); }, 2000});
    } else if (qEnvironmentVariableIsSet("KP_TEST_FLIGHT")) {
        // 打开 / 收起播放页的过程:封面从底栏黑胶飞出、投影与内容的出场时机
        steps->push_back({"f00_playing", [call] { call("automationPlayTitle", QStringLiteral("async")); }, 1800});
        steps->push_back({"f01_open_60", [call] { call("automationNowPlaying", true); }, 60});
        steps->push_back({"f02_open_mid", [] {}, 120});
        steps->push_back({"f03_open_late", [] {}, 160});
        steps->push_back({"f04_open_land", [] {}, 200});
        steps->push_back({"f05_open_done", [] {}, 900});
        steps->push_back({"f06_close_60", [call] { call("automationNowPlaying", false); }, 60});
        steps->push_back({"f07_close_mid", [] {}, 120});
        steps->push_back({"f08_close_late", [] {}, 160});
        steps->push_back({"f09_close_done", [] {}, 900});
    } else if (qEnvironmentVariableIsSet("KP_TEST_ALBUM")) {
        // 专辑页:详情 / 播放中 / 滚动后的精简栏 / 页尾 / 专辑菜单 / 封面大图 / 多碟 / 宽窗与小窗
        const QString hires = qEnvironmentVariable("KP_ALBUM_A", QStringLiteral("async"));
        const QString multi = qEnvironmentVariable("KP_ALBUM_B", QStringLiteral("Joe Hisaishi Best"));
        steps->push_back({"a01_detail", [call, hires] { call("automationOpenAlbumTitle", hires); }, 1600});
        steps->push_back({"a02_playing", [call, hires] { call("automationPlayTitle", hires); }, 1800});
        steps->push_back({"a03_scrolled", [call] { call("automationDetailScroll", 420); }, 900});
        steps->push_back({"a04_footer", [call] { call("automationDetailScroll", -1); }, 1200});
        steps->push_back({"a05_menu", [call] { call("automationDetailScroll", 0); call("automationAlbumMenu"); }, 900});
        steps->push_back({"a06_cover", [call] { call("automationCloseMenu"); call("automationViewCover", true); }, 1200});
        steps->push_back({"a07_multidisc", [call, multi] {
            call("automationViewCover", false);
            call("automationBack");
            call("automationOpenAlbumTitle", multi);
        }, 1800});
        steps->push_back({"a07b_multidisc_scroll", [call] { call("automationDetailScroll", 300); }, 900});
        steps->push_back({"a08_wide", [window, call, hires] {
            window->resize(1920, 1080);
            call("automationBack");
            call("automationOpenAlbumTitle", hires);
        }, 2000});
        steps->push_back({"a09_wall_wide", [call] { call("automationBack"); }, 1600});
        steps->push_back({"a10_small", [window, call, hires] {
            window->resize(980, 640);
            call("automationOpenAlbumTitle", hires);
        }, 2000});
        steps->push_back({"a11_wall_small", [call] { call("automationBack"); }, 1600});
    } else if (qEnvironmentVariableIsSet("KP_TEST_WALL")) {
        // 专辑墙:今日推荐 / 排序菜单 / 按年份 (年代索引) / 筛选 / 播放与暂停 / 卡片菜单 / 重扫后位置 / 封面大小 / 小窗
        steps->push_back({"w01_wall", [call] { call("automationGo", QStringLiteral("albums")); }, 1600});
        steps->push_back({"w02_sortmenu", [call] { call("automationWallSortMenu", true); }, 800});
        steps->push_back({"w03_year", [call] { call("automationWallSortMenu", false); call("automationWallSort", 2); }, 1400});
        steps->push_back({"w04_scrolled", [call] { call("automationWallScroll", 520); }, 900});
        steps->push_back({"w05_lossy", [call] { call("automationWallScroll", 0); call("automationWallFilter", 3); }, 1400});
        steps->push_back({"w06_playing", [call, &player] {
            call("automationWallFilter", 0);
            call("automationWallSort", 0);
            player.playAlbum(0, 0);
        }, 1800});
        steps->push_back({"w07_paused", [&player] { player.toggle(); }, 1200});
        steps->push_back({"w08_card_menu", [call] { call("automationWallMenu", 1); }, 900});
        steps->push_back({"w09_rescan_kept", [call, &lib] {
            call("automationCloseMenu");
            call("automationWallScroll", 600);
            lib.rescan();
        }, 6000});
        steps->push_back({"w10_small_cells", [call] { call("automationWallScroll", 0); call("automationAlbumCell", 128); }, 1400});
        steps->push_back({"w11_big_cells", [call] { call("automationAlbumCell", 272); }, 1400});
        steps->push_back({"w12_small_window", [window, call] {
            call("automationAlbumCell", 176);
            call("automationWallSort", 1);
            window->resize(980, 640);
        }, 1600});
        steps->push_back({"w13_wide", [window] { window->resize(1920, 1080); }, 1600});
    } else if (qEnvironmentVariableIsSet("KP_TEST_PAGES")) {
        // 歌手 / 歌单 / 歌曲 / 搜索页:播放中的暂停键、滚动后的精简栏、整组「更多」菜单、搜索最佳匹配与无结果
        const QString artist = qEnvironmentVariable("KP_PAGES_ARTIST", QStringLiteral("久石譲"));
        // 页面上的自动化函数 (按 objectName 找到页面根对象)
        auto pageCall = [window](const char* page, const char* fn, QVariant arg = {}) {
            QObject* o = window->findChild<QObject*>(QLatin1String(page));
            if (QObject* root = window->property("contentItem").value<QObject*>(); !o && root) o = root->findChild<QObject*>(QLatin1String(page));
            if (!o) return;
            if (arg.isValid()) QMetaObject::invokeMethod(o, fn, Q_ARG(QVariant, arg));
            else QMetaObject::invokeMethod(o, fn);
        };
        steps->push_back({"g01_artists", [call] { call("automationGo", QStringLiteral("artists")); }, 1600});
        steps->push_back({"g02_artist", [call, artist] { call("automationOpenArtist", artist); }, 1600});
        steps->push_back({"g03_artist_playing", [pageCall] { pageCall("artistDetailPage", "playOrToggle"); }, 1800});
        steps->push_back({"g04_artist_bar", [pageCall] { pageCall("artistDetailPage", "automationScroll", 760); }, 900});
        steps->push_back({"g05_artist_menu", [pageCall] {
            pageCall("artistDetailPage", "automationScroll", 0);
            pageCall("artistDetailPage", "automationMenu");
        }, 900});
        steps->push_back({"g06_artists_back", [call] { call("automationCloseMenu"); call("automationBack"); }, 1600});
        steps->push_back({"g07_playlists", [&engine, &lib, call] {
            auto* pl = qobject_cast<PlaylistService*>(engine.rootContext()->contextProperty("playlists").value<QObject*>());
            auto snap = lib.snapshot();
            if (pl && pl->playlists().size() <= 1 && snap->songs.size() > 40) {
                QVariantList a;
                for (size_t i = 0; i < 68; ++i) a.append(double(snap->songs[i * 3]->id));
                pl->addTracks(pl->create(QStringLiteral("深夜循环")), a);
                pl->create(QStringLiteral("空歌单"));
            }
            call("automationGo", QStringLiteral("playlists"));
        }, 1400});
        steps->push_back({"g08_playlist", [&engine, call] {
            auto* pl = qobject_cast<PlaylistService*>(engine.rootContext()->contextProperty("playlists").value<QObject*>());
            if (pl && pl->playlists().size() > 1) call("automationOpenPlaylist", pl->playlists()[1].toMap()["id"]);
        }, 1400});
        steps->push_back({"g09_playlist_bar", [pageCall] { pageCall("playlistDetailPage", "automationScroll", 700); }, 900});
        steps->push_back({"g10_playlist_menu", [pageCall] {
            pageCall("playlistDetailPage", "automationScroll", 0);
            pageCall("playlistDetailPage", "automationMenu");
        }, 900});
        steps->push_back({"g11_empty_playlist", [&engine, call] {
            call("automationCloseMenu");
            auto* pl = qobject_cast<PlaylistService*>(engine.rootContext()->contextProperty("playlists").value<QObject*>());
            if (!pl) return;
            for (const QVariant& v : pl->playlists())
                if (v.toMap()["count"].toInt() == 0) { call("automationOpenPlaylist", v.toMap()["id"]); break; }
        }, 1400});
        steps->push_back({"g12_songs", [call] { call("automationGo", QStringLiteral("songs")); }, 1600});
        steps->push_back({"g13_search_artist", [call, artist] { call("automationSearch", artist); }, 1400});
        steps->push_back({"g14_search_track", [call] { call("automationSearch", QStringLiteral("summer")); }, 1400});
        steps->push_back({"g15_search_none", [call] { call("automationSearch", QStringLiteral("qqxxzz")); }, 1400});
        steps->push_back({"g16_small_artist", [window, call, artist] {
            window->resize(980, 640);
            call("automationOpenArtist", artist);
        }, 1800});
    } else if (qEnvironmentVariableIsSet("KP_TEST_CHROME")) {
        // 播放队列抽屉 / 队列右键菜单与键盘操作 / 清空后撤销 / 沉浸页 / 歌曲信息 / 定时关闭与音量 / 窄窗播放条 / 顶部磨砂条
        auto key = [window](int k) {
            QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, k, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            QCoreApplication::sendEvent(window, &release);
        };
        auto wheel = [window](int dy) {
            const QPointF pos(window->width() * 0.55, window->height() * 0.45);
            QWheelEvent ev(pos, window->mapToGlobal(pos), QPoint(), QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &ev);
        };
        steps->push_back({"c01_queue", [call, &player, &lib] {
            auto snap = lib.snapshot();
            QVariantList ids;
            for (size_t i = 0; i < snap->songs.size() && i < 400; ++i) ids.append(double(snap->songs[i]->id));
            player.playTracks(ids, 12, QStringLiteral("songs"));
            call("automationQueue", true);
        }, 1600});
        steps->push_back({"c02_queue_scrolled", [call] { call("automationQueueScroll", 120); }, 900});
        steps->push_back({"c03_queue_menu", [call] { call("automationQueueScroll", 0); call("automationQueueMenu", 16); }, 900});
        steps->push_back({"c04_menu_keys", [key, window] {
            window->requestActivate();
            QTimer::singleShot(150, [key] { key(Qt::Key_Down); key(Qt::Key_Down); key(Qt::Key_Down); key(Qt::Key_Down); key(Qt::Key_Right); key(Qt::Key_Down); });
        }, 900});
        steps->push_back({"c05_cleared", [call] { call("automationCloseMenu"); call("automationClearQueue"); }, 900});
        steps->push_back({"c06_now_queue", [call, &player] {
            player.undoClearQueue();
            call("automationQueue", false);
            call("automationNowPlaying", true);
            call("automationNowTab", QStringLiteral("queue"));
        }, 1800});
        steps->push_back({"c07_now_lyrics", [call] { call("automationNowTab", QStringLiteral("lyrics")); }, 1000});
        steps->push_back({"c08_track_info", [call] { call("automationNowPlaying", false); call("automationTrackInfo", true); }, 1300});
        steps->push_back({"c09_sleep_compact", [call, window, &player] {
            call("automationTrackInfo", false);
            call("automationSleep", 30);
            window->resize(960, 640);
            player.changeVolume(-0.1);
        }, 500});
        steps->push_back({"c10_songs_band", [call, window, wheel] {
            window->resize(1280, 800);
            call("automationGo", QStringLiteral("songs"));
            QTimer::singleShot(900, [wheel] { wheel(-360); });
        }, 2400});
        steps->push_back({"c10b_songs_quality", [call] { call("automationSongsSort", 5); }, 1400});
        steps->push_back({"c10c_songs_artist_keys", [call, key, window] {
            call("automationSongsSort", 1);
            window->requestActivate();
            QTimer::singleShot(600, [key] { key(Qt::Key_Down); key(Qt::Key_Down); key(Qt::Key_Down); });
        }, 1600});
        steps->push_back({"c11_big_now", [call, window] {
            call("automationSongsSort", 0);
            call("automationSleep", 0);
            window->resize(1920, 1080);
            call("automationNowPlaying", true);
        }, 2000});
    } else if (qEnvironmentVariableIsSet("KP_TEST_FINDPLAY")) {
        // 回归:只查找重复、只预览整理 (不动文件),然后点播放
        auto tools = [&engine]() { return qobject_cast<LibraryTools*>(engine.rootContext()->contextProperty("libtools").value<QObject*>()); };
        // 用户报告的崩溃:暂停状态下重新扫描 (曲库换了新快照),再点一首别的歌
        steps->push_back({"p0_play", [&player] { player.playAlbum(1, 0); }, 1200});
        steps->push_back({"p0_pause_rescan", [&player, &lib] { if (player.playing()) player.toggle(); lib.rescan(); }, 6000});
        steps->push_back({"p0_play_after_scan", [&player] { player.playAlbum(2, 0); }, 1500});
        steps->push_back({"p1_dedupe", [tools, call] { call("automationGo", QStringLiteral("dedupe")); tools()->findDuplicates(); }, 8000});
        steps->push_back({"p2_organize", [tools, call, outDir] {
            call("automationGo", QStringLiteral("organize"));
            tools()->setTargetUrl(QDir(outDir).absoluteFilePath("organized"));
            tools()->buildPlan();
        }, 4000});
        steps->push_back({"p3_play", [&player, call] { call("automationGo", QStringLiteral("albums")); player.playAlbum(0, 0); }, 1500});
        steps->push_back({"p4_next", [&player] { player.next(); player.playAlbum(3, 2); }, 1500});
        steps->push_back({"p5_dedupe_again", [tools, call] { call("automationGo", QStringLiteral("dedupe")); tools()->autoSelect(1); }, 1500});
        steps->push_back({"p6_play_again", [&player] { player.playAlbum(5, 0); }, 1500});
    } else if (!flowDir.isEmpty()) {
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
    steps->push_back({"04b_track_info", [call] { call("automationTrackInfo", true); }, 900});
    steps->push_back({"05_artists", [window, call] {
        QMetaObject::invokeMethod(window, "automationTrackInfo", Q_ARG(QVariant, false));
        QMetaObject::invokeMethod(window, "automationBack");
        call("automationGo", QStringLiteral("artists"));
    }, 1600});
    steps->push_back({"06_artist_detail", [call] { call("automationOpenArtist", QStringLiteral("久石譲")); }, 1600});
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
    // 文件夹面板:打开途中与落定,以及面板里进入的整理 / 查重页
    steps->push_back({"20_panel_mid", [call] { call("automationLibraryPanel", true); }, 90});
    steps->push_back({"20b_panel", [] {}, 800});
    steps->push_back({"21_organize", [call] {
        call("automationLibraryPanel", false);
        call("automationGo", QStringLiteral("organize"));
    }, 1200});
    steps->push_back({"21b_organize_plan", [&engine, outDir] {
        auto* t = qobject_cast<LibraryTools*>(engine.rootContext()->contextProperty("libtools").value<QObject*>());
        if (!t) return;
        t->setTargetUrl(QDir(outDir).absoluteFilePath("organized"));
        t->buildPlan();
    }, 3000});
    steps->push_back({"22_dedupe", [&engine, call] {
        call("automationGo", QStringLiteral("dedupe"));
        auto* t = qobject_cast<LibraryTools*>(engine.rootContext()->contextProperty("libtools").value<QObject*>());
        if (t) t->findDuplicates();
    }, 400});
    steps->push_back({"22b_dedupe_done", [] {}, 14000});
    steps->push_back({"23_desktop_lyrics", [&engine, &player] {
        // 从头播放,给这首写一份短 LRC (测试曲目只有 2 秒);锁定以显示不悬停时的样子
        player.playAlbum(0, 0);
        // 同一目录的每首都写 (曲目很短,截图时可能已经播到后面几首);同时间戳的第二行是翻译
        const QString path = player.currentPath();
        const QDir dir = QFileInfo(path).dir();
        for (const QFileInfo& fi : dir.entryInfoList({"*.flac", "*.mp3", "*.wav"}, QDir::Files)) {
            QFile f(fi.path() + "/" + fi.completeBaseName() + ".lrc");
            if (!f.open(QIODevice::WriteOnly)) continue;
            f.write(QStringLiteral("[00:00.00]風が吹いている\n[00:00.00]风在吹\n[00:00.60]遠くの空へ 届くように\n[00:00.60]愿能传到远方的天空\n"
                                   "[00:01.20]夢の続きを 歩いていく\n[00:01.20]继续走在梦的路上\n[00:01.90]下一句\n").toUtf8());
            f.close();
        }
        auto* st = qobject_cast<SettingsService*>(engine.rootContext()->contextProperty("settings").value<QObject*>());
        if (st) { st->setLyricsLocked(true); st->setDesktopLyrics(true); }
        player.reloadLyrics();
    }, 1450});
    steps->push_back({"24_desktop_lyrics_off", [&engine, outDir] {
        // 桌面歌词是独立窗口:单独截下来,垫一张深色和一张浅色底看描边效果
        for (QWindow* w : QGuiApplication::topLevelWindows()) {
            auto* q = qobject_cast<QQuickWindow*>(w);
            if (!q || q->title() != QStringLiteral("桌面歌词") || !q->isVisible()) continue;
            // 离屏截图拿不到透明底:临时换成深 / 浅底色各截一张
            q->setColor(QColor(30, 34, 40));
            const QImage dark = q->grabWindow();
            q->setColor(QColor(225, 232, 238));
            const QImage light = q->grabWindow();
            q->setColor(Qt::transparent);
            QImage out(dark.width(), dark.height() * 2, QImage::Format_ARGB32_Premultiplied);
            QPainter p(&out);
            p.drawImage(0, 0, dark);
            p.drawImage(0, dark.height(), light);
            p.end();
            out.save(outDir + "/23b_desktop_lyrics_window.png");
        }
        auto* st = qobject_cast<SettingsService*>(engine.rootContext()->contextProperty("settings").value<QObject*>());
        if (st) { st->setDesktopLyrics(false); st->setLyricsLocked(false); }
    }, 600});
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
    // 设置页往下翻两屏 (检查各分组的对齐)
    for (int k = 1; k <= 3; ++k)
        steps->push_back({QStringLiteral("19%1_settings_scroll").arg(QChar('a' + k)), [window, k] {
            if (auto* f = window->findChild<QObject*>("settingsFlick")) {
                const double maxY = std::max(0.0, f->property("contentHeight").toDouble() - f->property("height").toDouble());
                f->setProperty("contentY", std::min(maxY, 900.0 * k));
            }
        }, 700});
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

#ifdef Q_OS_WIN
// 崩溃诊断:未处理的异常 (访问越界等) 发生时,在数据目录 crashes\ 下写一个 minidump,
// 用调试器打开即可看到崩溃时每个线程的调用栈
static wchar_t g_dumpDir[MAX_PATH] = {0};

static LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* ep) {
    if (!g_dumpDir[0]) return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH + 64];
    swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\crash-%04d%02d%02d-%02d%02d%02d.dmp", g_dumpDir, st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mi;
        mi.ThreadId = GetCurrentThreadId();
        mi.ExceptionPointers = ep;
        mi.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &mi, nullptr,
                          nullptr);
        CloseHandle(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void InstallCrashDump() {
    const QString dir = LibraryService::dataDir() + "/crashes";
    QDir().mkpath(dir);
    const std::wstring w = QDir::toNativeSeparators(dir).toStdWString();
    if (w.size() >= MAX_PATH) return;
    wcsncpy(g_dumpDir, w.c_str(), MAX_PATH - 1);
    SetUnhandledExceptionFilter(WriteCrashDump);
}
#endif

int main(int argc, char* argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QCoreApplication::setOrganizationName("KugouPlayer");
    QCoreApplication::setApplicationName("KugouPlayer");
    // 界面缩放 (设置页):必须在创建应用对象之前生效
    const double scale = SettingsService::StartupScale();
    if (scale > 0.5 && scale < 3.0 && std::abs(scale - 1.0) > 0.01 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));
    QGuiApplication app(argc, argv);
    // 全局线程池只跑提取封面主色、加载歌词、统计缓存这类零星后台任务,默认按逻辑核数 (可达 20) 铺线程没有必要
    QThreadPool::globalInstance()->setMaxThreadCount(4);
    app.setWindowIcon(QIcon(QStringLiteral(":/images/app_icon_256.png")));
#ifdef Q_OS_WIN
    InstallCrashDump();
#endif
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
    // 播放历史先于专辑墙建立:曲库变化时它先更新,专辑墙「最近播放」排序用到它
    PlaylistService playlists(&library);
    AlbumModel albums(&library, &playlists);
    TrackListModel albumTracks(&library);
    SearchModel search(&library);
    DecryptService decrypt(&library);
    SettingsService settings(&library);
    ArtistModel artists(&library);
    TrackCollectionModel songs(&library, &playlists);
    TrackCollectionModel artistTracks(&library, &playlists);
    TrackCollectionModel playlistTracks(&library, &playlists);
    artistTracks.setSource("artist");
    playlistTracks.setSource("playlist");
    TrackCollectionModel historyTracks(&library, &playlists);
    historyTracks.setSource("history");
    // 真正开始播放一首歌时记入历史 (恢复上次会话时停在暂停状态的那首,等按下播放才算)
    auto recordPlay = [&player, &playlists]() {
        if (player.playing() && player.trackId() >= 0) playlists.recordPlay(player.trackId());
    };
    QObject::connect(&player, &PlayerController::trackChanged, &playlists, recordPlay);
    QObject::connect(&player, &PlayerController::playingChanged, &playlists, recordPlay);
    WindowHelper windowHelper;
    LibraryTools libtools(&library, &playlists, &player);

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
    ctx->setContextProperty("historyTracks", &historyTracks);
    ctx->setContextProperty("windowHelper", &windowHelper);
    ctx->setContextProperty("libtools", &libtools);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    // 兼容 Qt 6.4 (loadFromModule 自 6.5 起才有):资源前缀在 CMake 中显式固定为 /qt/qml
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PlayerUi/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    player.attachWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)));
    // 全局多媒体键
    app.installNativeEventFilter(&windowHelper);
    windowHelper.registerMediaKeys(qobject_cast<QWindow*>(engine.rootObjects().value(0)));
    QObject::connect(&windowHelper, &WindowHelper::mediaPlayPause, &player, &PlayerController::toggle);
    QObject::connect(&windowHelper, &WindowHelper::mediaNext, &player, &PlayerController::next);
    QObject::connect(&windowHelper, &WindowHelper::mediaPrevious, &player, &PlayerController::previous);
    // 任务栏缩略图 (封面) 与缩略图按钮
    TaskbarHelper taskbar(&player, &playlists);
    app.installNativeEventFilter(&taskbar);
    taskbar.attach(qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)));
    QObject::connect(&taskbar, &TaskbarHelper::toggleDesktopLyrics, &settings,
                     [&settings]() { settings.setDesktopLyrics(!settings.desktopLyrics()); });
#ifdef Q_OS_WIN
    // 播放时不让电脑自动睡眠 (显示器照常关闭);暂停或关掉这项后恢复
    auto keepAwake = [&player, &settings]() {
        SetThreadExecutionState(player.playing() && settings.keepAwake() ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
    };
    QObject::connect(&player, &PlayerController::playingChanged, &settings, keepAwake);
    QObject::connect(&settings, &SettingsService::changed, &settings, keepAwake);
#endif

    if (!libraryArg.isEmpty()) library.addFolder(libraryArg);
    else library.rescan();

    if (!screenshotDir.isEmpty()) RunScreenshots(engine, library, player, screenshotDir, flowArg);
    return app.exec();
}
