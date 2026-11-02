# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)
project(BlackCore VERSION 1.0.2 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- зависимости -----------------------------------------------------------
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

# Экспериментальное расширение API libvterm (не входит в системную 0.3.3).
# По умолчанию выключено: стандартная сборка и пакет остаются совместимыми.
option(BLACKCORE_ENABLE_VTERM_PUSHLINE4 "Use extended libvterm scrollback callback" OFF)
if(BLACKCORE_ENABLE_VTERM_PUSHLINE4)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES PkgConfig::VTERM)
    check_cxx_source_compiles([=[
        #include <vterm.h>
        int main() {
            VTermScreenCallbacks cb{};
            cb.sb_pushline4 = nullptr;
            vterm_screen_callbacks_has_pushline4(nullptr);
            return 0;
        }
    ]=] BLACKCORE_VTERM_PUSHLINE4_AVAILABLE)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT BLACKCORE_VTERM_PUSHLINE4_AVAILABLE)
        message(FATAL_ERROR "The selected libvterm does not support sb_pushline4")
    endif()
    message(STATUS "BlackCore: extended libvterm scrollback callback enabled")
    message(WARNING "Experimental build: libvterm with sb_pushline4 must remain available at runtime; do not distribute this build as a standalone package")
endif()

# --- приложение ------------------------------------------------------------
set(BLACKCORE_SOURCES
    src/main.cpp
    src/core/AppInfo.cpp
    src/core/AppInfo.h
    src/core/Logger.cpp
    src/core/Logger.h
    src/core/Settings.cpp
    src/core/Settings.h
    src/core/ColorTheme.cpp
    src/core/ColorTheme.h
    src/core/Theme.cpp
    src/core/Theme.h
    src/terminal/PtyProcess.cpp
    src/terminal/PtyProcess.h
    src/terminal/VTermEngine.cpp
    src/terminal/VTermEngine.h
    src/terminal/TerminalView.cpp
    src/terminal/TerminalView.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/TitleBar.cpp
    src/ui/TitleBar.h
    src/ui/SideBar.cpp
    src/ui/SideBar.h
    src/ui/UiIcons.cpp
    src/ui/UiIcons.h
    src/ui/TerminalTabs.cpp
    src/ui/TerminalTabs.h
    src/ui/SettingsDialog.cpp
    src/ui/DialogHeader.cpp
    src/ui/SettingsDialog.h
    src/ui/DevPanel.cpp
    src/ui/DevPanel.h
    src/ui/PaneFrame.cpp
    src/ui/PaneFrame.h
    src/ui/PaneTree.cpp
    src/ui/PaneTree.h
    src/core/Profile.cpp
    src/core/Profile.h
    src/core/PaneLayout.cpp
    src/core/PaneLayout.h
    src/core/Workspace.cpp
    src/core/Workspace.h
    src/core/SessionStore.cpp
    src/core/SessionStore.h
    src/core/Shortcuts.cpp
    src/core/Shortcuts.h
    src/core/ConfigIO.cpp
    src/core/ConfigIO.h
    src/core/GitInfo.cpp
    src/core/GitInfo.h
    src/core/SshProfile.cpp
    src/core/SshProfile.h
    src/core/Project.cpp
    src/core/Project.h
    src/core/SystemMonitor.cpp
    src/core/SystemMonitor.h
    src/core/Diagnostics.cpp
    src/core/Diagnostics.h
    src/core/CrashHandler.cpp
    src/core/CrashHandler.h
    src/core/Secrets.cpp
    src/core/Secrets.h
    src/core/CommandLibrary.cpp
    src/core/CommandLibrary.h
    src/core/History.cpp
    src/core/History.h
    src/core/Notifications.cpp
    src/core/Notifications.h
    src/core/TaskTracker.cpp
    src/core/TaskTracker.h
    src/ui/MonitorPanel.cpp
    src/ui/MonitorPanel.h
    src/ui/CommandPalette.cpp
    src/ui/CommandPalette.h
    src/ui/CommandLibraryDialog.cpp
    src/ui/CommandLibraryDialog.h
    src/ui/HistoryDialog.cpp
    src/ui/HistoryDialog.h
    src/ui/NotificationToast.cpp
    src/ui/NotificationToast.h
    resources.qrc
)

