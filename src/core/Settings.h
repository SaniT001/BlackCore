// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace BlackCore {

//! Форма курсора терминала (пользовательская настройка).
enum class CursorStyle {
    FollowApplication = 0, //!< как просит программа (DECSCUSR)
    Block,
    Underline,
    Bar,
};

//! Настройки приложения (QSettings). Хранятся в ~/.config/BlackCore/BlackCore.conf
class Settings
{
public:
    static Settings &instance();

    void load();
    void save() const;

    // --- шрифт ---
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &value) { m_fontFamily = value; }

    int fontSize() const { return m_fontSize; }
    void setFontSize(int value);

    //! Межстрочный интервал, множитель высоты строки (1.00 … 1.60)
    qreal lineSpacing() const { return m_lineSpacing; }
    void setLineSpacing(qreal value);

    //! Отступ содержимого терминала от краёв, px (0 … 32)
    int padding() const { return m_padding; }
    void setPadding(int value);

    // --- оформление ---
    QString themeId() const { return m_themeId; }
    void setThemeId(const QString &value) { m_themeId = value; }

    CursorStyle cursorStyle() const { return m_cursorStyle; }
    void setCursorStyle(CursorStyle value) { m_cursorStyle = value; }

    //! Мигание курсора
    bool cursorBlink() const { return m_cursorBlink; }
    void setCursorBlink(bool value) { m_cursorBlink = value; }

    //! Прозрачность окна, проценты (60 … 100). Требует композитора.
    int windowOpacity() const { return m_windowOpacity; }
    void setWindowOpacity(int value);

    //! Системный заголовок окна вместо фирменного
    bool useSystemTitleBar() const { return m_useSystemTitleBar; }
    void setUseSystemTitleBar(bool value) { m_useSystemTitleBar = value; }

    //! Показывать боковую панель
    bool sidebarVisible() const { return m_sidebarVisible; }
    void setSidebarVisible(bool value) { m_sidebarVisible = value; }

    //! Показывать строку состояния
    bool statusBarVisible() const { return m_statusBarVisible; }
    void setStatusBarVisible(bool value) { m_statusBarVisible = value; }

    //! Показывать главное меню
    bool menuBarVisible() const { return m_menuBarVisible; }
    void setMenuBarVisible(bool value) { m_menuBarVisible = value; }

    //! Показывать панель разработчика (Git/SSH/проекты)
    bool devPanelVisible() const { return m_devPanelVisible; }
    void setDevPanelVisible(bool value) { m_devPanelVisible = value; }

    //! Показывать панель монитора (система и диагностика)
    bool monitorPanelVisible() const { return m_monitorPanelVisible; }
    void setMonitorPanelVisible(bool value) { m_monitorPanelVisible = value; }

    //! Период обновления показателей монитора, мс (500 … 10000)
    int monitorIntervalMs() const { return m_monitorIntervalMs; }
    void setMonitorIntervalMs(int value);

    //! Записывать историю команд и действий приложения
    bool recordHistory() const { return m_recordHistory; }
    void setRecordHistory(bool value) { m_recordHistory = value; }

    //! Показывать полосу вкладок (когда вкладка одна — можно скрыть)
    bool tabBarVisible() const { return m_tabBarVisible; }
    void setTabBarVisible(bool value) { m_tabBarVisible = value; }

    //! Масштаб интерфейса (HiDPI), применяется при следующем запуске
    qreal uiScale() const { return m_uiScale; }
    void setUiScale(qreal value);

    // --- терминал ---
    QString shell() const { return m_shell; }
    void setShell(const QString &value) { m_shell = value; }

    int scrollbackLines() const { return m_scrollbackLines; }
    void setScrollbackLines(int value);

    //! Сколько строк прокручивать одним шагом колеса мыши (1 … 10)
    int wheelScrollLines() const { return m_wheelScrollLines; }
    void setWheelScrollLines(int value);

    //! Прокручивать вниз при появлении нового вывода
    bool scrollOnOutput() const { return m_scrollOnOutput; }
    void setScrollOnOutput(bool value) { m_scrollOnOutput = value; }

    //! Возвращаться вниз при вводе с клавиатуры
    bool scrollOnKeystroke() const { return m_scrollOnKeystroke; }
    void setScrollOnKeystroke(bool value) { m_scrollOnKeystroke = value; }

    // --- поведение ---
    //! Стартовый каталог новой оболочки (пусто — последний использованный)
    QString startDirectory() const { return m_startDirectory; }
    void setStartDirectory(const QString &value) { m_startDirectory = value; }

    //! Восстанавливать вкладки, панели и каталоги при запуске
    bool restoreSession() const { return m_restoreSession; }
    void setRestoreSession(bool value) { m_restoreSession = value; }

    //! Новая вкладка открывается в каталоге активной панели
    bool newTabInCurrentDirectory() const { return m_newTabInCurrentDirectory; }
    void setNewTabInCurrentDirectory(bool value) { m_newTabInCurrentDirectory = value; }

    //! Предупреждать о закрытии вкладки/панели с запущенным процессом
    bool warnOnCloseRunning() const { return m_warnOnCloseRunning; }
    void setWarnOnCloseRunning(bool value) { m_warnOnCloseRunning = value; }

    //! Предупреждать о закрытии окна с запущенными процессами
    bool confirmCloseWindow() const { return m_confirmCloseWindow; }
    void setConfirmCloseWindow(bool value) { m_confirmCloseWindow = value; }

    //! Сохранять настройки сразу после изменения
    bool autoSave() const { return m_autoSave; }
    void setAutoSave(bool value) { m_autoSave = value; }

    bool confirmMultilinePaste() const { return m_confirmMultilinePaste; }
    void setConfirmMultilinePaste(bool value) { m_confirmMultilinePaste = value; }

    bool bellAudible() const { return m_bellAudible; }
    void setBellAudible(bool value) { m_bellAudible = value; }

    QString lastWorkingDirectory() const { return m_lastWorkingDirectory; }
    void setLastWorkingDirectory(const QString &value) { m_lastWorkingDirectory = value; }

    // --- значения по умолчанию и границы ---
    static QString defaultShell();

    //! Прочитать масштаб интерфейса до создания QApplication (для QT_SCALE_FACTOR)
    static qreal storedUiScale();

    static constexpr int kMinFontSize = 6;
    static constexpr int kMaxFontSize = 48;
    static constexpr int kMinScrollback = 100;
    static constexpr int kMaxScrollback = 100000;
    static constexpr qreal kMinLineSpacing = 1.0;
    static constexpr qreal kMaxLineSpacing = 1.6;
    static constexpr int kMinPadding = 0;
    static constexpr int kMaxPadding = 32;
    static constexpr int kMinOpacity = 60;
    static constexpr int kMaxOpacity = 100;
    static constexpr qreal kMinUiScale = 0.75;
    static constexpr qreal kMaxUiScale = 2.0;
    static constexpr int kMinWheelLines = 1;
    static constexpr int kMaxWheelLines = 10;
    static constexpr int kMinMonitorInterval = 500;
    static constexpr int kMaxMonitorInterval = 10000;

