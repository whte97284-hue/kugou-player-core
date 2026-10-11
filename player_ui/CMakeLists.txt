find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 ShaderTools Svg)
qt_standard_project_setup()

qt_add_executable(KugouPlayer
    src/main.cpp
    src/library_service.h src/library_service.cpp
    src/models.h src/models.cpp
    src/cover_provider.h src/cover_provider.cpp
    src/player_controller.h src/player_controller.cpp
    src/window_helper.h src/window_helper.cpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(KugouPlayer
    URI PlayerUi
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Backdrop.qml
        qml/TitleBar.qml
        qml/SideNav.qml
        qml/AlbumWall.qml
        qml/AlbumCard.qml
        qml/AlbumDetail.qml
        qml/TrackRow.qml
        qml/PlayIsland.qml
        qml/SignalChain.qml
        qml/Spectrum.qml
        qml/SeekBar.qml
        qml/IconButton.qml
        qml/Icon.qml
        qml/CommandPalette.qml
        qml/QualityBadge.qml
        qml/RoundedImage.qml
        qml/ScanToast.qml
)

qt_add_shaders(KugouPlayer "shaders"
    PREFIX "/"
    FILES
        shaders/grain.frag
        shaders/rounded.frag
        shaders/frosted.frag
)

target_link_libraries(KugouPlayer PRIVATE player_core Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
target_include_directories(KugouPlayer PRIVATE src)

if(MSVC)
    target_compile_options(KugouPlayer PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
    target_compile_definitions(KugouPlayer PRIVATE NOMINMAX)
endif()
if(WIN32)
    target_link_libraries(KugouPlayer PRIVATE dwmapi)
    set_target_properties(KugouPlayer PROPERTIES WIN32_EXECUTABLE ON)
endif()

file(GLOB PLAYER_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} icons/*.svg)
qt_add_resources(KugouPlayer "icons" PREFIX "/" FILES ${PLAYER_ICONS})