add_executable(blackcore ${BLACKCORE_SOURCES})
target_include_directories(blackcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(blackcore PRIVATE Qt6::Widgets Qt6::Svg PkgConfig::VTERM)

target_compile_definitions(blackcore PRIVATE
    BLACKCORE_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
)
if(BLACKCORE_ENABLE_VTERM_PUSHLINE4)
    target_compile_definitions(blackcore PRIVATE BLACKCORE_VTERM_PUSHLINE4)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(blackcore PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- автотесты -------------------------------------------------------------
option(BUILD_TESTING "Собрать автотесты терминала" OFF)
if(BUILD_TESTING)
    add_executable(blackcore-tests
        tests/terminal_tests.cpp
        src/core/AppInfo.cpp
        src/core/Logger.cpp
        src/core/Settings.cpp
        src/core/ColorTheme.cpp
        src/core/Theme.cpp
        src/core/Theme.h
        src/terminal/PtyProcess.cpp
        src/terminal/PtyProcess.h
        src/terminal/VTermEngine.cpp
        src/terminal/VTermEngine.h
        src/terminal/TerminalView.cpp
        src/terminal/TerminalView.h
        src/ui/MainWindow.cpp
        src/ui/MainWindow.h
        src/ui/TitleBar.cpp
        src/ui/TitleBar.h
        src/ui/SideBar.cpp
        src/ui/SideBar.h
        src/ui/UiIcons.cpp
        src/ui/UiIcons.h
        src/ui/TerminalTabs.cpp
        src/ui/TerminalTabs.h
        src/ui/SettingsDialog.cpp
        src/ui/DialogHeader.cpp
        src/ui/SettingsDialog.h
        src/ui/DevPanel.cpp
        src/ui/DevPanel.h
        src/ui/PaneFrame.cpp
        src/ui/PaneFrame.h
        src/ui/PaneTree.cpp
        src/ui/PaneTree.h
        src/core/Profile.cpp
        src/core/Profile.h
        src/core/PaneLayout.cpp
        src/core/PaneLayout.h
        src/core/Workspace.cpp
        src/core/Workspace.h
        src/core/SessionStore.cpp
        src/core/SessionStore.h
        src/core/Shortcuts.cpp
        src/core/Shortcuts.h
        src/core/ConfigIO.cpp
        src/core/ConfigIO.h
        src/core/GitInfo.cpp
        src/core/GitInfo.h
        src/core/SshProfile.cpp
        src/core/SshProfile.h
        src/core/Project.cpp
        src/core/Project.h
        src/core/SystemMonitor.cpp
        src/core/SystemMonitor.h
        src/core/Diagnostics.cpp
        src/core/Diagnostics.h
        src/core/CrashHandler.cpp
        src/core/CrashHandler.h
        src/core/Secrets.cpp
        src/core/Secrets.h
        src/core/CommandLibrary.cpp
        src/core/CommandLibrary.h
        src/core/History.cpp
        src/core/History.h
        src/core/Notifications.cpp
        src/core/Notifications.h
        src/core/TaskTracker.cpp
        src/core/TaskTracker.h
        src/ui/MonitorPanel.cpp
        src/ui/MonitorPanel.h
        src/ui/CommandPalette.cpp
        src/ui/CommandPalette.h
        src/ui/CommandLibraryDialog.cpp
        src/ui/CommandLibraryDialog.h
        src/ui/HistoryDialog.cpp
        src/ui/HistoryDialog.h
        src/ui/NotificationToast.cpp
        src/ui/NotificationToast.h
        resources.qrc
    )
    target_include_directories(blackcore-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(blackcore-tests PRIVATE Qt6::Widgets Qt6::Svg PkgConfig::VTERM)
    target_compile_definitions(blackcore-tests PRIVATE
        BLACKCORE_VERSION="${PROJECT_VERSION}"
        QT_NO_CAST_FROM_ASCII
    )
    if(BLACKCORE_ENABLE_VTERM_PUSHLINE4)
        target_compile_definitions(blackcore-tests PRIVATE BLACKCORE_VTERM_PUSHLINE4)
    endif()
endif()

# --- установка -------------------------------------------------------------
install(TARGETS blackcore RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES data/blackcore.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES data/icons/hicolor/scalable/apps/blackcore.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

foreach(icon_size 16 24 32 48 64 128 256 512)
    install(FILES data/icons/blackcore-${icon_size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps
            RENAME blackcore.png)
endforeach()

install(FILES README.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/blackcore)

# --- упаковка (.deb) -------------------------------------------------------
set(CPACK_PACKAGE_NAME "blackcore")
set(CPACK_PACKAGE_VENDOR "BlackCore")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "BlackCore — терминал для Linux Mint")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "BlackCore <blackcore@localhost>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "BlackCore <blackcore@localhost>")
set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libqt6widgets6 (>= 6.4), libqt6svg6 (>= 6.4), libqt6gui6 (>= 6.4), libqt6core6 (>= 6.4), libvterm0")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