private:
    Settings() = default;

    QString m_fontFamily;
    int m_fontSize = 11;
    qreal m_lineSpacing = 1.0;
    int m_padding = 6;
    QString m_themeId;
    CursorStyle m_cursorStyle = CursorStyle::FollowApplication;
    int m_windowOpacity = 100;
    bool m_useSystemTitleBar = false;
    bool m_sidebarVisible = true;
    qreal m_uiScale = 1.0;
    QString m_shell;
    int m_scrollbackLines = 3000;
    bool m_confirmMultilinePaste = true;
    bool m_bellAudible = true;
    QString m_lastWorkingDirectory;
    bool m_cursorBlink = true;
    bool m_statusBarVisible = true;
    bool m_menuBarVisible = true;
    bool m_tabBarVisible = true;
    bool m_devPanelVisible = false;
    bool m_monitorPanelVisible = false;
    int m_monitorIntervalMs = 1500;
    bool m_recordHistory = true;
    int m_wheelScrollLines = 3;
    bool m_scrollOnOutput = true;
    bool m_scrollOnKeystroke = true;
    QString m_startDirectory;
    bool m_restoreSession = true;
    bool m_newTabInCurrentDirectory = true;
    bool m_warnOnCloseRunning = true;
    bool m_confirmCloseWindow = true;
    bool m_autoSave = true;
};

} // namespace BlackCore
