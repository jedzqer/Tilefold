cmake_minimum_required(VERSION 3.20)
project(Tilefold VERSION 1.0.0 LANGUAGES CXX RC)

# 发布标签：beta / rc1 / 留空 = 正式版。
# 只影响对外显示的名字与安装包文件名；WiX 的 ProductVersion 必须是纯数字 x.y.z，
# 所以包版本号继续用 PROJECT_VERSION，标签只挂在显示名上。
set(TILEFOLD_RELEASE_TAG "-beta")
set(TILEFOLD_DISPLAY_VERSION "${PROJECT_VERSION}${TILEFOLD_RELEASE_TAG}")

configure_file(src/app.manifest.in "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 给 clangd / pi-lens 生成编译数据库，否则编辑器侧会缺 include 路径和 C++20 标准配置，
# 报出一堆假的“std::wstring_view 不存在”之类的错误。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MSVC)
    message(FATAL_ERROR "Tilefold 只支持 MSVC (Win32 + Direct2D)")
endif()

add_executable(Tilefold WIN32
    src/main.cpp
    src/app.rc
    src/tilefold.ico
    src/core/Log.cpp
    src/core/Paths.cpp
    src/core/Dpi.cpp
    src/core/Grid.cpp
    src/core/Layout.cpp
    src/core/LayoutPlacement.cpp
    src/core/MonitorLayout.cpp
    src/core/Autostart.cpp
    src/core/Settings.cpp
    src/core/SettingsWindow.cpp
    src/core/App.cpp
    src/render/GpuDevice.cpp
    src/render/Surface.cpp
    src/render/Layer.cpp
    src/shell/DesktopHost.cpp
    src/shell/BoxScan.cpp
    src/shell/Launcher.cpp
    src/shell/IconStore.cpp
    src/shell/ShellMenu.cpp
    src/shell/GroupFolder.cpp
    src/shell/DesktopContextMenu.cpp
    src/core/PromptDialog.cpp
    src/widgets/WidgetWindow.cpp
    src/widgets/FrameClock.cpp
    src/core/Tray.cpp
    src/widgets/Paint.cpp
    src/widgets/BigFolderWidget.cpp
    src/widgets/ExpandedFolder.cpp
    src/widgets/SingleIconWidget.cpp
)

target_include_directories(Tilefold PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(Tilefold PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    TILEFOLD_DISPLAY_VERSION=L"${TILEFOLD_DISPLAY_VERSION}"
)

target_compile_options(Tilefold PRIVATE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /EHsc
    /wd4324
)

target_link_libraries(Tilefold PRIVATE
    d3d11
    dxgi
    d2d1
    dcomp
    dwrite
    windowscodecs
    shcore
    shlwapi
    ole32
    oleaut32
    shell32
    user32
    gdi32
    dwmapi
)

# 我们的 app.rc 已经提供了完整的 RT_MANIFEST(ID 1)。
# 关掉 link.exe 自己生成 manifest，否则两个 ID=1 的 MANIFEST 资源会撞（CVT1100）。
target_link_options(Tilefold PRIVATE /MANIFEST:NO)

# 把 pdb 放在 build 目录，方便调试
set_target_properties(Tilefold PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Tilefold>"
    # Keep the portable executable independent of the VC++ redistributable.
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Ship only application files; never include the developer's data or shortcuts.
install(TARGETS Tilefold RUNTIME DESTINATION . CONFIGURATIONS Release)
install(FILES README.md LICENSE DESTINATION . CONFIGURATIONS Release)
set(CPACK_GENERATOR ZIP;WIX)
set(CPACK_PACKAGE_NAME Tilefold)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
if(CMAKE_GENERATOR_PLATFORM)
    set(TILEFOLD_PACKAGE_ARCH "${CMAKE_GENERATOR_PLATFORM}")
else()
    set(TILEFOLD_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME "Tilefold-${TILEFOLD_DISPLAY_VERSION}-windows-${TILEFOLD_PACKAGE_ARCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Tilefold ${TILEFOLD_DISPLAY_VERSION}")
set(CPACK_WIX_PRODUCT_NAME "Tilefold")
set(CPACK_WIX_UPGRADE_GUID "9D7E4F82-06B8-45C2-9A15-01D8F5C68A1B")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "Tilefold")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/src/tilefold.ico")
set(CPACK_WIX_INSTALL_SCOPE "perUser")
set(CPACK_WIX_COMPONENT_KEYS_REGISTRY_PATH "Software/Tilefold/Installer/Components")
configure_file(cmake/tilefold-wix-patch.xml.in
    "${CMAKE_CURRENT_BINARY_DIR}/tilefold-wix-patch.xml" @ONLY)
set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_BINARY_DIR}/tilefold-wix-patch.xml")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)

include(CTest)
if(BUILD_TESTING)
    add_executable(TilefoldUiTests
        tests/UiCompatibility.cpp
        src/core/Autostart.cpp
        src/core/SettingsWindow.cpp
        src/core/Settings.cpp
        src/core/Paths.cpp
        src/core/Log.cpp
        src/core/Dpi.cpp
        src/widgets/WidgetWindow.cpp
        src/widgets/FrameClock.cpp
        src/render/GpuDevice.cpp
        src/render/Surface.cpp
        src/render/Layer.cpp
        src/shell/DesktopHost.cpp
    )
    target_include_directories(TilefoldUiTests PRIVATE src)
    target_compile_definitions(TilefoldUiTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    )
    target_compile_options(TilefoldUiTests PRIVATE /utf-8 /W4)
    target_link_libraries(TilefoldUiTests PRIVATE user32 gdi32 shell32 shcore
        d3d11 dxgi d2d1 dcomp dwrite windowscodecs shlwapi ole32 oleaut32 dwmapi)
    set_target_properties(TilefoldUiTests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME ui-compatibility COMMAND TilefoldUiTests)
    add_executable(TilefoldMonitorTests
        tests/MonitorLayoutTests.cpp
        src/core/MonitorLayout.cpp
        src/core/Grid.cpp
        src/core/LayoutPlacement.cpp
        src/core/Layout.cpp
        src/core/Dpi.cpp
        src/core/Paths.cpp
        src/core/Log.cpp
    )
    target_include_directories(TilefoldMonitorTests PRIVATE src)
    target_compile_definitions(TilefoldMonitorTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    )
    target_compile_options(TilefoldMonitorTests PRIVATE /utf-8 /W4)
    target_link_libraries(TilefoldMonitorTests PRIVATE shcore shell32 user32)
    set_target_properties(TilefoldMonitorTests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME monitor-layout COMMAND TilefoldMonitorTests)
    add_executable(TilefoldPanelTests tests/PanelGeometryTests.cpp)
    target_include_directories(TilefoldPanelTests PRIVATE src)
    target_compile_definitions(TilefoldPanelTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(TilefoldPanelTests PRIVATE /utf-8 /W4)
    set_target_properties(TilefoldPanelTests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_test(NAME panel-geometry COMMAND TilefoldPanelTests)

    # 「新建收纳文件夹」的命名规则：纯字符串逻辑，不需要窗口和 GPU
    add_executable(TilefoldGroupTests
        tests/GroupFolderTests.cpp
        src/shell/GroupFolder.cpp
    )
    target_include_directories(TilefoldGroupTests PRIVATE src)
    target_compile_definitions(TilefoldGroupTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(TilefoldGroupTests PRIVATE /utf-8 /W4)
    set_target_properties(TilefoldGroupTests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_test(NAME group-folder COMMAND TilefoldGroupTests)

    add_executable(TilefoldLayoutGroupTests
        tests/LayoutGroupTests.cpp
        src/core/Layout.cpp
        src/core/Grid.cpp
        src/core/Paths.cpp
        src/core/Log.cpp
    )
    target_include_directories(TilefoldLayoutGroupTests PRIVATE src)
    target_compile_definitions(TilefoldLayoutGroupTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(TilefoldLayoutGroupTests PRIVATE /utf-8 /W4)
    set_target_properties(TilefoldLayoutGroupTests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(TilefoldLayoutGroupTests PRIVATE shcore shell32 user32)
    add_test(NAME layout-groups COMMAND TilefoldLayoutGroupTests)
endif()
