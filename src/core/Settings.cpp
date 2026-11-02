// SPDX-License-Identifier: MIT
#include "Settings.h"

#include "Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSettings>
#include <QStandardPaths>

namespace BlackCore {
namespace {

constexpr const char *kFontFamilyKey = "terminal/fontFamily";
constexpr const char *kFontSizeKey = "terminal/fontSize";
constexpr const char *kLineSpacingKey = "terminal/lineSpacing";
constexpr const char *kPaddingKey = "terminal/padding";
constexpr const char *kThemeKey = "appearance/themeId";
constexpr const char *kLegacyThemeKey = "terminal/colorTheme";
constexpr const char *kCursorStyleKey = "appearance/cursorStyle";
constexpr const char *kOpacityKey = "appearance/windowOpacity";
constexpr const char *kSystemTitleBarKey = "appearance/useSystemTitleBar";
constexpr const char *kSidebarKey = "appearance/sidebarVisible";
constexpr const char *kUiScaleKey = "appearance/uiScale";
constexpr const char *kShellKey = "terminal/shell";
constexpr const char *kScrollbackKey = "terminal/scrollbackLines";
constexpr const char *kConfirmPasteKey = "terminal/confirmMultilinePaste";
constexpr const char *kBellKey = "terminal/bellAudible";
constexpr const char *kLastDirKey = "session/lastWorkingDirectory";
constexpr const char *kCursorBlinkKey = "appearance/cursorBlink";
constexpr const char *kStatusBarKey = "appearance/statusBarVisible";
constexpr const char *kMenuBarKey = "appearance/menuBarVisible";
constexpr const char *kTabBarKey = "appearance/tabBarVisible";
constexpr const char *kDevPanelKey = "appearance/devPanelVisible";
constexpr const char *kMonitorPanelKey = "appearance/monitorPanelVisible";
constexpr const char *kMonitorIntervalKey = "monitor/intervalMs";
constexpr const char *kRecordHistoryKey = "history/recordCommands";
constexpr const char *kWheelLinesKey = "terminal/wheelScrollLines";
constexpr const char *kScrollOnOutputKey = "terminal/scrollOnOutput";
constexpr const char *kScrollOnKeystrokeKey = "terminal/scrollOnKeystroke";
constexpr const char *kStartDirectoryKey = "behavior/startDirectory";
constexpr const char *kRestoreSessionKey = "session/restoreOnStartup";
constexpr const char *kNewTabDirKey = "behavior/newTabInCurrentDirectory";
constexpr const char *kWarnCloseKey = "behavior/warnOnCloseRunning";
constexpr const char *kConfirmCloseWindowKey = "behavior/confirmCloseWindow";
constexpr const char *kAutoSaveKey = "behavior/autoSave";

//! Ключ для чтения масштаба до создания QApplication
QString uiScaleStoragePath()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty())
        configDir = QDir::homePath() + QStringLiteral("/.config");
    return configDir + QStringLiteral("/BlackCore/BlackCore.conf");
}

} // namespace

Settings &Settings::instance()
{
    static Settings settings;
    return settings;
}

