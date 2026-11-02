// SPDX-License-Identifier: MIT
#pragma once

#include "../core/History.h"
#include "../core/Notifications.h"
#include "CommandPalette.h"
#include "SettingsDialog.h"

#include <QHash>

#include <QList>
#include <QMainWindow>
#include <QPoint>
#include <QRect>

class QLabel;
class QMenu;
class QSystemTrayIcon;
class QMenuBar;
class QSplitter;
class QTimer;
class QToolButton;

namespace BlackCore {

class CommandLibraryDialog;
class CommandPalette;
class DevPanel;
class HistoryDialog;
class MonitorPanel;
class NotificationToast;
class TaskTracker;
class SideBar;
class TerminalTabs;
class TerminalView;
class TitleBar;

//! Главное окно BlackCore: фирменный заголовок, вкладки, боковая панель, статус-строка.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void setInitialCommand(const QString &command);
    void startSession();

    //! Открыть новую вкладку (при command — вместо интерактивной оболочки)
    void newTab(const QString &command = QString());

    TerminalView *currentTerminal() const;
    TerminalTabs *tabs() const { return m_tabs; }
    SideBar *sideBar() const { return m_sideBar; }
    TitleBar *titleBar() const { return m_titleBar; }

    void openSettings();

    //! Открыть рабочее пространство (набор панелей) по идентификатору
    void openWorkspace(const QString &workspaceId);
    //! Разделить активную панель
    void splitCurrentPane(Qt::Orientation orientation);
    //! Закрыть активную панель (последнюю — вместе с вкладкой)
    void closeCurrentPane();
    //! Сохранить текущую раскладку как рабочее пространство
    void saveCurrentWorkspace();

    //! Сохранить состояние вкладок (вызывается при выходе)
    void saveSession();
    //! Восстановить сессию, если она есть и включена
    bool restoreSession();

    // --- этап 4: настройки ---
    //! Применить настройки внешнего вида и поведения
    void applySettings(const AppearanceSettings &appearance, const BehaviorSettings &behavior,
                       bool persist);
    //! Назначить горячие клавиши из ShortcutManager
    void applyShortcuts();
    //! Открыть файл конфигурации в проводнике/редакторе
    void openConfigFile();
    void exportConfig();
    void importConfig();
    void resetAllSettings();

    // --- этап 5: инструменты разработчика ---
    DevPanel *devPanel() const { return m_devPanel; }
    //! Показать/скрыть панель разработчика
    void setDevPanelVisible(bool visible);
    bool isDevPanelVisible() const;
    //! Открыть SSH-подключение из профиля в новой вкладке
    void connectSshProfile(const QString &profileId);
    //! Открыть проект: раскладка проекта или новая вкладка в его каталоге
    void openProject(const QString &projectId);
    //! Открыть терминал в каталоге в новой вкладке
    void openDirectoryInNewTab(const QString &directory);
    //! Вставить текст в активный терминал (без Enter)
    void insertIntoTerminal(const QString &text);
    //! Выполнить команду в активном терминале (с Enter)
    void executeInTerminal(const QString &command);
    //! Добавить проект из текущего каталога
    void addCurrentDirectoryAsProject();
    //! Статус Git для строки состояния
    QString gitStatusLabel() const;

    // --- этап 6: мониторинг и диагностика ---
    MonitorPanel *monitorPanel() const { return m_monitorPanel; }
    //! Показать/скрыть панель монитора
    void setMonitorPanelVisible(bool visible);
    bool isMonitorPanelVisible() const;

    // --- этап 7: палитра, библиотека команд, история, уведомления ---
    //! Показать командную палитру
    void openCommandPalette();
    CommandPalette *commandPalette() const { return m_palette; }
    //! Пункты палитры, собранные из действий, тем, пространств, SSH и команд
    QList<PaletteItem> paletteItems() const;
    //! Выполнить пункт палитры по идентификатору
    bool triggerPaletteItem(const QString &id);

