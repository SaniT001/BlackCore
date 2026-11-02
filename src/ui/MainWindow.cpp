// SPDX-License-Identifier: MIT
#include "MainWindow.h"

#include "../core/AppInfo.h"
#include "../core/Logger.h"
#include "../core/PaneLayout.h"
#include "../core/Profile.h"
#include "../core/ConfigIO.h"
#include "../core/GitInfo.h"
#include "../core/Project.h"
#include "../core/SshProfile.h"
#include "../core/SessionStore.h"
#include "../core/Shortcuts.h"
#include "../core/Settings.h"
#include "../core/Theme.h"
#include "../core/Workspace.h"
#include "../terminal/TerminalView.h"
#include "SideBar.h"
#include "DevPanel.h"
#include "CommandLibraryDialog.h"
#include "HistoryDialog.h"
#include "MonitorPanel.h"
#include "../core/CommandLibrary.h"
#include "../core/Secrets.h"
#include "../core/TaskTracker.h"
#include "NotificationToast.h"
#include "PaneFrame.h"
#include "PaneTree.h"
#include "TerminalTabs.h"
#include "TitleBar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QAbstractButton>
#include <QMessageBox>
#include <QMenuBar>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <unistd.h>

namespace BlackCore {
namespace {

constexpr int kResizeBorder = 6;

QString readProcessWorkingDirectory(pid_t pid)
{
    if (pid <= 0)
        return QString();
    const QString link = QStringLiteral("/proc/%1/cwd").arg(pid);
    const QString path = QString::fromLocal8Bit(QFile::symLinkTarget(link).toUtf8());
    return QFileInfo(path).isDir() ? path : QString();
}

//! Экранирование пути для вставки в приглашение оболочки
QString quoteForShell(const QString &path)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9._@%+,=:~/^-]+$"));
    if (!path.isEmpty() && safe.match(path).hasMatch())
        return path;
    QString escaped = path;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

//! Дочерние процессы оболочки: если они есть — запущена программа, а не просто приглашение
bool mainWindowHasChildren(pid_t pid)
{
    if (pid <= 0)
        return false;
    const QString path = QStringLiteral("/proc/%1/task/%1/children").arg(pid);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return !file.readAll().trimmed().isEmpty();
}

} // namespace

//! Экранирование пути (доступно всему файлу)
static QString shellQuote(const QString &path)
{
    return quoteForShell(path);
}

//! Команда с переходом в рабочий каталог: одна строка — через &&, сценарий — в подоболочке
static QString composeWorkingDirectory(const QString &directory, const QString &command)
{
    const QString quoted = quoteForShell(directory);
    if (!command.contains(QLatin1Char('\n')))
        return QStringLiteral("cd %1 && %2").arg(quoted, command);
    return QStringLiteral("( cd %1 &&\n%2\n)").arg(quoted, command);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setMinimumSize(720, 440);
    setWindowTitle(fullName());
    setWindowIcon(QIcon(QStringLiteral(":/icons/blackcore-256.png")));

    createActions();
    createCentralUi();
    createMenus();
    createStatusBar();

    setupStage7();

    applyWindowChrome();
    applySettings(AppearanceSettings::fromSettings(), BehaviorSettings::fromSettings(), false);
    applyShortcuts();

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        Settings::instance().save();
        m_saveTimer->stop();
    });

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusBar);
    m_statusTimer->start();

    restoreGeometryFromSettings();
    updateStatusBar();

    // Панели могли быть открыты в прошлой сессии — приводим размеры в порядок
    if (Settings::instance().devPanelVisible())
        setDevPanelVisible(true);
    if (Settings::instance().monitorPanelVisible())
        setMonitorPanelVisible(true);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Построение интерфейса
// ---------------------------------------------------------------------------

void MainWindow::createCentralUi()
{
    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Заголовок окна и меню — один блок в области меню QMainWindow.
    // Так порядок «заголовок → меню» сохраняется в любом оконном менеджере.
    auto *topArea = new QWidget(this);
    auto *topLayout = new QVBoxLayout(topArea);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);

    m_titleBar = new TitleBar(topArea);
    topLayout->addWidget(m_titleBar);

    m_menuBar = new QMenuBar(topArea);
    m_menuBar->setNativeMenuBar(false);
    topLayout->addWidget(m_menuBar);

    setMenuWidget(topArea);

    connect(m_titleBar, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeRestoreRequested, this, [this]() {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    });
    connect(m_titleBar, &TitleBar::closeRequested, this, &QWidget::close);
    connect(m_titleBar, &TitleBar::sidebarToggled, this, [this](bool visible) {
        setSidebarVisible(visible, true);
    });

    m_sideBar = new SideBar(root);
    m_tabs = new TerminalTabs(root);
    m_devPanel = new DevPanel(root);
    m_devPanel->setVisible(Settings::instance().devPanelVisible());

    // --- этап 6: панель монитора и диагностики ---
    m_monitorPanel = new MonitorPanel(root);
    m_monitorPanel->setRefreshInterval(Settings::instance().monitorIntervalMs());
    m_monitorPanel->setVisible(Settings::instance().monitorPanelVisible());

    m_splitter = new QSplitter(Qt::Horizontal, root);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_splitter->addWidget(m_sideBar);
    m_splitter->addWidget(m_tabs);
    m_splitter->addWidget(m_devPanel);
    m_splitter->addWidget(m_monitorPanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setStretchFactor(3, 0);
    m_splitter->setSizes({230, 760, 0, 0});
    rootLayout->addWidget(m_splitter, 1);

    setCentralWidget(root);

    // --- этап 5: панель разработчика ---
    connect(m_devPanel, &DevPanel::commandRequested, this, [this](const QString &command) {
        insertIntoTerminal(command);
    });
    connect(m_devPanel, &DevPanel::commandExecuted, this, [this](const QString &command) {
        executeInTerminal(command);
    });
    connect(m_devPanel, &DevPanel::changeDirectoryRequested, this, [this](const QString &directory) {
        insertIntoTerminal(QStringLiteral("cd %1").arg(shellQuote(directory)));
    });
    connect(m_devPanel, &DevPanel::openDirectoryRequested, this, &MainWindow::openDirectoryInNewTab);
    connect(m_devPanel, &DevPanel::sshRequested, this, &MainWindow::onSshRequested);
    connect(m_devPanel, &DevPanel::openProjectRequested, this, &MainWindow::onProjectRequested);
    connect(m_devPanel, &DevPanel::statusMessage, this, [this](const QString &text) {
        statusBar()->showMessage(text, 5000);
    });

    // --- этап 6: панель монитора ---
    connect(m_monitorPanel, &MonitorPanel::commandRequested, this, [this](const QString &command) {
        insertIntoTerminal(command);
    });
    connect(m_monitorPanel, &MonitorPanel::statusMessage, this, [this](const QString &text) {
        statusBar()->showMessage(text, 5000);
    });
    connect(m_monitorPanel, &MonitorPanel::overviewUpdated, this, [this]() {
        if (!m_statusMonitor || !m_monitorPanel || !m_monitorPanel->isVisible())
            return;
        const SystemOverview &overview = m_monitorPanel->overview();
        m_statusMonitor->setText(QStringLiteral("CPU %1 · RAM %2")
                                     .arg(SystemMonitor::formatPercent(overview.cpu.total),
                                          SystemMonitor::formatPercent(overview.memory.usedPercent())));
        m_statusMonitor->setToolTip(tr("Процессор: %1\nПамять: %2 из %3\nНагрузка: %4\nЯдро: %5")
                                        .arg(SystemMonitor::formatPercent(overview.cpu.total),
                                             SystemMonitor::formatBytes(overview.memory.usedKb() * 1024ULL),
                                             SystemMonitor::formatBytes(overview.memory.totalKb * 1024ULL),
                                             overview.loadText(),
                                             overview.kernel));
    });

    connect(m_tabs, &TerminalTabs::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &TerminalTabs::tabCountChanged, this, &MainWindow::onTabCountChanged);
    connect(m_tabs, &TerminalTabs::newTabRequested, this, [this]() { newTab(); });
    connect(m_tabs, &TerminalTabs::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(m_tabs, &TerminalTabs::tabRenameRequested, this, &MainWindow::onTabRenameRequested);
    connect(m_tabs, &TerminalTabs::paneCloseRequested, this, &MainWindow::onPaneCloseRequested);
    connect(m_tabs, &TerminalTabs::terminalCreated, this, &MainWindow::onTerminalCreated);
    connect(m_tabs, &TerminalTabs::activePaneChanged, this, &MainWindow::onActivePaneChanged);
    connect(m_tabs, &TerminalTabs::tabCountChanged, this, [this](int) { updatePaneActions(); });

    connect(m_sideBar, &SideBar::newTabRequested, this, [this]() { newTab(); });
    connect(m_sideBar, &SideBar::sessionActivated, this, [this](int index) {
        if (m_tabs && index != m_tabs->currentIndex())
            m_tabs->setCurrentIndex(index);
    });
    connect(m_sideBar, &SideBar::copyRequested, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->copySelection();
    });
    connect(m_sideBar, &SideBar::pasteRequested, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->pasteClipboard();
    });
    connect(m_sideBar, &SideBar::executeRequested, this, [this]() {
        if (auto *terminal = currentTerminal()) {
            terminal->sendTextToShell(QStringLiteral("\r"));
            terminal->setFocus();
        }
    });
    connect(m_sideBar, &SideBar::clearRequested, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->clearScreen();
    });
    connect(m_sideBar, &SideBar::settingsRequested, this, &MainWindow::openSettings);
}