QString Settings::defaultShell()
{
    const QString envShell = qEnvironmentVariable("SHELL");
    if (!envShell.isEmpty() && QFileInfo(envShell).isExecutable())
        return envShell;
    for (const QString &candidate : {QStringLiteral("/bin/bash"), QStringLiteral("/usr/bin/bash")}) {
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    return QStringLiteral("/bin/sh");
}

void Settings::setFontSize(int value)
{
    m_fontSize = qBound(kMinFontSize, value, kMaxFontSize);
}

void Settings::setLineSpacing(qreal value)
{
    m_lineSpacing = qBound(kMinLineSpacing, value, kMaxLineSpacing);
}

void Settings::setPadding(int value)
{
    m_padding = qBound(kMinPadding, value, kMaxPadding);
}

void Settings::setWindowOpacity(int value)
{
    m_windowOpacity = qBound(kMinOpacity, value, kMaxOpacity);
}

void Settings::setUiScale(qreal value)
{
    m_uiScale = qBound(kMinUiScale, value, kMaxUiScale);
}

void Settings::setScrollbackLines(int value)
{
    m_scrollbackLines = qBound(kMinScrollback, value, kMaxScrollback);
}

void Settings::setWheelScrollLines(int value)
{
    m_wheelScrollLines = qBound(kMinWheelLines, value, kMaxWheelLines);
}

qreal Settings::storedUiScale()
{
    QSettings store(uiScaleStoragePath(), QSettings::IniFormat);
    const qreal scale = store.value(QLatin1String(kUiScaleKey), 1.0).toReal();
    return qBound(kMinUiScale, scale, kMaxUiScale);
}

void Settings::load()
{
    QSettings store;
    m_fontFamily = store.value(QLatin1String(kFontFamilyKey), QString()).toString();

    if (m_fontFamily.isEmpty()) {
        // Моноширинный шрифт по умолчанию из системных
        const QStringList families = QFontDatabase::families();
        const QStringList preferred{QStringLiteral("Ubuntu Mono"),
                                    QStringLiteral("DejaVu Sans Mono"),
                                    QStringLiteral("Liberation Mono"),
                                    QStringLiteral("Noto Sans Mono"),
                                    QStringLiteral("Source Code Pro"),
                                    QStringLiteral("Hack"),
                                    QStringLiteral("Cascadia Code"),
                                    QStringLiteral("Fira Code")};
        for (const QString &name : preferred) {
            if (families.contains(name)) {
                m_fontFamily = name;
                break;
            }
        }
        if (m_fontFamily.isEmpty())
            m_fontFamily = QStringLiteral("Monospace");
    }

    setFontSize(store.value(QLatin1String(kFontSizeKey), 11).toInt());
    setLineSpacing(store.value(QLatin1String(kLineSpacingKey), 1.0).toReal());
    setPadding(store.value(QLatin1String(kPaddingKey), 6).toInt());

    // Тема: новый ключ, иначе миграция со старого
    QString themeId = store.value(QLatin1String(kThemeKey), QString()).toString();
    if (themeId.isEmpty())
        themeId = store.value(QLatin1String(kLegacyThemeKey), QString()).toString();
    m_themeId = ThemeManager::normalizeId(themeId);

    const int cursorStyle = store.value(QLatin1String(kCursorStyleKey), 0).toInt();
    m_cursorStyle = static_cast<CursorStyle>(qBound(0, cursorStyle, 3));

    setWindowOpacity(store.value(QLatin1String(kOpacityKey), 100).toInt());
    m_useSystemTitleBar = store.value(QLatin1String(kSystemTitleBarKey), false).toBool();
    m_sidebarVisible = store.value(QLatin1String(kSidebarKey), true).toBool();
    setUiScale(store.value(QLatin1String(kUiScaleKey), 1.0).toReal());

    m_shell = store.value(QLatin1String(kShellKey), defaultShell()).toString();
    if (!QFileInfo(m_shell).isExecutable())
        m_shell = defaultShell();

    setScrollbackLines(store.value(QLatin1String(kScrollbackKey), 3000).toInt());
    m_confirmMultilinePaste = store.value(QLatin1String(kConfirmPasteKey), true).toBool();
    m_bellAudible = store.value(QLatin1String(kBellKey), true).toBool();
    m_lastWorkingDirectory = store.value(QLatin1String(kLastDirKey), QDir::homePath()).toString();

    m_cursorBlink = store.value(QLatin1String(kCursorBlinkKey), true).toBool();
    m_statusBarVisible = store.value(QLatin1String(kStatusBarKey), true).toBool();
    m_menuBarVisible = store.value(QLatin1String(kMenuBarKey), true).toBool();
    m_tabBarVisible = store.value(QLatin1String(kTabBarKey), true).toBool();
    m_devPanelVisible = store.value(QLatin1String(kDevPanelKey), false).toBool();
    m_monitorPanelVisible = store.value(QLatin1String(kMonitorPanelKey), false).toBool();
    setMonitorIntervalMs(store.value(QLatin1String(kMonitorIntervalKey), 1500).toInt());
    m_recordHistory = store.value(QLatin1String(kRecordHistoryKey), true).toBool();
    setWheelScrollLines(store.value(QLatin1String(kWheelLinesKey), 3).toInt());
    m_scrollOnOutput = store.value(QLatin1String(kScrollOnOutputKey), true).toBool();
    m_scrollOnKeystroke = store.value(QLatin1String(kScrollOnKeystrokeKey), true).toBool();

    // Стартовый каталог: пустая строка или пропавший каталог — «как раньше»
    m_startDirectory = store.value(QLatin1String(kStartDirectoryKey), QString()).toString();
    if (!m_startDirectory.isEmpty() && !QFileInfo(m_startDirectory).isDir())
        m_startDirectory.clear();

    m_restoreSession = store.value(QLatin1String(kRestoreSessionKey), true).toBool();
    m_newTabInCurrentDirectory = store.value(QLatin1String(kNewTabDirKey), true).toBool();
    m_warnOnCloseRunning = store.value(QLatin1String(kWarnCloseKey), true).toBool();
    m_confirmCloseWindow = store.value(QLatin1String(kConfirmCloseWindowKey), true).toBool();
    m_autoSave = store.value(QLatin1String(kAutoSaveKey), true).toBool();
}

void Settings::setMonitorIntervalMs(int value)
{
    m_monitorIntervalMs = qBound(kMinMonitorInterval, value, kMaxMonitorInterval);
}

void Settings::save() const
{
    QSettings store;
    store.setValue(QLatin1String(kFontFamilyKey), m_fontFamily);
    store.setValue(QLatin1String(kFontSizeKey), m_fontSize);
    store.setValue(QLatin1String(kLineSpacingKey), m_lineSpacing);
    store.setValue(QLatin1String(kPaddingKey), m_padding);
    store.setValue(QLatin1String(kThemeKey), m_themeId);
    store.setValue(QLatin1String(kCursorStyleKey), static_cast<int>(m_cursorStyle));
    store.setValue(QLatin1String(kOpacityKey), m_windowOpacity);
    store.setValue(QLatin1String(kSystemTitleBarKey), m_useSystemTitleBar);
    store.setValue(QLatin1String(kSidebarKey), m_sidebarVisible);
    store.setValue(QLatin1String(kUiScaleKey), m_uiScale);
    store.setValue(QLatin1String(kShellKey), m_shell);
    store.setValue(QLatin1String(kScrollbackKey), m_scrollbackLines);
    store.setValue(QLatin1String(kConfirmPasteKey), m_confirmMultilinePaste);
    store.setValue(QLatin1String(kBellKey), m_bellAudible);
    store.setValue(QLatin1String(kLastDirKey), m_lastWorkingDirectory);
    store.setValue(QLatin1String(kCursorBlinkKey), m_cursorBlink);
    store.setValue(QLatin1String(kStatusBarKey), m_statusBarVisible);
    store.setValue(QLatin1String(kMenuBarKey), m_menuBarVisible);
    store.setValue(QLatin1String(kTabBarKey), m_tabBarVisible);
    store.setValue(QLatin1String(kDevPanelKey), m_devPanelVisible);
    store.setValue(QLatin1String(kMonitorPanelKey), m_monitorPanelVisible);
    store.setValue(QLatin1String(kMonitorIntervalKey), m_monitorIntervalMs);
    store.setValue(QLatin1String(kRecordHistoryKey), m_recordHistory);
    store.setValue(QLatin1String(kWheelLinesKey), m_wheelScrollLines);
    store.setValue(QLatin1String(kScrollOnOutputKey), m_scrollOnOutput);
    store.setValue(QLatin1String(kScrollOnKeystrokeKey), m_scrollOnKeystroke);
    store.setValue(QLatin1String(kStartDirectoryKey), m_startDirectory);
    store.setValue(QLatin1String(kRestoreSessionKey), m_restoreSession);
    store.setValue(QLatin1String(kNewTabDirKey), m_newTabInCurrentDirectory);
    store.setValue(QLatin1String(kWarnCloseKey), m_warnOnCloseRunning);
    store.setValue(QLatin1String(kConfirmCloseWindowKey), m_confirmCloseWindow);
    store.setValue(QLatin1String(kAutoSaveKey), m_autoSave);
    store.sync();
}

} // namespace BlackCore