    void openCommandLibrary();
    CommandLibraryDialog *commandLibrary() const { return m_libraryDialog; }

    void openHistory(int section = 0);
    HistoryDialog *historyDialog() const { return m_historyDialog; }

    //! Выполнить сохранённую команду. confirmed=true — подтверждение уже получено.
    bool runSavedCommand(const QString &id, bool confirmed = false);
    //! Выполнить произвольный текст в активном терминале с учётом истории и уведомлений
    bool runCommandText(const QString &command, CommandSource source,
                        const QString &comment = QString(), bool script = false);
    //! Показать текст команды и запросить подтверждение (true — можно выполнять)
    bool confirmCommand(const QString &title, const QString &command, bool destructive);

    //! Количество активных отслеживаемых задач (для тестов)
    int activeTaskCount() const;
    TaskTracker *taskTracker() const { return m_tasks; }
    NotificationToast *notificationToast() const { return m_toast; }

    //! Последнее показанное уведомление (для тестов)
    Notification lastNotification() const { return m_lastNotification; }

    //! Последний применённый набор внешнего вида (для тестов и предпросмотра)
    AppearanceSettings appearanceValues() const { return m_appearance; }
    BehaviorSettings behaviorValues() const { return m_behavior; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    //! Отложенное сохранение настроек (автосохранение)
    void scheduleSettingsSave();

private slots:
    void onTabChanged(int index);
    void onTabCountChanged(int count);
    void onTabCloseRequested(int index);
    void onSessionFinished(int exitCode, int exitSignal);
    void onTerminalCreated(BlackCore::TerminalView *terminal, bool startNow);
    void onPaneCloseRequested(BlackCore::TerminalView *terminal);
    void onTabRenameRequested(int index);
    void onActivePaneChanged(BlackCore::TerminalView *terminal);
    void onTaskFinished(int id, const QString &command, const QString &tag, int durationMs,
                        int exitCode, bool script);
    void updateStatusBar();
    void showAbout();
    void newWindow();

private:
    void createActions();
    void createMenus();
    void setupStage7();
    void createCentralUi();
    void createStatusBar();
    void applyWindowChrome();
    void applyAppearance(const AppearanceSettings &values, bool persist);
    void setSidebarVisible(bool visible, bool animate);
    void updateSidebar();
    void updateWindowTitle();
    void wireTerminal(TerminalView *terminal);
    bool startTerminal(TerminalView *terminal);
    void rebuildWorkspaceMenu();
    //! Есть ли в оболочке дочерние процессы (запущенная программа)
    static bool hasRunningChild(pid_t pid);
    //! Спросить подтверждение у пользователя (true — можно закрывать)
    bool confirmClose(const QList<TerminalView *> &terminals, const QString &actionText);
    void updatePaneActions();
    void applyShortcutBindings();
    QAction *actionFor(const QString &shortcutId) const;
    void switchTabBy(int delta);
    void onSshRequested(const QString &profileId);
    void onProjectRequested(const QString &projectId);
    void updateDevContext();
    void rememberWorkingDirectory();
    void restoreGeometryFromSettings();
    void saveGeometryToSettings();
    QString currentWorkingDirectory() const;

    //! Границы окна для изменения размера без системной рамки
    Qt::Edges edgesAt(const QPoint &localPosition) const;
    void updateResizeCursor(const QPoint &localPosition);

    TitleBar *m_titleBar = nullptr;
    QMenuBar *m_menuBar = nullptr;
    SideBar *m_sideBar = nullptr;
    TerminalTabs *m_tabs = nullptr;
    QSplitter *m_splitter = nullptr;

    QLabel *m_statusInfo = nullptr;
    QLabel *m_statusSession = nullptr;
    QTimer *m_statusTimer = nullptr;

    QMenu *m_themeMenu = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QAction *m_newTabAction = nullptr;
    QAction *m_closeTabAction = nullptr;
    QAction *m_newWindowAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_zoomResetAction = nullptr;
    QAction *m_clearAction = nullptr;
    QAction *m_clearScrollbackAction = nullptr;
    QAction *m_sidebarAction = nullptr;
    QAction *m_menuBarAction = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_shellAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_logFileAction = nullptr;

    // --- этап 3: вкладки, панели, рабочие пространства ---
    QMenu *m_tabMenu = nullptr;
    QMenu *m_paneMenu = nullptr;
    QMenu *m_workspaceMenu = nullptr;
    QAction *m_renameTabAction = nullptr;
    QAction *m_duplicateTabAction = nullptr;
    QAction *m_nextTabAction = nullptr;
    QAction *m_previousTabAction = nullptr;
    QAction *m_splitVerticalAction = nullptr;
    QAction *m_splitHorizontalAction = nullptr;
    QAction *m_closePaneAction = nullptr;
    QAction *m_focusNextPaneAction = nullptr;
    QAction *m_focusPrevPaneAction = nullptr;
    QAction *m_focusLeftPaneAction = nullptr;
    QAction *m_focusRightPaneAction = nullptr;
    QAction *m_focusUpPaneAction = nullptr;
    QAction *m_focusDownPaneAction = nullptr;
    QAction *m_saveWorkspaceAction = nullptr;
    QAction *m_restoreSessionAction = nullptr;

    // --- этап 4 ---
    QMenu *m_configMenu = nullptr;
    QAction *m_exportConfigAction = nullptr;
    QAction *m_importConfigAction = nullptr;
    QAction *m_resetConfigAction = nullptr;
    QAction *m_openConfigAction = nullptr;
    QAction *m_autoSaveAction = nullptr;
    QTimer *m_saveTimer = nullptr;
    QHash<QString, QAction *> m_shortcutActions;

    // --- этап 5 ---
    DevPanel *m_devPanel = nullptr;
    QAction *m_devPanelAction = nullptr;
    QMenu *m_devMenu = nullptr;
    QAction *m_sshConnectAction = nullptr;
    QAction *m_projectOpenAction = nullptr;
    QAction *m_addProjectAction = nullptr;
    QLabel *m_statusGit = nullptr;

    // --- этап 6 ---
    MonitorPanel *m_monitorPanel = nullptr;
    QAction *m_monitorPanelAction = nullptr;
    QMenu *m_monitorMenu = nullptr;
    QAction *m_monitorOverviewAction = nullptr;
    QAction *m_monitorProcessesAction = nullptr;
    QAction *m_monitorDiagnosticsAction = nullptr;
    QAction *m_monitorRefreshAction = nullptr;
    QAction *m_monitorLogAction = nullptr;
    QLabel *m_statusMonitor = nullptr;

    // --- этап 7 ---
    CommandPalette *m_palette = nullptr;
    CommandLibraryDialog *m_libraryDialog = nullptr;
    HistoryDialog *m_historyDialog = nullptr;
    NotificationToast *m_toast = nullptr;
    TaskTracker *m_tasks = nullptr;
    QMenu *m_toolsMenu = nullptr;
    QMenu *m_notificationsMenu = nullptr;
    QAction *m_paletteAction = nullptr;
    QAction *m_libraryAction = nullptr;
    QAction *m_historyAction = nullptr;
    QAction *m_notificationsAction = nullptr;
    QAction *m_notificationsEnabledAction = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QHash<TerminalView *, int> m_terminalIds;
    QHash<TerminalView *, QString> m_sshSessions;
    int m_nextTerminalId = 1;
    Notification m_lastNotification;
    QString m_lastTrackedHistoryId;
    AppearanceSettings m_appearance;
    BehaviorSettings m_behavior;
    QList<QAction *> m_tabIndexActions;

    QString m_initialCommand;

    bool m_customTitleBarActive = true;
    bool m_resizing = false;
    Qt::Edges m_resizeEdges;
    QPoint m_resizeStartGlobal;
    QRect m_resizeStartGeometry;
};

} // namespace BlackCore