void MainWindow::createActions()
{
    m_newTabAction = new QAction(tr("Новая вкладка"), this);
    m_newTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    connect(m_newTabAction, &QAction::triggered, this, [this]() { newTab(); });

    m_closeTabAction = new QAction(tr("Закрыть вкладку"), this);
    m_closeTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(m_closeTabAction, &QAction::triggered, this, [this]() {
        if (m_tabs)
            onTabCloseRequested(m_tabs->currentIndex());
    });

    // Ctrl+C/Ctrl+V намеренно не перехватываются: они уходят в оболочку
    m_copyAction = new QAction(tr("Копировать"), this);
    m_copyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    m_copyAction->setEnabled(false);
    connect(m_copyAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->copySelection();
    });

    m_pasteAction = new QAction(tr("Вставить"), this);
    m_pasteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(m_pasteAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->pasteClipboard();
    });

    m_selectAllAction = new QAction(tr("Выделить всё"), this);
    m_selectAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    connect(m_selectAllAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->selectAll();
    });

    m_clearAction = new QAction(tr("Очистить экран"), this);
    m_clearAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    connect(m_clearAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->clearScreen();
    });

    m_clearScrollbackAction = new QAction(tr("Очистить прокрутку"), this);
    connect(m_clearScrollbackAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal())
            terminal->clearScrollback();
    });

    m_zoomInAction = new QAction(tr("Увеличить шрифт"), this);
    m_zoomInAction->setShortcuts({QKeySequence(QStringLiteral("Ctrl++")),
                                  QKeySequence(QStringLiteral("Ctrl+="))});
    connect(m_zoomInAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal()) {
            terminal->zoomIn();
            updateStatusBar();
        }
    });

    m_zoomOutAction = new QAction(tr("Уменьшить шрифт"), this);
    m_zoomOutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    connect(m_zoomOutAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal()) {
            terminal->zoomOut();
            updateStatusBar();
        }
    });

    m_zoomResetAction = new QAction(tr("Сбросить масштаб"), this);
    m_zoomResetAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(m_zoomResetAction, &QAction::triggered, this, [this]() {
        if (auto *terminal = currentTerminal()) {
            terminal->zoomReset();
            updateStatusBar();
        }
    });

    m_sidebarAction = new QAction(tr("Боковая панель"), this);
    m_sidebarAction->setCheckable(true);
    m_sidebarAction->setChecked(Settings::instance().sidebarVisible());
    m_sidebarAction->setShortcut(QKeySequence(QStringLiteral("F9")));
    connect(m_sidebarAction, &QAction::triggered, this, [this](bool checked) {
        setSidebarVisible(checked, true);
    });

    m_menuBarAction = new QAction(tr("Главное меню"), this);
    m_menuBarAction->setCheckable(true);
    m_menuBarAction->setChecked(Settings::instance().menuBarVisible());
    m_menuBarAction->setToolTip(tr("Скрывает и показывает главное меню (Ctrl+Shift+M)"));
    // Действие должно работать и при скрытом меню, поэтому оно принадлежит окну,
    // а не только пункту меню.
    addAction(m_menuBarAction);
    connect(m_menuBarAction, &QAction::toggled, this, [this](bool visible) {
        if (m_menuBar)
            m_menuBar->setVisible(visible);
        Settings &settings = Settings::instance();
        settings.setMenuBarVisible(visible);
        m_appearance.menuBarVisible = visible;
        scheduleSettingsSave();
    });

    m_fullScreenAction = new QAction(tr("Полноэкранный режим"), this);
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(QStringLiteral("F11")));
    connect(m_fullScreenAction, &QAction::toggled, this, [this](bool enabled) {
        if (enabled)
            showFullScreen();
        else
            showNormal();
    });

    m_settingsAction = new QAction(tr("Настройки внешнего вида…"), this);
    m_settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    m_shellAction = new QAction(tr("Оболочка: %1").arg(Settings::instance().shell()), this);
    m_shellAction->setEnabled(false);

    m_newWindowAction = new QAction(tr("Новое окно"), this);
    m_newWindowAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    connect(m_newWindowAction, &QAction::triggered, this, &MainWindow::newWindow);

    m_quitAction = new QAction(tr("Выход"), this);
    m_quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Q")));
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_aboutAction = new QAction(tr("О программе"), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    // --- этап 3: вкладки ---
    m_renameTabAction = new QAction(tr("Переименовать вкладку…"), this);
    m_renameTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(m_renameTabAction, &QAction::triggered, this, [this]() {
        if (m_tabs)
            onTabRenameRequested(m_tabs->currentIndex());
    });

    m_duplicateTabAction = new QAction(tr("Дублировать вкладку"), this);
    m_duplicateTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+U")));
    m_duplicateTabAction->setToolTip(tr("Новая вкладка с той же раскладкой и каталогами"));
    connect(m_duplicateTabAction, &QAction::triggered, this, [this]() {
        if (!m_tabs)
            return;
        if (m_tabs->duplicateTab(m_tabs->currentIndex()))
            onTabChanged(m_tabs->currentIndex());
    });

    m_nextTabAction = new QAction(tr("Следующая вкладка"), this);
    m_nextTabAction->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Tab")),
                                   QKeySequence(QStringLiteral("Ctrl+PgDown"))});
    connect(m_nextTabAction, &QAction::triggered, this, [this]() { switchTabBy(1); });

    m_previousTabAction = new QAction(tr("Предыдущая вкладка"), this);
    m_previousTabAction->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
                                       QKeySequence(QStringLiteral("Ctrl+PgUp"))});
    connect(m_previousTabAction, &QAction::triggered, this, [this]() { switchTabBy(-1); });

    for (int i = 1; i <= 9; ++i) {
        auto *action = new QAction(tr("Вкладка %1").arg(i), this);
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i)));
        connect(action, &QAction::triggered, this, [this, i]() {
            if (m_tabs && i <= m_tabs->count())
                m_tabs->setCurrentIndex(i - 1);
        });
        m_tabIndexActions.append(action);
        addAction(action);
    }

    // --- этап 3: панели ---
    m_splitVerticalAction = new QAction(tr("Разделить вертикально"), this);
    m_splitVerticalAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    m_splitVerticalAction->setToolTip(tr("Две панели рядом (Ctrl+Shift+D)"));
    connect(m_splitVerticalAction, &QAction::triggered, this, [this]() {
        splitCurrentPane(Qt::Horizontal);
    });

    m_splitHorizontalAction = new QAction(tr("Разделить горизонтально"), this);
    m_splitHorizontalAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    m_splitHorizontalAction->setToolTip(tr("Панели друг под другом (Ctrl+Shift+E)"));
    connect(m_splitHorizontalAction, &QAction::triggered, this, [this]() {
        splitCurrentPane(Qt::Vertical);
    });

    m_closePaneAction = new QAction(tr("Закрыть панель"), this);
    m_closePaneAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    connect(m_closePaneAction, &QAction::triggered, this, &MainWindow::closeCurrentPane);

    m_focusNextPaneAction = new QAction(tr("Следующая панель"), this);
    m_focusNextPaneAction->setShortcut(QKeySequence(QStringLiteral("F6")));
    connect(m_focusNextPaneAction, &QAction::triggered, this, [this]() {
        if (PaneTree *tree = m_tabs ? m_tabs->currentTab() : nullptr)
            tree->focusNextPane(1);
    });

    m_focusPrevPaneAction = new QAction(tr("Предыдущая панель"), this);
    m_focusPrevPaneAction->setShortcut(QKeySequence(QStringLiteral("Shift+F6")));
    connect(m_focusPrevPaneAction, &QAction::triggered, this, [this]() {
        if (PaneTree *tree = m_tabs ? m_tabs->currentTab() : nullptr)
            tree->focusNextPane(-1);
    });

    auto makeFocusAction = [this](const QString &text, const QString &shortcut,
                                  Qt::Edge edge) -> QAction * {
        auto *action = new QAction(text, this);
        action->setShortcut(QKeySequence(shortcut));
        connect(action, &QAction::triggered, this, [this, edge]() {
            if (PaneTree *tree = m_tabs ? m_tabs->currentTab() : nullptr)
                tree->focusDirection(edge);
        });
        return action;
    };
    m_focusLeftPaneAction = makeFocusAction(tr("Панель слева"), QStringLiteral("Ctrl+Shift+Left"), Qt::LeftEdge);
    m_focusRightPaneAction = makeFocusAction(tr("Панель справа"), QStringLiteral("Ctrl+Shift+Right"), Qt::RightEdge);
    m_focusUpPaneAction = makeFocusAction(tr("Панель сверху"), QStringLiteral("Ctrl+Shift+Up"), Qt::TopEdge);
    m_focusDownPaneAction = makeFocusAction(tr("Панель снизу"), QStringLiteral("Ctrl+Shift+Down"), Qt::BottomEdge);

    // --- этап 3: рабочие пространства и сессии ---
    m_saveWorkspaceAction = new QAction(tr("Сохранить как рабочее пространство…"), this);
    connect(m_saveWorkspaceAction, &QAction::triggered, this, &MainWindow::saveCurrentWorkspace);

    m_restoreSessionAction = new QAction(tr("Восстанавливать сессию при запуске"), this);
    m_restoreSessionAction->setCheckable(true);
    m_restoreSessionAction->setChecked(SessionStore::restoreEnabled());
    connect(m_restoreSessionAction, &QAction::toggled, this, [](bool enabled) {
        SessionStore::setRestoreEnabled(enabled);
    });

    // --- этап 4: конфигурация ---
    m_exportConfigAction = new QAction(tr("Экспорт настроек…"), this);
    m_exportConfigAction->setToolTip(tr("Сохранить настройки, горячие клавиши, профили "
                                        "и рабочие пространства в файл JSON"));
    connect(m_exportConfigAction, &QAction::triggered, this, &MainWindow::exportConfig);

    m_importConfigAction = new QAction(tr("Импорт настроек…"), this);
    m_importConfigAction->setToolTip(tr("Проверить файл, создать резервную копию и применить"));
    connect(m_importConfigAction, &QAction::triggered, this, &MainWindow::importConfig);

    m_resetConfigAction = new QAction(tr("Сбросить настройки…"), this);
    connect(m_resetConfigAction, &QAction::triggered, this, &MainWindow::resetAllSettings);

    m_openConfigAction = new QAction(tr("Открыть файл настроек"), this);
    connect(m_openConfigAction, &QAction::triggered, this, &MainWindow::openConfigFile);

    m_autoSaveAction = new QAction(tr("Автосохранение настроек"), this);
    m_autoSaveAction->setCheckable(true);
    m_autoSaveAction->setChecked(Settings::instance().autoSave());
    connect(m_autoSaveAction, &QAction::toggled, this, [this](bool enabled) {
        Settings &settings = Settings::instance();
        settings.setAutoSave(enabled);
        m_behavior.autoSave = enabled;
        if (enabled)
            scheduleSettingsSave();
        updateStatusBar();
    });

    // --- этап 5: инструменты разработчика ---
    m_devPanelAction = new QAction(tr("Панель разработчика"), this);
    m_devPanelAction->setCheckable(true);
    m_devPanelAction->setShortcut(QKeySequence(QStringLiteral("F8")));
    m_devPanelAction->setToolTip(tr("Git, SSH-подключения и проекты (F8)"));
    connect(m_devPanelAction, &QAction::toggled, this, [this](bool visible) {
        setDevPanelVisible(visible);
    });

    m_sshConnectAction = new QAction(tr("SSH-подключение…"), this);
    connect(m_sshConnectAction, &QAction::triggered, this, [this]() {
        setDevPanelVisible(true);
        if (m_devPanel)
            m_devPanel->showSection(1);
    });

    m_projectOpenAction = new QAction(tr("Открыть проект…"), this);
    connect(m_projectOpenAction, &QAction::triggered, this, [this]() {
        setDevPanelVisible(true);
        if (m_devPanel)
            m_devPanel->showSection(2);
    });

    m_addProjectAction = new QAction(tr("Добавить текущий каталог в проекты"), this);
    connect(m_addProjectAction, &QAction::triggered, this, &MainWindow::addCurrentDirectoryAsProject);

    // --- этап 6: монитор и диагностика ---
    m_monitorPanelAction = new QAction(tr("Панель монитора"), this);
    m_monitorPanelAction->setCheckable(true);
    m_monitorPanelAction->setShortcut(QKeySequence(QStringLiteral("F7")));
    m_monitorPanelAction->setToolTip(tr("Загрузка CPU, память, диски, сеть и диагностика (F7)"));
    connect(m_monitorPanelAction, &QAction::toggled, this, [this](bool visible) {
        setMonitorPanelVisible(visible);
    });

    m_monitorOverviewAction = new QAction(tr("Обзор системы"), this);
    connect(m_monitorOverviewAction, &QAction::triggered, this, [this]() {
        setMonitorPanelVisible(true);
        if (m_monitorPanel)
            m_monitorPanel->showSection(0);
    });

    m_monitorProcessesAction = new QAction(tr("Ресурсоёмкие процессы"), this);
    connect(m_monitorProcessesAction, &QAction::triggered, this, [this]() {
        setMonitorPanelVisible(true);
        if (m_monitorPanel) {
            m_monitorPanel->showSection(1);
            m_monitorPanel->refreshProcesses(false);
        }
    });

    m_monitorDiagnosticsAction = new QAction(tr("Диагностика и журнал"), this);
    connect(m_monitorDiagnosticsAction, &QAction::triggered, this, [this]() {
        setMonitorPanelVisible(true);
        if (m_monitorPanel) {
            m_monitorPanel->showSection(2);
            m_monitorPanel->runDiagnostics();
        }
    });

    m_monitorRefreshAction = new QAction(tr("Обновить показатели"), this);
    m_monitorRefreshAction->setShortcut(QKeySequence(QStringLiteral("F5")));
    connect(m_monitorRefreshAction, &QAction::triggered, this, [this]() {
        if (!m_monitorPanel)
            return;
        m_monitorPanel->refresh();
        if (m_monitorPanel->currentSection() == 1)
            m_monitorPanel->refreshProcesses(false);
        statusBar()->showMessage(tr("Показатели обновлены"), 2000);
    });

    m_monitorLogAction = new QAction(tr("Журнал BlackCore"), this);
    connect(m_monitorLogAction, &QAction::triggered, this, [this]() {
        setMonitorPanelVisible(true);
        if (m_monitorPanel) {
            m_monitorPanel->showSection(2);
            m_monitorPanel->refreshLog();
        }
    });

    // --- этап 7: палитра, библиотека команд, история, уведомления ---
    m_paletteAction = new QAction(tr("Командная палитра"), this);
    m_paletteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    m_paletteAction->setToolTip(tr("Быстрый доступ ко всем действиям (Ctrl+Shift+P)"));
    connect(m_paletteAction, &QAction::triggered, this, &MainWindow::openCommandPalette);

    m_libraryAction = new QAction(tr("Библиотека команд"), this);
    m_libraryAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    m_libraryAction->setToolTip(tr("Сохранённые команды и сценарии (Ctrl+Shift+L)"));
    connect(m_libraryAction, &QAction::triggered, this, &MainWindow::openCommandLibrary);

    m_historyAction = new QAction(tr("История и поиск"), this);
    m_historyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    m_historyAction->setToolTip(tr("История команд, поиск по выводу, действия и журнал (Ctrl+Shift+H)"));
    connect(m_historyAction, &QAction::triggered, this, [this]() { openHistory(0); });

    m_notificationsEnabledAction = new QAction(tr("Показывать уведомления"), this);
    m_notificationsEnabledAction->setCheckable(true);
    m_notificationsEnabledAction->setChecked(NotificationCenter::instance().enabled());
    connect(m_notificationsEnabledAction, &QAction::toggled, this, [this](bool enabled) {
        NotificationSettings settings = NotificationCenter::instance().settings();
        settings.enabled = enabled;
        NotificationCenter::instance().setSettings(settings);
        if (m_notificationsAction)
            m_notificationsAction->setChecked(enabled);
        ActivityLog::record(enabled ? tr("Уведомления включены") : tr("Уведомления выключены"),
                            QString(), tr("уведомления"));
        statusBar()->showMessage(enabled ? tr("Уведомления включены")
                                         : tr("Уведомления полностью отключены"), 4000);
    });

    m_notificationsAction = new QAction(tr("Уведомления"), this);
    m_notificationsAction->setCheckable(true);
    m_notificationsAction->setChecked(NotificationCenter::instance().enabled());
    connect(m_notificationsAction, &QAction::toggled, m_notificationsEnabledAction, &QAction::setChecked);
    connect(m_notificationsEnabledAction, &QAction::toggled, m_notificationsAction, &QAction::setChecked);

    m_logFileAction = new QAction(tr("Открыть журнал"), this);
    connect(m_logFileAction, &QAction::triggered, this, [this]() {
        const QString path = Log::filePath();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::information(this, tr("Журнал"), tr("Файл журнала ещё не создан."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = m_menuBar->addMenu(tr("&Файл"));
    fileMenu->addAction(m_newTabAction);
    fileMenu->addAction(m_newWindowAction);
    fileMenu->addAction(m_closeTabAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *editMenu = m_menuBar->addMenu(tr("&Правка"));
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addSeparator();
    editMenu->addAction(m_selectAllAction);
    editMenu->addAction(m_clearAction);
    editMenu->addAction(m_clearScrollbackAction);

    QMenu *viewMenu = m_menuBar->addMenu(tr("&Вид"));
    viewMenu->addAction(m_sidebarAction);
    viewMenu->addAction(m_menuBarAction);
    viewMenu->addAction(m_fullScreenAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_zoomResetAction);
    viewMenu->addSeparator();

    m_themeMenu = viewMenu->addMenu(tr("Тема"));
    for (const Theme &theme : ThemeManager::themes()) {
        QAction *action = m_themeMenu->addAction(theme.name);
        action->setCheckable(true);
        action->setData(theme.id);
        action->setToolTip(theme.description);
        action->setChecked(theme.id == ThemeManager::current().id);
        connect(action, &QAction::triggered, this, [this, id = theme.id]() {
            AppearanceSettings values = AppearanceSettings::fromSettings();
            values.themeId = id;
            applyAppearance(values, true);
        });
    }

    viewMenu->addSeparator();
    viewMenu->addAction(m_settingsAction);

    QMenu *tabMenu = m_menuBar->addMenu(tr("В&кладки"));
    tabMenu->addAction(m_newTabAction);
    tabMenu->addAction(m_renameTabAction);
    tabMenu->addAction(m_duplicateTabAction);
    tabMenu->addAction(m_closeTabAction);
    tabMenu->addSeparator();
    tabMenu->addAction(m_nextTabAction);
    tabMenu->addAction(m_previousTabAction);
    tabMenu->addSeparator();
    for (QAction *action : m_tabIndexActions)
        tabMenu->addAction(action);
    m_tabMenu = tabMenu;

    m_paneMenu = m_menuBar->addMenu(tr("&Панель"));
    m_paneMenu->addAction(m_splitVerticalAction);
    m_paneMenu->addAction(m_splitHorizontalAction);
    m_paneMenu->addAction(m_closePaneAction);
    m_paneMenu->addSeparator();
    m_paneMenu->addAction(m_focusNextPaneAction);
    m_paneMenu->addAction(m_focusPrevPaneAction);
    m_paneMenu->addAction(m_focusLeftPaneAction);
    m_paneMenu->addAction(m_focusRightPaneAction);
    m_paneMenu->addAction(m_focusUpPaneAction);
    m_paneMenu->addAction(m_focusDownPaneAction);

    m_workspaceMenu = m_menuBar->addMenu(tr("Ра&бочие пространства"));
    rebuildWorkspaceMenu();
    m_workspaceMenu->addSeparator();
    m_workspaceMenu->addAction(m_restoreSessionAction);

    m_devMenu = m_menuBar->addMenu(tr("&Разработчик"));
    m_devMenu->addAction(m_devPanelAction);
    m_devMenu->addSeparator();
    m_devMenu->addAction(m_sshConnectAction);
    m_devMenu->addAction(m_projectOpenAction);
    m_devMenu->addAction(m_addProjectAction);

    m_toolsMenu = m_menuBar->addMenu(tr("&Инструменты"));
    m_toolsMenu->addAction(m_paletteAction);
    m_toolsMenu->addAction(m_libraryAction);
    m_toolsMenu->addAction(m_historyAction);
    m_toolsMenu->addSeparator();
    m_notificationsMenu = m_toolsMenu->addMenu(tr("Уведомления"));
    m_notificationsMenu->addAction(m_notificationsEnabledAction);
    m_notificationsMenu->addAction(m_notificationsAction);
    auto *showNotificationsAction = new QAction(tr("История уведомлений"), this);
    connect(showNotificationsAction, &QAction::triggered, this, [this]() { openHistory(4); });
    m_notificationsMenu->addAction(showNotificationsAction);

    m_monitorMenu = m_menuBar->addMenu(tr("&Монитор"));
    m_monitorMenu->addAction(m_monitorPanelAction);
    m_monitorMenu->addSeparator();
    m_monitorMenu->addAction(m_monitorOverviewAction);
    m_monitorMenu->addAction(m_monitorProcessesAction);
    m_monitorMenu->addAction(m_monitorDiagnosticsAction);
    m_monitorMenu->addSeparator();
    m_monitorMenu->addAction(m_monitorRefreshAction);
    m_monitorMenu->addAction(m_monitorLogAction);

    m_configMenu = m_menuBar->addMenu(tr("На&стройки"));
    m_configMenu->addAction(m_settingsAction);
    m_configMenu->addAction(m_autoSaveAction);
    m_configMenu->addSeparator();
    m_configMenu->addAction(m_exportConfigAction);
    m_configMenu->addAction(m_importConfigAction);
    m_configMenu->addAction(m_resetConfigAction);
    m_configMenu->addSeparator();
    m_configMenu->addAction(m_openConfigAction);
    m_configMenu->addAction(m_logFileAction);

    QMenu *helpMenu = m_menuBar->addMenu(tr("&Справка"));
    helpMenu->addAction(m_shellAction);
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::createStatusBar()
{
    m_statusInfo = new QLabel(this);
    m_statusGit = new QLabel(this);
    m_statusGit->setToolTip(tr("Git: ветка и число незакоммиченных изменений"));
    m_statusMonitor = new QLabel(this);
    m_statusMonitor->setToolTip(tr("Загрузка процессора и памяти (F7)"));
    m_statusMonitor->setVisible(Settings::instance().monitorPanelVisible());
    m_statusSession = new QLabel(this);
    statusBar()->addWidget(m_statusInfo, 1);
    statusBar()->addPermanentWidget(m_statusMonitor);
    statusBar()->addPermanentWidget(m_statusGit);
    statusBar()->addPermanentWidget(m_statusSession);
    statusBar()->setSizeGripEnabled(true);
}

// ---------------------------------------------------------------------------
// Оформление окна
// ---------------------------------------------------------------------------

void MainWindow::applyWindowChrome()
{
    const bool customTitleBar = !Settings::instance().useSystemTitleBar();
    const Qt::WindowFlags flags = customTitleBar
        ? (Qt::Window | Qt::FramelessWindowHint)
        : Qt::Window;

    const bool visible = isVisible();
    const QRect previousGeometry = geometry();

    setWindowFlags(flags);
    m_titleBar->setVisible(customTitleBar);
    m_menuBar->setVisible(true);

    if (visible) {
        setGeometry(previousGeometry);
        show();
    }

    if (customTitleBar) {
        // Изменение размера без рамки: следим за курсором у краёв окна
        qApp->installEventFilter(this);
    } else {
        qApp->removeEventFilter(this);
    }
}

Qt::Edges MainWindow::edgesAt(const QPoint &localPosition) const
{
    if (isMaximized() || isFullScreen() || Settings::instance().useSystemTitleBar())
        return {};

    Qt::Edges edges;
    if (localPosition.x() <= kResizeBorder)
        edges |= Qt::LeftEdge;
    if (localPosition.x() >= width() - kResizeBorder)
        edges |= Qt::RightEdge;
    if (localPosition.y() <= kResizeBorder)
        edges |= Qt::TopEdge;
    if (localPosition.y() >= height() - kResizeBorder)
        edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::updateResizeCursor(const QPoint &localPosition)
{
    const Qt::Edges edges = edgesAt(localPosition);
    if (edges == Qt::Edges()) {
        if (!m_resizing)
            unsetCursor();
        return;
    }

    if ((edges & Qt::LeftEdge) && (edges & Qt::TopEdge))
        setCursor(Qt::SizeFDiagCursor);
    else if ((edges & Qt::RightEdge) && (edges & Qt::BottomEdge))
        setCursor(Qt::SizeFDiagCursor);
    else if ((edges & Qt::RightEdge) && (edges & Qt::TopEdge))
        setCursor(Qt::SizeBDiagCursor);
    else if ((edges & Qt::LeftEdge) && (edges & Qt::BottomEdge))
        setCursor(Qt::SizeBDiagCursor);
    else if (edges & (Qt::LeftEdge | Qt::RightEdge))
        setCursor(Qt::SizeHorCursor);
    else
        setCursor(Qt::SizeVerCursor);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this || Settings::instance().useSystemTitleBar())
        return QMainWindow::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint localPosition = mapFromGlobal(mouseEvent->globalPosition().toPoint());

        if (m_resizing) {
            const QPoint delta = mouseEvent->globalPosition().toPoint() - m_resizeStartGlobal;
            QRect geometry = m_resizeStartGeometry;
            if (m_resizeEdges & Qt::LeftEdge)
                geometry.setLeft(qMin(geometry.left() + delta.x(), geometry.right() - minimumWidth()));
            if (m_resizeEdges & Qt::RightEdge)
                geometry.setRight(qMax(geometry.right() + delta.x(), geometry.left() + minimumWidth()));
            if (m_resizeEdges & Qt::TopEdge)
                geometry.setTop(qMin(geometry.top() + delta.y(), geometry.bottom() - minimumHeight()));
            if (m_resizeEdges & Qt::BottomEdge)
                geometry.setBottom(qMax(geometry.bottom() + delta.y(), geometry.top() + minimumHeight()));
            setGeometry(geometry);
            return true;
        }

        if (!(mouseEvent->buttons() & Qt::LeftButton))
            updateResizeCursor(localPosition);
        break;
    }
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton)
            break;
        const QPoint localPosition = mapFromGlobal(mouseEvent->globalPosition().toPoint());
        const Qt::Edges edges = edgesAt(localPosition);
        if (edges == Qt::Edges())
            break;

        // Сначала пробуем системное изменение размера (нативно в X11/Cinnamon)
        if (windowHandle() && windowHandle()->startSystemResize(edges))
            return true;

        m_resizing = true;
        m_resizeEdges = edges;
        m_resizeStartGlobal = mouseEvent->globalPosition().toPoint();
        m_resizeStartGeometry = geometry();
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (m_resizing) {
            m_resizing = false;
            unsetCursor();
            return true;
        }
        break;
    default:
        break;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange && m_titleBar) {
        m_titleBar->setWindowMaximized(isMaximized());
        m_fullScreenAction->blockSignals(true);
        m_fullScreenAction->setChecked(isFullScreen());
        m_fullScreenAction->blockSignals(false);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::applySettings(const AppearanceSettings &appearance,
                               const BehaviorSettings &behavior, bool persist)
{
    m_appearance = appearance;
    m_behavior = behavior;

    if (persist) {
        appearance.applyToSettings();
        behavior.applyToSettings();
    }

    // --- тема (включая «системную») ---
    const Theme theme = ThemeManager::theme(appearance.themeId);
    ThemeManager::apply(theme);

    if (m_themeMenu) {
        for (QAction *action : m_themeMenu->actions())
            action->setChecked(action->data().toString() == theme.id);
    }

    setWindowOpacity(appearance.windowOpacity / 100.0);

    const bool chromeChanged = m_customTitleBarActive == appearance.useSystemTitleBar;
    if (chromeChanged) {
        m_customTitleBarActive = !appearance.useSystemTitleBar;
        applyWindowChrome();
    }

    // --- терминалы ---
    if (m_tabs) {
        for (TerminalView *terminal : m_tabs->allTerminals()) {
            terminal->applyTheme(theme);
            terminal->setFontFamily(appearance.fontFamily);
            terminal->setFontSize(appearance.fontSize);
            terminal->setLineSpacing(appearance.lineSpacing);
            terminal->setPadding(appearance.padding);
            terminal->setCursorStyle(appearance.cursorStyle);
            terminal->setCursorBlink(appearance.cursorBlink);
            terminal->setWheelScrollLines(appearance.wheelScrollLines);
            terminal->setScrollOnOutput(appearance.scrollOnOutput);
            terminal->setScrollOnKeystroke(appearance.scrollOnKeystroke);
            if (terminal->scrollbackLimit() != appearance.scrollbackLines)
                terminal->setScrollbackLimit(appearance.scrollbackLines);
        }
    }

    // --- видимость панелей ---
    if (m_sidebarAction && m_sidebarAction->isChecked() != appearance.sidebarVisible) {
        m_sidebarAction->blockSignals(true);
        m_sidebarAction->setChecked(appearance.sidebarVisible);
        m_sidebarAction->blockSignals(false);
    }
    if (m_titleBar)
        m_titleBar->setSidebarChecked(appearance.sidebarVisible);
    setSidebarVisible(appearance.sidebarVisible, false);

    if (m_tabs)
        m_tabs->setTabBarVisible(appearance.tabBarVisible);
    if (statusBar())
        statusBar()->setVisible(appearance.statusBarVisible);
    if (m_menuBar)
        m_menuBar->setVisible(appearance.menuBarVisible);
    if (m_menuBarAction && m_menuBarAction->isChecked() != appearance.menuBarVisible) {
        m_menuBarAction->blockSignals(true);
        m_menuBarAction->setChecked(appearance.menuBarVisible);
        m_menuBarAction->blockSignals(false);
    }

    // --- флаги поведения ---
    if (m_restoreSessionAction) {
        m_restoreSessionAction->blockSignals(true);
        m_restoreSessionAction->setChecked(behavior.restoreSession);
        m_restoreSessionAction->blockSignals(false);
    }
    if (m_autoSaveAction) {
        m_autoSaveAction->blockSignals(true);
        m_autoSaveAction->setChecked(behavior.autoSave);
        m_autoSaveAction->blockSignals(false);
    }

    if (persist)
        scheduleSettingsSave();

    updateStatusBar();
    updateSidebar();
}

void MainWindow::applyAppearance(const AppearanceSettings &values, bool persist)
{
    applySettings(values, m_behavior, persist);
}


// ---------------------------------------------------------------------------
// Этап 5: панель разработчика, SSH, проекты
// ---------------------------------------------------------------------------

void MainWindow::setDevPanelVisible(bool visible)
{
    if (!m_devPanel || !m_splitter)
        return;

    // Одновременно открыта только одна правая панель
    if (visible && m_monitorPanel && m_monitorPanel->isVisible())
        setMonitorPanelVisible(false);

    const bool changed = m_devPanel->isVisible() != visible;
    m_devPanel->setVisible(visible);

    if (visible) {
        QList<int> sizes = m_splitter->sizes();
        const int wideEnough = qMax(280, qMin(380, this->width() / 3));
        if (sizes.value(2) < wideEnough) {
            const int total = sizes.value(0) + sizes.value(1) + sizes.value(2) + sizes.value(3);
            sizes[2] = wideEnough;
            sizes[1] = qMax(260, total - sizes.value(0) - sizes.value(3) - wideEnough);
            m_splitter->setSizes(sizes);
        }
        m_devPanel->refresh();
        updateDevContext();
    }

    if (changed) {
        Settings::instance().setDevPanelVisible(visible);
        scheduleSettingsSave();
    }
    if (m_devPanelAction && m_devPanelAction->isChecked() != visible) {
        m_devPanelAction->blockSignals(true);
        m_devPanelAction->setChecked(visible);
        m_devPanelAction->blockSignals(false);
    }
}

bool MainWindow::isDevPanelVisible() const
{
    return m_devPanel && m_devPanel->isVisible();
}

void MainWindow::setMonitorPanelVisible(bool visible)
{
    if (!m_monitorPanel || !m_splitter)
        return;

    // Панель монитора занимает то же место справа, что и панель разработчика
    if (visible && m_devPanel && m_devPanel->isVisible())
        setDevPanelVisible(false);

    const bool changed = m_monitorPanel->isVisible() != visible;
    m_monitorPanel->setVisible(visible);

    if (visible) {
        QList<int> sizes = m_splitter->sizes();
        const int wideEnough = qMax(m_monitorPanel->minimumSizeHint().width(),
                                    qMin(400, qMax(340, this->width() / 3)));
        if (sizes.value(3) < wideEnough) {
            const int total = sizes.value(0) + sizes.value(1) + sizes.value(2) + sizes.value(3);
            sizes[3] = wideEnough;
            sizes[1] = qMax(260, total - sizes.value(0) - sizes.value(2) - wideEnough);
            m_splitter->setSizes(sizes);
        }
        m_monitorPanel->refresh();
    }

    if (changed) {
        Settings::instance().setMonitorPanelVisible(visible);
        scheduleSettingsSave();
    }
    if (m_statusMonitor)
        m_statusMonitor->setVisible(visible);
    if (m_monitorPanelAction && m_monitorPanelAction->isChecked() != visible) {
        m_monitorPanelAction->blockSignals(true);
        m_monitorPanelAction->setChecked(visible);
        m_monitorPanelAction->blockSignals(false);
    }
}

bool MainWindow::isMonitorPanelVisible() const
{
    return m_monitorPanel && m_monitorPanel->isVisible();
}

void MainWindow::updateDevContext()
{
    if (!m_devPanel)
        return;
    const QString directory = currentWorkingDirectory();
    m_devPanel->setWorkingDirectory(directory.isEmpty() ? QDir::homePath() : directory);
}

QString MainWindow::gitStatusLabel() const
{
    const GitStatus git = GitService::status(currentWorkingDirectory());
    return git.valid ? git.shortLabel() : QString();
}

void MainWindow::insertIntoTerminal(const QString &text)
{
    TerminalView *terminal = currentTerminal();
    if (!terminal || text.isEmpty()) {
        statusBar()->showMessage(tr("Нет активного терминала"), 4000);
        return;
    }
    terminal->sendTextToShell(text);
    terminal->setFocus();
    statusBar()->showMessage(tr("Вставлено в приглашение: %1").arg(text), 4000);
}

void MainWindow::executeInTerminal(const QString &command)
{
    TerminalView *terminal = currentTerminal();
    if (!terminal || command.isEmpty()) {
        statusBar()->showMessage(tr("Нет активного терминала"), 4000);
        return;
    }
    terminal->sendTextToShell(command + QLatin1Char('\r'));
    terminal->setFocus();
    Log::info(tr("Команда из панели разработчика: %1").arg(command));
    statusBar()->showMessage(tr("Выполняется: %1").arg(command), 5000);
}

void MainWindow::openDirectoryInNewTab(const QString &directory)
{
    if (directory.isEmpty() || !QFileInfo(directory).isDir())
        return;

    PaneSpec spec;
    spec.workingDirectory = directory;
    spec.title = QFileInfo(directory).fileName();

    if (!m_tabs)
        return;

    PaneTree *tree = m_tabs->addTab(spec, true);
    if (!tree)
        return;

    updatePaneActions();
    updateStatusBar();
    updateSidebar();
    ActivityLog::record(tr("Открыт каталог в новой вкладке"), directory, tr("вкладки"));
    statusBar()->showMessage(tr("Открыт каталог: %1").arg(directory), 4000);
}

void MainWindow::connectSshProfile(const QString &profileId)
{
    const SshProfile profile = SshManager::profile(profileId);
    if (!profile.isValid()) {
        QMessageBox::warning(this, tr("SSH"), tr("Профиль подключения не найден."));
        return;
    }

    QString error;
    const QString command = SshManager::buildCommand(profile, &error);
    if (command.isEmpty()) {
        QMessageBox::warning(this, tr("SSH"), error);
        return;
    }

    // Команду формирует SshManager из проверенных полей; пароль спрашивает сам ssh
    PaneSpec spec;
    spec.title = profile.name;
    spec.command = command;
    spec.runCommand = true;
    if (!profile.name.isEmpty())
        spec.workingDirectory = QDir::homePath();

    const int previousCount = m_tabs->count();
    PaneTree *tree = m_tabs->addTab(spec, true);
    if (!tree) {
        m_tabs->removeTab(previousCount);
        return;
    }
    // Имя подключения важнее заголовка, который пришлёт ssh
    m_tabs->renameTab(m_tabs->count() - 1, profile.name);

    // Запоминаем панель, чтобы сообщить о потере соединения
    if (PaneTree *tree = m_tabs->tabAt(m_tabs->count() - 1)) {
        for (TerminalView *pane : tree->terminals())
            m_sshSessions.insert(pane, profile.name);
    }
    ActivityLog::record(tr("SSH-подключение"), profile.name + QStringLiteral(" (")
                                                   + profile.host + QLatin1Char(')'),
                        tr("SSH"));

    Log::info(tr("SSH-подключение «%1»: %2").arg(profile.name, command));
    updatePaneActions();
    updateStatusBar();
    updateSidebar();
    statusBar()->showMessage(tr("Подключение к %1…").arg(profile.host), 6000);
}

void MainWindow::openProject(const QString &projectId)
{
    const Project project = ProjectManager::project(projectId);
    if (!project.isValid()) {
        QMessageBox::warning(this, tr("Проект"), tr("Проект не найден."));
        return;
    }

    const QString problem = ProjectManager::validate(project);
    if (!problem.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, tr("Проект недоступен"),
            tr("%1\n\nОткрыть терминал в домашнем каталоге?").arg(problem),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return;
        openDirectoryInNewTab(QDir::homePath());
        return;
    }

    // У проекта может быть своя раскладка панелей
    QSharedPointer<LayoutNode> layout = project.layout;
    if (!layout)
        layout = ProjectManager::defaultLayout(project.path);

    for (const PaneSpec &pane : layout->panes()) {
        if (pane.runCommand && !CommandPolicy::isSafe(pane.command)) {
            QMessageBox::warning(
                this, tr("Проект"),
                tr("Команда панели «%1» не разрешена политикой безопасности:\n%2")
                    .arg(pane.title, pane.command));
            return;
        }
    }

    m_tabs->addTabFromLayout(*layout, project.name, true);
    updatePaneActions();
    updateStatusBar();
    updateSidebar();
    statusBar()->showMessage(tr("Открыт проект «%1»").arg(project.name), 5000);
}

void MainWindow::onSshRequested(const QString &profileId)
{
    connectSshProfile(profileId);
}

void MainWindow::onProjectRequested(const QString &projectId)
{
    openProject(projectId);
}

void MainWindow::addCurrentDirectoryAsProject()
{
    const QString directory = currentWorkingDirectory();
    if (directory.isEmpty()) {
        QMessageBox::information(this, tr("Проекты"), tr("Не удалось определить каталог."));
        return;
    }

    const Project existing = ProjectManager::projectForPath(directory);
    if (existing.isValid()) {
        QMessageBox::information(this, tr("Проекты"),
                                 tr("Каталог уже входит в проект «%1».").arg(existing.name));
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("Новый проект"), tr("Название проекта:"), QLineEdit::Normal,
        QFileInfo(directory).fileName(), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;

    Project project;
    project.id = ProjectManager::makeId(name);
    project.name = name.trimmed();
    project.path = directory;
    project.layout = ProjectManager::defaultLayout(directory);
    project.commands = ProjectManager::suggestedCommands(directory);
    ProjectManager::saveProject(project);

    if (m_devPanel) {
        m_devPanel->refresh();
        m_devPanel->selectProject(project.id);
    }
    setDevPanelVisible(true);
    statusBar()->showMessage(tr("Проект «%1» добавлен").arg(project.name), 5000);
}

// ---------------------------------------------------------------------------
// Горячие клавиши
// ---------------------------------------------------------------------------

QAction *MainWindow::actionFor(const QString &shortcutId) const
{
    return m_shortcutActions.value(shortcutId, nullptr);
}

void MainWindow::applyShortcutBindings()
{
    struct Binding
    {
        const char *id;
        QAction *action;
    };

    const QList<Binding> bindings{
        {"app.newWindow", m_newWindowAction},
        {"app.settings", m_settingsAction},
        {"app.quit", m_quitAction},
        {"tab.new", m_newTabAction},
        {"tab.close", m_closeTabAction},
        {"tab.rename", m_renameTabAction},
        {"tab.duplicate", m_duplicateTabAction},
        {"tab.next", m_nextTabAction},
        {"tab.previous", m_previousTabAction},
        {"pane.splitVertical", m_splitVerticalAction},
        {"pane.splitHorizontal", m_splitHorizontalAction},
        {"pane.close", m_closePaneAction},
        {"pane.next", m_focusNextPaneAction},
        {"pane.previous", m_focusPrevPaneAction},
        {"pane.left", m_focusLeftPaneAction},
        {"pane.right", m_focusRightPaneAction},
        {"pane.up", m_focusUpPaneAction},
        {"pane.down", m_focusDownPaneAction},
        {"edit.copy", m_copyAction},
        {"edit.paste", m_pasteAction},
        {"edit.selectAll", m_selectAllAction},
        {"edit.clear", m_clearAction},
        {"view.sidebar", m_sidebarAction},
        {"view.menuBar", m_menuBarAction},
        {"view.devPanel", m_devPanelAction},
        {"view.palette", m_paletteAction},
        {"view.history", m_historyAction},
        {"view.commandLibrary", m_libraryAction},
        {"view.fullScreen", m_fullScreenAction},
        {"view.zoomIn", m_zoomInAction},
        {"view.zoomOut", m_zoomOutAction},
        {"view.zoomReset", m_zoomResetAction},
    };

    m_shortcutActions.clear();
    for (const Binding &binding : bindings) {
        QAction *action = binding.action;
        if (!action)
            continue;
        const QString id = QString::fromLatin1(binding.id);
        m_shortcutActions.insert(id, action);
        const QKeySequence key = ShortcutManager::key(id);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WindowShortcut);
    }

    // Вкладки по номерам: Ctrl+1 … Ctrl+9 (переопределяемые)
    for (int i = 0; i < m_tabIndexActions.size(); ++i) {
        const QString id = QStringLiteral("tab.index%1").arg(i + 1);
        m_shortcutActions.insert(id, m_tabIndexActions.at(i));
        m_tabIndexActions.at(i)->setShortcut(ShortcutManager::key(id));
    }

    // Дополнительные сочетания, которые нельзя выразить одним QAction
    if (m_nextTabAction) {
        const QKeySequence next = ShortcutManager::key(QStringLiteral("tab.next"));
        if (next == QKeySequence(QStringLiteral("Ctrl+Tab")))
            m_nextTabAction->setShortcuts({next, QKeySequence(QStringLiteral("Ctrl+PgDown"))});
        else
            m_nextTabAction->setShortcuts({next});
    }
    if (m_previousTabAction) {
        const QKeySequence previous = ShortcutManager::key(QStringLiteral("tab.previous"));
        if (previous == QKeySequence(QStringLiteral("Ctrl+Shift+Tab")))
            m_previousTabAction->setShortcuts({previous, QKeySequence(QStringLiteral("Ctrl+PgUp"))});
        else
            m_previousTabAction->setShortcuts({previous});
    }
    if (m_zoomInAction) {
        const QKeySequence zoomIn = ShortcutManager::key(QStringLiteral("view.zoomIn"));
        if (zoomIn == QKeySequence(QStringLiteral("Ctrl++")))
            m_zoomInAction->setShortcuts({zoomIn, QKeySequence(QStringLiteral("Ctrl+="))});
        else
            m_zoomInAction->setShortcuts({zoomIn});
    }
}

void MainWindow::applyShortcuts()
{
    applyShortcutBindings();
}

void MainWindow::scheduleSettingsSave()
{
    if (!m_saveTimer) {
        Settings::instance().save();
        return;
    }
    if (!Settings::instance().autoSave()) {
        // Автосохранение выключено — настройки запишутся при выходе
        return;
    }
    m_saveTimer->start();
}

// ---------------------------------------------------------------------------
// Вкладки и сессии
// ---------------------------------------------------------------------------

void MainWindow::setInitialCommand(const QString &command)
{
    m_initialCommand = command;
}

void MainWindow::startSession()
{
    if (!m_tabs)
        return;

    if (m_tabs->count() == 0) {
        if (!restoreSession()) {
            PaneTree *tree = m_tabs->addTab(PaneSpec(), false);
            TerminalView *terminal = tree ? tree->activeTerminal() : nullptr;
            if (terminal) {
                wireTerminal(terminal);
                if (!m_initialCommand.isEmpty()) {
                    terminal->setInitialCommand(m_initialCommand);
                    m_initialCommand.clear();
                }
                if (!terminal->startSession()) {
                    QMessageBox::critical(
                        this, tr("Не удалось запустить терминал"),
                        tr("BlackCore не смог запустить оболочку.\n\n"
                           "Проверьте настройки оболочки и журнал: %1")
                            .arg(Log::filePath()),
                        QMessageBox::Close);
                }
                terminal->setFocus();
            }
        }
    }

    updatePaneActions();
    updateStatusBar();
    updateSidebar();
}

void MainWindow::newTab(const QString &command)
{
    if (!m_tabs)
        return;

    PaneSpec spec;
    if (Settings::instance().newTabInCurrentDirectory()) {
        if (TerminalView *current = currentTerminal())
            spec.workingDirectory = current->workingDirectory();
    }

    const int previousCount = m_tabs->count();
    PaneTree *tree = m_tabs->addTab(spec, false);
    TerminalView *terminal = tree ? tree->activeTerminal() : nullptr;
    if (!terminal)
        return;

    wireTerminal(terminal);
    if (!command.isEmpty())
        terminal->setInitialCommand(command);

    QString error;
    if (!terminal->startSession(&error)) {
        QMessageBox::warning(this, tr("Не удалось открыть вкладку"), error);
        m_tabs->removeTab(previousCount);
        return;
    }

    terminal->setFocus();
    onTabChanged(m_tabs->currentIndex());
}

bool MainWindow::startTerminal(TerminalView *terminal)
{
    if (!terminal || terminal->isSessionRunning())
        return true;

    QString error;
    if (terminal->startSession(&error)) {
        terminal->setFocus();
        return true;
    }

    Log::error(tr("Не удалось запустить панель: %1").arg(error));
    statusBar()->showMessage(tr("Не удалось запустить оболочку в панели: %1").arg(error), 8000);
    return false;
}

void MainWindow::onTerminalCreated(TerminalView *terminal, bool startNow)
{
    if (!terminal)
        return;

    wireTerminal(terminal);
    if (startNow)
        startTerminal(terminal);

    updatePaneActions();
    updateStatusBar();
    updateSidebar();
}

// ---------------------------------------------------------------------------
// Панели: разделение, закрытие, фокус
// ---------------------------------------------------------------------------

void MainWindow::splitCurrentPane(Qt::Orientation orientation)
{
    PaneTree *tree = m_tabs ? m_tabs->currentTab() : nullptr;
    if (!tree)
        return;

    TerminalView *target = tree->activeTerminal();
    PaneSpec spec;
    spec.title = tr("Панель %1").arg(tree->paneCount() + 1);
    if (target)
        spec.workingDirectory = target->workingDirectory();

    tree->split(target, orientation, spec, true);
    ActivityLog::record(orientation == Qt::Horizontal ? tr("Панель разделена вертикально")
                                                      : tr("Панель разделена горизонтально"),
                        currentWorkingDirectory(), tr("панели"));
    updatePaneActions();
    updateStatusBar();
    updateSidebar();
}

void MainWindow::closeCurrentPane()
{
    PaneTree *tree = m_tabs ? m_tabs->currentTab() : nullptr;
    if (!tree)
        return;

    TerminalView *terminal = tree->activeTerminal();
    if (!terminal)
        return;

    if (tree->paneCount() <= 1) {
        onTabCloseRequested(m_tabs->currentIndex());
        return;
    }

    if (!confirmClose({terminal}, tr("Закрыть панель?")))
        return;

    tree->closePane(terminal);
    updatePaneActions();
    updateStatusBar();
    updateSidebar();
}

void MainWindow::onPaneCloseRequested(TerminalView *terminal)
{
    if (!terminal)
        return;

    PaneTree *tree = nullptr;
    if (m_tabs) {
        for (PaneTree *candidate : m_tabs->trees()) {
            if (candidate->terminals().contains(terminal)) {
                tree = candidate;
                break;
            }
        }
    }
    if (!tree)
        return;

    if (tree->paneCount() <= 1) {
        onTabCloseRequested(m_tabs->indexOf(tree));
        return;
    }

    if (!confirmClose({terminal}, tr("Закрыть панель?")))
        return;

    tree->closePane(terminal);
    updatePaneActions();
    updateStatusBar();
    updateSidebar();
}

void MainWindow::onActivePaneChanged(TerminalView *terminal)
{
    if (!terminal)
        return;
    m_copyAction->setEnabled(terminal->hasSelection());
    updateStatusBar();
    updateSidebar();
    if (isDevPanelVisible())
        updateDevContext();
}

bool MainWindow::hasRunningChild(pid_t pid)
{
    return mainWindowHasChildren(pid);
}

bool MainWindow::confirmClose(const QList<TerminalView *> &terminals, const QString &actionText)
{
    // Пользователь может отключить предупреждения в настройках поведения
    if (!Settings::instance().warnOnCloseRunning())
        return true;

    for (TerminalView *terminal : terminals) {
        if (!terminal || !terminal->isSessionRunning())
            continue;

        // Предупреждаем только если в панели действительно запущена программа
        if (!hasRunningChild(terminal->shellPid()))
            continue;

        const auto answer = QMessageBox::question(
            this, actionText,
            tr("В панели выполняется процесс (pid %1).\n"
               "Завершить его и продолжить?")
                .arg(terminal->shellPid()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
    }
    return true;
}

void MainWindow::updatePaneActions()
{
    PaneTree *tree = m_tabs ? m_tabs->currentTab() : nullptr;
    const bool split = tree && tree->paneCount() > 1;

    if (m_closePaneAction)
        m_closePaneAction->setEnabled(tree != nullptr);
    for (QAction *action : {m_focusNextPaneAction, m_focusPrevPaneAction, m_focusLeftPaneAction,
                            m_focusRightPaneAction, m_focusUpPaneAction, m_focusDownPaneAction}) {
        if (action)
            action->setEnabled(split);
    }
}

void MainWindow::switchTabBy(int delta)
{
    if (!m_tabs || m_tabs->count() == 0)
        return;

    const int count = m_tabs->count();
    int index = (m_tabs->currentIndex() + delta) % count;
    if (index < 0)
        index += count;
    m_tabs->setCurrentIndex(index);
}

TerminalView *MainWindow::currentTerminal() const
{
    return m_tabs ? m_tabs->currentTerminal() : nullptr;
}

void MainWindow::wireTerminal(TerminalView *terminal)
{
    if (!terminal)
        return;

    // Идентификатор панели для отслеживания задач (0 — служебный)
    if (!m_terminalIds.contains(terminal))
        m_terminalIds.insert(terminal, m_nextTerminalId++);

    const int terminalId = m_terminalIds.value(terminal);
    connect(terminal, &TerminalView::outputReceived, this, [this, terminalId]() {
        if (m_tasks)
            m_tasks->noteOutput(terminalId);
    });

    // Подключаем сигналы один раз при создании вкладки — без дублей при переключении
    connect(terminal, &TerminalView::copyAvailable, m_copyAction, &QAction::setEnabled);
    connect(terminal, &TerminalView::sessionFinished, this, &MainWindow::onSessionFinished);
    connect(terminal, &TerminalView::fontSizeChanged, this, [this](int) {
        updateStatusBar();
    });
    connect(terminal, &TerminalView::titleChanged, this, [this](const QString &) {
        updateWindowTitle();
        updateSidebar();
    });
}

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index);
    TerminalView *terminal = currentTerminal();
    if (terminal) {
        m_copyAction->setEnabled(terminal->hasSelection());
        terminal->setFocus();
    }

    if (m_sideBar)
        m_sideBar->setCurrentIndex(index);
    updateWindowTitle();
    updateStatusBar();
    updateSidebar();
    if (isDevPanelVisible())
        updateDevContext();
}

void MainWindow::onTabCountChanged(int count)
{
    if (m_sideBar) {
        m_sideBar->setSessions(m_tabs->sessionTitles(), m_tabs->currentIndex());
        m_sideBar->setCounters(count);
    }
    updateSidebar();
}

void MainWindow::onTabCloseRequested(int index)
{
    if (!m_tabs || index < 0)
        return;

    PaneTree *tree = m_tabs->tabAt(index);
    if (m_tabs->count() <= 1) {
        // Единственную вкладку закрываем вместе с окном — так ожидает пользователь
        close();
        return;
    }

    const QList<TerminalView *> terminals = tree ? tree->terminals() : QList<TerminalView *>();
    if (!confirmClose(terminals, tr("Закрыть вкладку?")))
        return;

    m_tabs->removeTab(index);
    updatePaneActions();
    updateStatusBar();
    updateSidebar();
}

void MainWindow::onTabRenameRequested(int index)
{
    if (!m_tabs || index < 0 || index >= m_tabs->count())
        return;

    bool accepted = false;
    const QString current = m_tabs->customTitle(index);
    const QString title = QInputDialog::getText(
        this, tr("Переименовать вкладку"),
        tr("Название вкладки (пустое — автоматическое):"),
        QLineEdit::Normal, current, &accepted);

    if (!accepted)
        return;

    m_tabs->renameTab(index, title);
    updateSidebar();
    updateWindowTitle();
}

// ---------------------------------------------------------------------------
// Рабочие пространства
// ---------------------------------------------------------------------------

void MainWindow::rebuildWorkspaceMenu()
{
    if (!m_workspaceMenu)
        return;

    m_workspaceMenu->clear();

    const QList<Workspace> workspaces = WorkspaceManager::all();
    for (const Workspace &workspace : workspaces) {
        QAction *action = m_workspaceMenu->addAction(workspace.name);
        action->setData(workspace.id);
        const QString hint = tr("%1 — панелей: %2")
                                 .arg(workspace.description.isEmpty() ? workspace.name
                                                                      : workspace.description)
                                 .arg(workspace.paneCount());
        action->setToolTip(hint);
        action->setStatusTip(hint);
        connect(action, &QAction::triggered, this, [this, id = workspace.id]() {
            openWorkspace(id);
        });
    }

    m_workspaceMenu->addSeparator();
    m_workspaceMenu->addAction(m_saveWorkspaceAction);
}

void MainWindow::openWorkspace(const QString &workspaceId)
{
    if (!m_tabs)
        return;

    const Workspace workspace = WorkspaceManager::workspace(workspaceId);
    if (!workspace.layout) {
        QMessageBox::warning(this, tr("Рабочее пространство"),
                             tr("Рабочее пространство «%1» не найдено.").arg(workspaceId));
        return;
    }

    // Команды шаблона — только те, что прошли политику безопасности
    QSharedPointer<LayoutNode> layout = workspace.layout;
    for (const PaneSpec &pane : layout->panes()) {
        if (pane.runCommand && !CommandPolicy::isSafe(pane.command)) {
            QMessageBox::warning(this, tr("Рабочее пространство"),
                                 tr("Команда «%1» не разрешена политикой безопасности.")
                                     .arg(pane.command));
            return;
        }
    }

    m_tabs->addTabFromLayout(*layout, workspace.name, true);
    updatePaneActions();
    updateStatusBar();
    updateSidebar();
    statusBar()->showMessage(tr("Открыто рабочее пространство «%1»").arg(workspace.name), 5000);
}

void MainWindow::saveCurrentWorkspace()
{
    PaneTree *tree = m_tabs ? m_tabs->currentTab() : nullptr;
    if (!tree)
        return;

    bool accepted = false;
    QString name = m_tabs->customTitle(m_tabs->currentIndex());
    if (name.isEmpty())
        name = m_tabs->tabTitle(m_tabs->currentIndex());
    name = QInputDialog::getText(this, tr("Сохранить рабочее пространство"),
                                 tr("Название:"), QLineEdit::Normal, name, &accepted)
               .trimmed();
    if (!accepted || name.isEmpty())
        return;

    const Workspace workspace = WorkspaceManager::fromCurrent(
        name, tr("Пользовательское рабочее пространство"), tree->captureLayout());
    WorkspaceManager::saveUser(workspace);
    rebuildWorkspaceMenu();
    statusBar()->showMessage(tr("Рабочее пространство «%1» сохранено").arg(name), 5000);
}

// ---------------------------------------------------------------------------
// Сохранение и восстановление сессии
// ---------------------------------------------------------------------------

void MainWindow::saveSession()
{
    if (!m_tabs)
        return;

    SessionState state;
    state.currentTab = m_tabs->currentIndex();

    for (int i = 0; i < m_tabs->count(); ++i) {
        PaneTree *tree = m_tabs->tabAt(i);
        if (!tree)
            continue;

        TabState tab;
        tab.title = m_tabs->tabTitle(i);
        tab.customTitle = m_tabs->customTitle(i);
        tab.layout = tree->captureLayout();
        tab.activePane = m_tabs->activePaneIndex(i);
        if (tab.layout)
            state.tabs.append(tab);
    }

    if (state.tabs.isEmpty())
        SessionStore::clear();
    else
        SessionStore::save(state);
}

bool MainWindow::restoreSession()
{
    if (!SessionStore::restoreEnabled())
        return false;

    const SessionState state = SessionStore::sanitize(SessionStore::load());
    if (!state.isValid())
        return false;

    for (const TabState &tab : state.tabs) {
        if (!tab.layout)
            continue;
        const QString title = tab.customTitle.isEmpty() ? tab.title : tab.customTitle;
        PaneTree *tree = m_tabs->addTabFromLayout(*tab.layout, title, true);
        if (tree && tab.activePane > 0)
            tree->focusPane(tab.activePane);
    }

    if (m_tabs->count() == 0)
        return false;

    m_tabs->setCurrentIndex(qBound(0, state.currentTab, m_tabs->count() - 1));
    statusBar()->showMessage(tr("Сессия восстановлена: вкладок %1").arg(m_tabs->count()), 6000);
    Log::info(tr("Сессия восстановлена: вкладок %1").arg(m_tabs->count()));
    return true;
}

void MainWindow::onSessionFinished(int exitCode, int exitSignal)
{
    if (auto *terminal = qobject_cast<TerminalView *>(sender())) {
        if (m_sshSessions.contains(terminal)) {
            const QString name = m_sshSessions.take(terminal);
            NotificationCenter::instance().notify(
                NotificationKind::SshDisconnected, tr("Потеря SSH-соединения"),
                tr("Подключение «%1» завершено (код %2)%3")
                    .arg(name)
                    .arg(exitCode)
                    .arg(exitSignal != 0 ? tr(", сигнал %1").arg(exitSignal) : QString()));
            ActivityLog::record(tr("SSH-соединение закрыто"), name, tr("SSH"));
        }
        // Незакрытые задачи этой панели больше не выполняются
        if (m_tasks) {
            const int terminalId = m_terminalIds.value(terminal, 0);
            m_tasks->complete(0, exitCode);
            Q_UNUSED(terminalId);
        }
    }
    updateStatusBar();
    updateSidebar();
    statusBar()->showMessage(tr("Процесс оболочки завершён"), 8000);
}

void MainWindow::newWindow()
{
    auto *window = new MainWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(size());
    window->show();
    window->startSession();
}

// ---------------------------------------------------------------------------
// Боковая панель и статус-строка
// ---------------------------------------------------------------------------

void MainWindow::setSidebarVisible(bool visible, bool animate)
{
    if (!m_sideBar || !m_splitter)
        return;

    Settings::instance().setSidebarVisible(visible);
    scheduleSettingsSave();

    m_sidebarAction->blockSignals(true);
    m_sidebarAction->setChecked(visible);
    m_sidebarAction->blockSignals(false);
    m_titleBar->setSidebarChecked(visible);

    if (!animate || m_sideBar->isVisible() == visible) {
        m_sideBar->setVisible(visible);
        return;
    }

    m_sideBar->setVisible(visible);
    if (visible) {
        QList<int> sizes = m_splitter->sizes();
        if (sizes.value(0) < 120) {
            sizes[0] = 230;
            sizes[1] = qMax(320, width() - 230);
            m_splitter->setSizes(sizes);
        }
    }
}

QString MainWindow::currentWorkingDirectory() const
{
    if (TerminalView *terminal = currentTerminal())
        return readProcessWorkingDirectory(terminal->shellPid());
    return QString();
}

void MainWindow::updateSidebar()
{
    if (!m_sideBar || !m_tabs)
        return;

    m_sideBar->setSessions(m_tabs->sessionTitles(), m_tabs->currentIndex());

    TerminalView *terminal = currentTerminal();
    if (!terminal)
        return;

    m_sideBar->setSessionInfo(terminal->shellTitle(),
                              currentWorkingDirectory(),
                              QStringLiteral("%1 × %2").arg(terminal->cols()).arg(terminal->rows()),
                              static_cast<int>(terminal->shellPid()),
                              terminal->isSessionRunning());
}

void MainWindow::updateStatusBar()
{
    if (!m_statusInfo || !m_statusSession || !m_tabs)
        return;

    TerminalView *terminal = currentTerminal();
    const Theme theme = ThemeManager::current();

    if (!terminal) {
        m_statusInfo->setText(tr("Вкладок нет — Ctrl+Shift+T, чтобы открыть"));
        m_statusSession->clear();
        return;
    }

    const QString state = terminal->isSessionRunning()
        ? tr("● %1").arg(terminal->shellTitle())
        : tr("○ оболочка завершена");
    m_statusInfo->setText(QStringLiteral("%1   •   %2   •   %3 × %4")
                              .arg(state, currentWorkingDirectory())
                              .arg(terminal->cols())
                              .arg(terminal->rows()));
    const QString workingDirectory = currentWorkingDirectory();

    // Панель разработчика следит за каталогом активной панели
    if (m_devPanel && m_devPanel->isVisible() && !workingDirectory.isEmpty())
        m_devPanel->setWorkingDirectory(workingDirectory);

    if (m_statusGit) {
        const GitStatus git = GitService::status(workingDirectory);
        if (git.valid) {
            m_statusGit->setText(git.shortLabel());
            m_statusGit->setToolTip(tr("Репозиторий: %1\nИзменений: %2")
                                        .arg(git.repositoryRoot)
                                        .arg(git.changedFiles()));
        } else {
            m_statusGit->clear();
            m_statusGit->setToolTip(QString());
        }
    }

    m_statusSession->setText(QStringLiteral("%1 pt   •   %2   •   вкладок: %3")
                                 .arg(terminal->fontSize())
                                 .arg(theme.name)
                                 .arg(m_tabs->count()));
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(fullName());
    if (m_titleBar) {
        m_titleBar->setTitle(QStringLiteral("BlackCore"));
        m_titleBar->setSubtitle(versionString());
    }
}

// ---------------------------------------------------------------------------
// Настройки, справка, завершение
// ---------------------------------------------------------------------------

void MainWindow::openSettings()
{
    const AppearanceSettings originalAppearance = AppearanceSettings::fromSettings();
    const BehaviorSettings originalBehavior = BehaviorSettings::fromSettings();

    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::previewChanged, this,
            [this](const AppearanceSettings &values) { applyAppearance(values, false); });
    connect(&dialog, &SettingsDialog::settingsApplied, this, [this, &dialog]() {
        applySettings(dialog.appearance(), dialog.behavior(), true);
        applyShortcuts();
        Settings::instance().save();
    });

    if (dialog.exec() == QDialog::Accepted) {
        applySettings(dialog.appearance(), dialog.behavior(), true);
        applyShortcuts();
        Settings::instance().save();
    } else {
        // Отмена — возвращаем прежний вид и поведение
        applySettings(originalAppearance, originalBehavior, true);
        Settings::instance().save();
    }
    updateStatusBar();
    updateSidebar();
}

void MainWindow::openConfigFile()
{
    const QString path = ConfigIO::configFilePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(this, tr("Файл настроек"),
                                 tr("Файл настроек ещё не создан:\n%1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::exportConfig()
{
    const QString suggested = QDir::homePath()
        + QStringLiteral("/blackcore-config-%1.json")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Экспорт настроек"), suggested,
        tr("Настройки BlackCore (*.json);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!ConfigIO::exportToFile(path, &error)) {
        QMessageBox::warning(this, tr("Экспорт настроек"),
                             tr("Не удалось сохранить файл.\n\n%1").arg(error));
        return;
    }
    statusBar()->showMessage(tr("Настройки экспортированы: %1").arg(path), 6000);
}

void MainWindow::importConfig()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Импорт настроек"), QDir::homePath(),
        tr("Настройки BlackCore (*.json);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    QStringList warnings;
    QString error;
    if (!ConfigIO::importFromFile(path, &error, &warnings)) {
        QMessageBox::warning(this, tr("Импорт настроек"),
                             tr("Файл не применён.\n\n%1").arg(error));
        return;
    }

    applySettings(AppearanceSettings::fromSettings(), BehaviorSettings::fromSettings(), false);
    applyShortcuts();
    updateStatusBar();
    updateSidebar();

    QString text = tr("Настройки импортированы из файла:\n%1").arg(path);
    if (!warnings.isEmpty())
        text += QStringLiteral("\n\n") + warnings.join(QLatin1Char('\n'));
    QMessageBox::information(this, tr("Импорт настроек"), text);
}

void MainWindow::resetAllSettings()
{
    const auto answer = QMessageBox::question(
        this, tr("Сброс настроек"),
        tr("Вернуть все настройки BlackCore к значениям по умолчанию?\n\n"
           "Перед сбросом будет создана резервная копия.\n"
           "Файлы .bashrc/.zshrc и системная оболочка не изменяются."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString backupPath;
    QString error;
    if (!ConfigIO::resetToDefaults(&backupPath, &error)) {
        QMessageBox::warning(this, tr("Сброс настроек"), error);
        return;
    }

    applySettings(AppearanceSettings::fromSettings(), BehaviorSettings::fromSettings(), false);
    applyShortcuts();
    updateStatusBar();
    updateSidebar();
    statusBar()->showMessage(tr("Настройки сброшены. Резервная копия: %1").arg(backupPath), 8000);
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, fullName(),
        tr("<h3>%1</h3>"
           "<p>Современный терминал для Linux Mint с удобным управлением, "
           "гибкой настройкой внешнего вида и инструментами для повседневной работы.</p>"
           "<p><b>SaniT &amp; Mix Of Me</b></p>").arg(fullName().toHtmlEscaped()));
}

void MainWindow::restoreGeometryFromSettings()
{
    QSettings store;
    const QByteArray geometry = store.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty() && restoreGeometry(geometry))
        return;
    resize(1040, 640);
}

void MainWindow::saveGeometryToSettings()
{
    QSettings store;
    store.setValue(QStringLiteral("window/geometry"), saveGeometry());
    store.sync();
}

void MainWindow::rememberWorkingDirectory()
{
    const QString directory = currentWorkingDirectory();
    if (!directory.isEmpty())
        Settings::instance().setLastWorkingDirectory(directory);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_toast)
        m_toast->reposition();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const bool askAboutProcesses = Settings::instance().confirmCloseWindow();
    if (m_tabs && askAboutProcesses
        && !confirmClose(m_tabs->allTerminals(), tr("Закрыть терминал?"))) {
        event->ignore();
        return;
    }

    // Сессия сохраняется до остановки процессов: только имена, каталоги и раскладка
    if (SessionStore::restoreEnabled())
        saveSession();

    rememberWorkingDirectory();
    Settings::instance().save();
    saveGeometryToSettings();
    event->accept();
}


// ---------------------------------------------------------------------------
// Этап 7: командная палитра, библиотека команд, история, уведомления
// ---------------------------------------------------------------------------

void MainWindow::setupStage7()
{
    // Уведомления: центр решает, что показывать; окно показывает плашку и/или
    // системное уведомление. Полное отключение — одна галочка в настройках.
    m_toast = new NotificationToast(this);
    m_toast->setVisible(false);
    connect(m_toast, &NotificationToast::showHistoryRequested, this, [this]() { openHistory(4); });
    connect(m_toast, &NotificationToast::dismissed, this, [this](const Notification &) {
        m_toast->reposition();
    });

    connect(&NotificationCenter::instance(), &NotificationCenter::notified, this,
            [this](const Notification &notification) {
                m_lastNotification = notification;
                if (m_toast)
                    m_toast->push(notification);

                const NotificationSettings settings = NotificationCenter::instance().settings();
                if (settings.desktop && QSystemTrayIcon::isSystemTrayAvailable()) {
                    if (!m_trayIcon) {
                        m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/blackcore-128.png")), this);
                        m_trayIcon->setToolTip(QStringLiteral("BlackCore"));
                        m_trayIcon->show();
                    }
                    m_trayIcon->showMessage(notification.title, notification.body,
                                            QSystemTrayIcon::Information, 8000);
                }
                updateStatusBar();
            },
            Qt::QueuedConnection);

    // Ошибки приложения: qCritical в любом месте программы → уведомление
    Log::setErrorNotifier([](const QString &text) {
        NotificationCenter::instance().notify(NotificationKind::Error,
                                              QObject::tr("Ошибка приложения"), text);
    });

    // Отслеживание выполнения команд, запущенных из BlackCore
    m_tasks = new TaskTracker(this);
    connect(m_tasks, &TaskTracker::finished, this, &MainWindow::onTaskFinished);
    // Пока у оболочки есть дочерние процессы, команда ещё выполняется:
    // без этой проверки долгая команда без вывода считалась бы завершённой
    m_tasks->setBusyCheck([this](int terminalId) {
        for (auto it = m_terminalIds.constBegin(); it != m_terminalIds.constEnd(); ++it) {
            if (it.value() != terminalId)
                continue;
            const qint64 pid = it.key() ? it.key()->shellProcessId() : -1;
            if (pid <= 0)
                return false;
            return mainWindowHasChildren(static_cast<pid_t>(pid));
        }
        return false;
    });

    // Палитра команд
    m_palette = new CommandPalette(this);
    connect(m_palette, &CommandPalette::triggerRequested, this, [this](const QString &id) {
        if (triggerPaletteItem(id))
            ActivityLog::record(tr("Палитра команд"), id, tr("палитра"));
    });

    // Библиотека команд
    m_libraryDialog = new CommandLibraryDialog(this);
    connect(m_libraryDialog, &CommandLibraryDialog::runRequested, this,
            [this](const SavedCommand &command) {
                runSavedCommand(command.id, false);
            });
}

void MainWindow::openCommandPalette()
{
    if (!m_palette)
        return;
    m_palette->setItems(paletteItems());
    m_palette->setQuery(QString());
    m_palette->open_(this);
}

QList<PaletteItem> MainWindow::paletteItems() const
{
    QList<PaletteItem> items;

    auto add = [&items](const QString &id, const QString &title, const QString &subtitle,
                        const QString &group, const QString &keywords = QString()) {
        PaletteItem item;
        item.id = id;
        item.title = title;
        item.subtitle = subtitle;
        item.group = group;
        item.keywords = keywords;
        items.append(item);
    };

    const auto shortcutOf = [](const QString &id) {
        const QKeySequence key = ShortcutManager::key(id);
        return key.isEmpty() ? QString() : key.toString(QKeySequence::NativeText);
    };

    // --- действия ---
    add(QStringLiteral("tab.new"), tr("Новая вкладка"), shortcutOf(QStringLiteral("tab.new")),
        tr("Вкладки"), QStringLiteral("новая терминал tab"));
    add(QStringLiteral("tab.close"), tr("Закрыть вкладку"), shortcutOf(QStringLiteral("tab.close")),
        tr("Вкладки"));
    add(QStringLiteral("tab.duplicate"), tr("Дублировать вкладку"),
        shortcutOf(QStringLiteral("tab.duplicate")), tr("Вкладки"));
    add(QStringLiteral("tab.rename"), tr("Переименовать вкладку"),
        shortcutOf(QStringLiteral("tab.rename")), tr("Вкладки"));
    add(QStringLiteral("pane.splitVertical"), tr("Разделить панель вертикально"),
        shortcutOf(QStringLiteral("pane.splitVertical")), tr("Панели"));
    add(QStringLiteral("pane.splitHorizontal"), tr("Разделить панель горизонтально"),
        shortcutOf(QStringLiteral("pane.splitHorizontal")), tr("Панели"));
    add(QStringLiteral("pane.close"), tr("Закрыть панель"), shortcutOf(QStringLiteral("pane.close")),
        tr("Панели"));
    add(QStringLiteral("pane.next"), tr("Следующая панель"), shortcutOf(QStringLiteral("pane.next")),
        tr("Панели"));
    add(QStringLiteral("app.settings"), tr("Настройки"), shortcutOf(QStringLiteral("app.settings")),
        tr("Приложение"), QStringLiteral("настройки preferences"));
    add(QStringLiteral("app.quit"), tr("Выход"), shortcutOf(QStringLiteral("app.quit")),
        tr("Приложение"));
    add(QStringLiteral("view.sidebar"), tr("Боковая панель"), shortcutOf(QStringLiteral("view.sidebar")),
        tr("Вид"));
    add(QStringLiteral("view.menuBar"), tr("Главное меню"), shortcutOf(QStringLiteral("view.menuBar")),
        tr("Вид"));
    add(QStringLiteral("view.fullScreen"), tr("Полноэкранный режим"),
        shortcutOf(QStringLiteral("view.fullScreen")), tr("Вид"));
    add(QStringLiteral("edit.clear"), tr("Очистить экран"), shortcutOf(QStringLiteral("edit.clear")),
        tr("Терминал"));
    add(QStringLiteral("edit.copy"), tr("Копировать"), shortcutOf(QStringLiteral("edit.copy")),
        tr("Терминал"));
    add(QStringLiteral("edit.paste"), tr("Вставить"), shortcutOf(QStringLiteral("edit.paste")),
        tr("Терминал"));
    add(QStringLiteral("workspace.save"), tr("Сохранить рабочее пространство"), QString(),
        tr("Рабочие пространства"));

    // --- мониторинг ---
    add(QStringLiteral("monitor.toggle"), tr("Монитор: открыть панель"),
        shortcutOf(QStringLiteral("view.monitorPanel")), tr("Монитор"),
        QStringLiteral("мониторинг система cpu память"));
    add(QStringLiteral("monitor.overview"), tr("Монитор: обзор системы"), QString(), tr("Монитор"));
    add(QStringLiteral("monitor.processes"), tr("Монитор: ресурсоёмкие процессы"), QString(),
        tr("Монитор"), QStringLiteral("процессы процессы top"));
    add(QStringLiteral("monitor.diagnostics"), tr("Монитор: диагностика и журнал"), QString(),
        tr("Монитор"), QStringLiteral("диагностика проверки журнал логи"));
    add(QStringLiteral("devPanel.toggle"), tr("Панель разработчика"),
        shortcutOf(QStringLiteral("view.devPanel")), tr("Разработчик"),
        QStringLiteral("git ssh проекты"));
    add(QStringLiteral("devPanel.ssh"), tr("SSH-подключение…"), QString(), tr("Разработчик"),
        QStringLiteral("ssh сервер подключиться профиль"));
    add(QStringLiteral("devPanel.projects"), tr("Открыть проект…"), QString(), tr("Разработчик"),
        QStringLiteral("проект каталог открыть"));

    // --- инструменты этапа 7 ---
    add(QStringLiteral("history.open"), tr("История и поиск"),
        shortcutOf(QStringLiteral("view.history")), tr("Инструменты"),
        QStringLiteral("история поиск журнал"));
    add(QStringLiteral("library.open"), tr("Библиотека команд"),
        shortcutOf(QStringLiteral("view.commandLibrary")), tr("Инструменты"),
        QStringLiteral("команды сценарии"));
    add(QStringLiteral("notifications.history"), tr("История уведомлений"), QString(),
        tr("Инструменты"));

    // --- рабочие пространства ---
    for (const Workspace &workspace : WorkspaceManager::all()) {
        add(QStringLiteral("workspace:") + workspace.id, workspace.name,
            workspace.description.isEmpty()
                ? tr("панелей: %1").arg(workspace.paneCount())
                : workspace.description,
            tr("Рабочие пространства"), QStringLiteral("рабочее пространство workspace"));
    }

    // --- темы ---
    for (const Theme &theme : ThemeManager::themes()) {
        add(QStringLiteral("theme:") + theme.id, theme.name, theme.description, tr("Темы"),
            QStringLiteral("тема оформление цвет"));
    }

    // --- SSH-профили ---
    for (const SshProfile &profile : SshManager::profiles()) {
        add(QStringLiteral("ssh:") + profile.id, tr("SSH: %1").arg(profile.name),
            QStringLiteral("%1@%2:%3").arg(profile.user.isEmpty() ? QStringLiteral("—") : profile.user,
                                           profile.host)
                .arg(profile.port),
            tr("SSH"), QStringLiteral("ssh подключение сервер"));
    }

    // --- проекты ---
    for (const Project &project : ProjectManager::projects()) {
        add(QStringLiteral("project:") + project.id, tr("Проект: %1").arg(project.name),
            project.path, tr("Проекты"), QStringLiteral("проект project каталог"));
    }

    // --- сохранённые команды ---
    for (const SavedCommand &command : CommandLibrary::commands()) {
        add(QStringLiteral("cmd:") + command.id, command.name,
            QStringLiteral("%1 · %2")
                .arg(command.categoryName(), command.command.simplified().left(60)),
            tr("Команды"), command.description + QLatin1Char(' ') + command.command);
    }

    return items;
}

bool MainWindow::triggerPaletteItem(const QString &id)
{
    if (id.isEmpty())
        return false;

    if (id.startsWith(QStringLiteral("workspace:"))) {
        openWorkspace(id.mid(QStringLiteral("workspace:").size()));
        return true;
    }
    if (id.startsWith(QStringLiteral("theme:"))) {
        const QString themeId = id.mid(QStringLiteral("theme:").size());
        AppearanceSettings values = AppearanceSettings::fromSettings();
        values.themeId = ThemeManager::theme(themeId).id;
        applyAppearance(values, true);
        ActivityLog::record(tr("Применена тема"), ThemeManager::current().name, tr("настройки"));
        statusBar()->showMessage(tr("Тема: %1").arg(ThemeManager::current().name), 4000);
        return true;
    }
    if (id.startsWith(QStringLiteral("ssh:"))) {
        connectSshProfile(id.mid(QStringLiteral("ssh:").size()));
        return true;
    }
    if (id.startsWith(QStringLiteral("project:"))) {
        openProject(id.mid(QStringLiteral("project:").size()));
        return true;
    }
    if (id.startsWith(QStringLiteral("cmd:"))) {
        runSavedCommand(id.mid(QStringLiteral("cmd:").size()), false);
        return true;
    }

    // --- простые действия ---
    if (id == QLatin1String("tab.new")) { newTab(); return true; }
    if (id == QLatin1String("tab.close")) {
        if (m_closeTabAction)
            m_closeTabAction->trigger();
        return true;
    }
    if (id == QLatin1String("tab.duplicate")) {
        if (m_tabs->duplicateTab(m_tabs->currentIndex()))
            onTabChanged(m_tabs->currentIndex());
        return true;
    }
    if (id == QLatin1String("tab.rename")) { onTabRenameRequested(m_tabs->currentIndex()); return true; }
    if (id == QLatin1String("pane.splitVertical")) { splitCurrentPane(Qt::Horizontal); return true; }
    if (id == QLatin1String("pane.splitHorizontal")) { splitCurrentPane(Qt::Vertical); return true; }
    if (id == QLatin1String("pane.close")) { closeCurrentPane(); return true; }
    if (id == QLatin1String("pane.next")) {
        if (m_focusNextPaneAction)
            m_focusNextPaneAction->trigger();
        return true;
    }
    if (id == QLatin1String("app.settings")) { openSettings(); return true; }
    if (id == QLatin1String("app.quit")) { close(); return true; }
    if (id == QLatin1String("view.sidebar")) {
        setSidebarVisible(!m_sidebarAction->isChecked(), true);
        return true;
    }
    if (id == QLatin1String("view.menuBar")) {
        if (m_menuBarAction)
            m_menuBarAction->toggle();
        return true;
    }
    if (id == QLatin1String("view.fullScreen")) {
        if (m_fullScreenAction)
            m_fullScreenAction->toggle();
        return true;
    }
    if (id == QLatin1String("edit.clear")) {
        if (auto *terminal = currentTerminal())
            terminal->clearScreen();
        return true;
    }
    if (id == QLatin1String("edit.copy")) {
        if (auto *terminal = currentTerminal())
            terminal->copySelection();
        return true;
    }
    if (id == QLatin1String("edit.paste")) {
        if (auto *terminal = currentTerminal())
            terminal->pasteClipboard();
        return true;
    }
    if (id == QLatin1String("workspace.save")) { saveCurrentWorkspace(); return true; }
    if (id == QLatin1String("monitor.toggle")) {
        setMonitorPanelVisible(!isMonitorPanelVisible());
        return true;
    }
    if (id == QLatin1String("monitor.overview")) {
        setMonitorPanelVisible(true);
        if (m_monitorPanel)
            m_monitorPanel->showSection(0);
        return true;
    }
    if (id == QLatin1String("monitor.processes")) {
        setMonitorPanelVisible(true);
        if (m_monitorPanel) {
            m_monitorPanel->showSection(1);
            m_monitorPanel->refreshProcesses(false);
        }
        return true;
    }
    if (id == QLatin1String("monitor.diagnostics")) {
        setMonitorPanelVisible(true);
        if (m_monitorPanel) {
            m_monitorPanel->showSection(2);
            m_monitorPanel->runDiagnostics();
        }
        return true;
    }
    if (id == QLatin1String("devPanel.toggle")) {
        setDevPanelVisible(!isDevPanelVisible());
        return true;
    }
    if (id == QLatin1String("devPanel.ssh")) {
        setDevPanelVisible(true);
        if (m_devPanel)
            m_devPanel->showSection(1);
        return true;
    }
    if (id == QLatin1String("devPanel.projects")) {
        setDevPanelVisible(true);
        if (m_devPanel)
            m_devPanel->showSection(2);
        return true;
    }
    if (id == QLatin1String("history.open")) { openHistory(0); return true; }
    if (id == QLatin1String("library.open")) { openCommandLibrary(); return true; }
    if (id == QLatin1String("notifications.history")) { openHistory(4); return true; }

    return false;
}

void MainWindow::openCommandLibrary()
{
    if (!m_libraryDialog)
        return;
    m_libraryDialog->reload();
    m_libraryDialog->show();
    m_libraryDialog->raise();
    m_libraryDialog->activateWindow();
    ActivityLog::record(tr("Открыта библиотека команд"), QString(), tr("команды"));
}

void MainWindow::openHistory(int section)
{
    if (!m_historyDialog) {
        m_historyDialog = new HistoryDialog(this);
        connect(m_historyDialog, &HistoryDialog::insertRequested, this, [this](const QString &command) {
            insertIntoTerminal(command);
        });
        connect(m_historyDialog, &HistoryDialog::jumpToLineRequested, this, [this](int line) {
            if (auto *terminal = currentTerminal())
                terminal->scrollToLine(line);
        });
        connect(m_historyDialog, &HistoryDialog::historyCleared, this, [this]() {
            ActivityLog::record(tr("Очищена история команд"), QString(), tr("история"));
            statusBar()->showMessage(tr("История команд очищена"), 4000);
        });
        connect(m_historyDialog, &HistoryDialog::activityCleared, this, [this]() {
            statusBar()->showMessage(tr("История действий очищена"), 4000);
        });
        m_historyDialog->setWindowFlag(Qt::Window, true);
    }

    m_historyDialog->setTerminal(currentTerminal());
    m_historyDialog->refreshAll();
    m_historyDialog->openSection(section);
    m_historyDialog->show();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

bool MainWindow::confirmCommand(const QString &title, const QString &command, bool destructive)
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Подтверждение"));
    box.setIcon(destructive ? QMessageBox::Warning : QMessageBox::Question);
    box.setText(title);
    box.setInformativeText(destructive
                               ? tr("Команда может изменить систему или удалить данные. "
                                    "Проверьте содержимое перед запуском.")
                               : tr("Выполнить команду в активном терминале?"));
    box.setDetailedText(command);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (QAbstractButton *yes = box.button(QMessageBox::Yes))
        yes->setText(tr("Выполнить"));
    if (QAbstractButton *no = box.button(QMessageBox::No))
        no->setText(tr("Отмена"));
    return box.exec() == QMessageBox::Yes;
}

bool MainWindow::runSavedCommand(const QString &id, bool confirmed)
{
    const SavedCommand command = CommandLibrary::command(id);
    if (!command.isValid()) {
        statusBar()->showMessage(tr("Команда не найдена в библиотеке"), 4000);
        return false;
    }

    QString text = command.command.trimmed();
    if (!command.workingDirectory.isEmpty())
        text = composeWorkingDirectory(command.workingDirectory, text);

    const bool destructive = CommandLibrary::isDestructive(command.command);
    if (CommandLibrary::needsConfirmation(command) && !confirmed) {
        if (!confirmCommand(tr("Выполнить «%1»?").arg(command.name), text, destructive))
            return false;
    }

    CommandLibrary::markUsed(command.id);
    ActivityLog::record(tr("Запущена команда из библиотеки"), command.name, tr("команды"));
    return runCommandText(text, CommandSource::Library, command.name,
                          command.command.contains(QLatin1Char('\n')));
}

bool MainWindow::runCommandText(const QString &command, CommandSource source, const QString &comment,
                                bool script)
{
    TerminalView *terminal = currentTerminal();
    if (!terminal || command.trimmed().isEmpty()) {
        statusBar()->showMessage(tr("Нет активного терминала"), 4000);
        return false;
    }

    const Settings &settings = Settings::instance();
    QString historyId;
    if (settings.recordHistory())
        historyId = CommandHistory::record(command, source, terminal->workingDirectory(), comment);

    terminal->sendTextToShell(command.trimmed() + QLatin1Char('\r'));
    terminal->setFocus();

    if (m_tasks) {
        const int terminalId = m_terminalIds.value(terminal, 0);
        m_lastTrackedHistoryId = historyId;
        m_tasks->begin(command, terminalId, historyId, script);
    }

    // В журнал команда попадает очищенной от секретов
    Log::info(tr("Выполняется команда (%1): %2").arg(comment.isEmpty() ? tr("вручную") : comment,
                                                     Secrets::commandForLog(command)));
    statusBar()->showMessage(tr("Выполняется: %1").arg(comment.isEmpty() ? command : comment), 5000);
    return true;
}

void MainWindow::onTaskFinished(int id, const QString &command, const QString &tag, int durationMs,
                                int exitCode, bool script)
{
    Q_UNUSED(id);
    if (!tag.isEmpty())
        CommandHistory::updateResult(tag, durationMs, exitCode);

    NotificationCenter::instance().notifyCommandFinished(
        command, durationMs, exitCode,
        script ? NotificationKind::Script : NotificationKind::LongCommand);

    if (durationMs >= NotificationCenter::instance().settings().longCommandSeconds * 1000) {
        statusBar()->showMessage(tr("Завершено за %1: %2")
                                     .arg(QString::number(durationMs / 1000.0, 'f', 1) + tr(" с"),
                                          command.simplified().left(80)),
                                 8000);
    }
}

int MainWindow::activeTaskCount() const
{
    return m_tasks ? m_tasks->activeCount() : 0;
}


} // namespace BlackCore
