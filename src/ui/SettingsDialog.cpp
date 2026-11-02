// SPDX-License-Identifier: MIT
#include "SettingsDialog.h"

#include "DialogHeader.h"

#include "../core/CommandLibrary.h"
#include "../core/History.h"
#include "../core/ConfigIO.h"
#include "../core/Logger.h"
#include "../core/Shortcuts.h"
#include "../core/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QWindow>
#include <QVBoxLayout>

namespace BlackCore {
namespace {

QLabel *hintLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", QStringLiteral("secondary"));
    label->setWordWrap(true);
    return label;
}

} // namespace

// ---------------------------------------------------------------------------
// Структуры значений
// ---------------------------------------------------------------------------

AppearanceSettings AppearanceSettings::fromSettings()
{
    const Settings &settings = Settings::instance();
    AppearanceSettings values;
    values.themeId = settings.themeId();
    values.fontFamily = settings.fontFamily();
    values.fontSize = settings.fontSize();
    values.lineSpacing = settings.lineSpacing();
    values.padding = settings.padding();
    values.cursorStyle = settings.cursorStyle();
    values.cursorBlink = settings.cursorBlink();
    values.windowOpacity = settings.windowOpacity();
    values.useSystemTitleBar = settings.useSystemTitleBar();
    values.uiScale = settings.uiScale();
    values.sidebarVisible = settings.sidebarVisible();
    values.statusBarVisible = settings.statusBarVisible();
    values.menuBarVisible = settings.menuBarVisible();
    values.tabBarVisible = settings.tabBarVisible();
    values.scrollbackLines = settings.scrollbackLines();
    values.wheelScrollLines = settings.wheelScrollLines();
    values.scrollOnOutput = settings.scrollOnOutput();
    values.scrollOnKeystroke = settings.scrollOnKeystroke();
    return values;
}

void AppearanceSettings::applyToSettings() const
{
    Settings &settings = Settings::instance();
    settings.setThemeId(themeId);
    settings.setFontFamily(fontFamily);
    settings.setFontSize(fontSize);
    settings.setLineSpacing(lineSpacing);
    settings.setPadding(padding);
    settings.setCursorStyle(cursorStyle);
    settings.setCursorBlink(cursorBlink);
    settings.setWindowOpacity(windowOpacity);
    settings.setUseSystemTitleBar(useSystemTitleBar);
    settings.setUiScale(uiScale);
    settings.setSidebarVisible(sidebarVisible);
    settings.setStatusBarVisible(statusBarVisible);
    settings.setMenuBarVisible(menuBarVisible);
    settings.setTabBarVisible(tabBarVisible);
    settings.setScrollbackLines(scrollbackLines);
    settings.setWheelScrollLines(wheelScrollLines);
    settings.setScrollOnOutput(scrollOnOutput);
    settings.setScrollOnKeystroke(scrollOnKeystroke);
}

BehaviorSettings BehaviorSettings::fromSettings()
{
    const Settings &settings = Settings::instance();
    BehaviorSettings values;
    values.shell = settings.shell();
    values.startDirectory = settings.startDirectory();
    values.restoreSession = settings.restoreSession();
    values.newTabInCurrentDirectory = settings.newTabInCurrentDirectory();
    values.warnOnCloseRunning = settings.warnOnCloseRunning();
    values.confirmCloseWindow = settings.confirmCloseWindow();
    values.confirmMultilinePaste = settings.confirmMultilinePaste();
    values.bellAudible = settings.bellAudible();
    values.autoSave = settings.autoSave();
    return values;
}

void BehaviorSettings::applyToSettings() const
{
    Settings &settings = Settings::instance();
    if (!shell.isEmpty())
        settings.setShell(shell);
    settings.setStartDirectory(startDirectory);
    settings.setRestoreSession(restoreSession);
    settings.setNewTabInCurrentDirectory(newTabInCurrentDirectory);
    settings.setWarnOnCloseRunning(warnOnCloseRunning);
    settings.setConfirmCloseWindow(confirmCloseWindow);
    settings.setConfirmMultilinePaste(confirmMultilinePaste);
    settings.setBellAudible(bellAudible);
    settings.setAutoSave(autoSave);
}

// ---------------------------------------------------------------------------
// Диалог
// ---------------------------------------------------------------------------

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Настройки BlackCore"));
    setModal(true);
    setMinimumWidth(680);
    setWindowFlag(Qt::FramelessWindowHint, true);
    buildUi();
    loadValues();
}

void SettingsDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    outer->addWidget(new DialogHeader(tr("Настройки BlackCore"), this));

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);
    outer->addLayout(layout, 1);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setUsesScrollButtons(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    layout->addWidget(m_tabs, 1);

    m_tabs->addTab(buildAppearancePage(), tr("Оформление"));
    m_tabs->addTab(buildPanelsPage(), tr("Панели и прокрутка"));
    m_tabs->addTab(buildBehaviorPage(), tr("Поведение"));
    m_tabs->addTab(buildNotificationsPage(), tr("Уведомления"));
    m_tabs->addTab(buildShortcutsPage(), tr("Горячие клавиши"));
    m_tabs->addTab(buildConfigPage(), tr("Конфигурация"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                             | QDialogButtonBox::Apply
                                             | QDialogButtonBox::RestoreDefaults,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    buttons->button(QDialogButtonBox::Apply)->setText(tr("Применить"));
    buttons->button(QDialogButtonBox::Apply)->setToolTip(
        tr("Применить и сохранить, не закрывая окно"));
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(tr("По умолчанию"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SettingsDialog::apply);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &SettingsDialog::resetToDefaults);
}

QWidget *SettingsDialog::buildAppearancePage()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // --- тема ---
    auto *themeGroup = new QGroupBox(tr("Тема"), page);
    auto *themeForm = new QFormLayout(themeGroup);
    m_themeCombo = new QComboBox(themeGroup);
    for (const Theme &theme : ThemeManager::themes()) {
        const QString label = theme.dark ? tr("%1 (тёмная)").arg(theme.name) : theme.name;
        m_themeCombo->addItem(label, theme.id);
    }
    themeForm->addRow(tr("Цветовая схема:"), m_themeCombo);
    m_themeHint = hintLabel(QString(), themeGroup);
    themeForm->addRow(QString(), m_themeHint);
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        m_themeHint->setText(ThemeManager::theme(m_themeCombo->currentData().toString()).description);
        emitPreview();
    });
    layout->addWidget(themeGroup);

    // --- шрифт ---
    auto *fontGroup = new QGroupBox(tr("Шрифт и текст"), page);
    auto *fontForm = new QFormLayout(fontGroup);
    m_fontCombo = new QFontComboBox(fontGroup);
    m_fontCombo->setFontFilters(QFontComboBox::MonospacedFonts);
    fontForm->addRow(tr("Моноширинный шрифт:"), m_fontCombo);

    m_fontSizeSpin = new QSpinBox(fontGroup);
    m_fontSizeSpin->setRange(Settings::kMinFontSize, Settings::kMaxFontSize);
    m_fontSizeSpin->setSuffix(tr(" pt"));
    fontForm->addRow(tr("Размер текста:"), m_fontSizeSpin);

    m_lineSpacingSpin = new QSpinBox(fontGroup);
    m_lineSpacingSpin->setRange(qRound(Settings::kMinLineSpacing * 100),
                                qRound(Settings::kMaxLineSpacing * 100));
    m_lineSpacingSpin->setSingleStep(5);
    m_lineSpacingSpin->setSuffix(tr(" %"));
    fontForm->addRow(tr("Межстрочный интервал:"), m_lineSpacingSpin);

    m_paddingSpin = new QSpinBox(fontGroup);
    m_paddingSpin->setRange(Settings::kMinPadding, Settings::kMaxPadding);
    m_paddingSpin->setSuffix(tr(" px"));
    fontForm->addRow(tr("Отступы от края:"), m_paddingSpin);
    layout->addWidget(fontGroup);

    // --- курсор ---
    auto *cursorGroup = new QGroupBox(tr("Курсор"), page);
    auto *cursorForm = new QFormLayout(cursorGroup);
    m_cursorCombo = new QComboBox(cursorGroup);
    m_cursorCombo->addItem(tr("Как в программе (по умолчанию)"),
                           static_cast<int>(CursorStyle::FollowApplication));
    m_cursorCombo->addItem(tr("Блок"), static_cast<int>(CursorStyle::Block));
    m_cursorCombo->addItem(tr("Подчёркивание"), static_cast<int>(CursorStyle::Underline));
    m_cursorCombo->addItem(tr("Вертикальная черта"), static_cast<int>(CursorStyle::Bar));
    cursorForm->addRow(tr("Форма курсора:"), m_cursorCombo);

    m_cursorBlinkCheck = new QCheckBox(tr("Мигание курсора"), cursorGroup);
    cursorForm->addRow(QString(), m_cursorBlinkCheck);
    layout->addWidget(cursorGroup);

    // --- окно ---
    auto *windowGroup = new QGroupBox(tr("Окно"), page);
    auto *windowForm = new QFormLayout(windowGroup);
    auto *opacityRow = new QWidget(windowGroup);
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    m_opacitySlider = new QSlider(Qt::Horizontal, opacityRow);
    m_opacitySlider->setRange(Settings::kMinOpacity, Settings::kMaxOpacity);
    m_opacitySlider->setSingleStep(2);
    m_opacitySlider->setPageStep(10);
    m_opacityValue = new QLabel(opacityRow);
    m_opacityValue->setMinimumWidth(46);
    m_opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityLayout->addWidget(m_opacitySlider, 1);
    opacityLayout->addWidget(m_opacityValue);
    windowForm->addRow(tr("Прозрачность:"), opacityRow);
    windowForm->addRow(QString(), hintLabel(tr("Требуется композитор рабочего стола "
                                               "(в Cinnamon он включён по умолчанию)."),
                                            windowGroup));

    m_systemTitleCheck = new QCheckBox(tr("Использовать системный заголовок окна"), windowGroup);
    windowForm->addRow(QString(), m_systemTitleCheck);

    m_scaleCombo = new QComboBox(windowGroup);
    m_scaleCombo->addItem(tr("Автоматически (по системе)"), 1.0);
    m_scaleCombo->addItem(QStringLiteral("125 %"), 1.25);
    m_scaleCombo->addItem(QStringLiteral("150 %"), 1.5);
    m_scaleCombo->addItem(QStringLiteral("175 %"), 1.75);
    m_scaleCombo->addItem(QStringLiteral("200 %"), 2.0);
    windowForm->addRow(tr("Масштаб интерфейса:"), m_scaleCombo);
    windowForm->addRow(QString(), hintLabel(tr("HiDPI: явный масштаб применяется при "
                                               "следующем запуске."),
                                            windowGroup));
    layout->addWidget(windowGroup);
    layout->addStretch(1);

    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this]() { emitPreview(); });
    connect(m_fontSizeSpin, &QSpinBox::valueChanged, this, [this]() { emitPreview(); });
    connect(m_lineSpacingSpin, &QSpinBox::valueChanged, this, [this]() { emitPreview(); });
    connect(m_paddingSpin, &QSpinBox::valueChanged, this, [this]() { emitPreview(); });
    connect(m_cursorCombo, &QComboBox::currentIndexChanged, this, [this]() { emitPreview(); });
    connect(m_cursorBlinkCheck, &QCheckBox::toggled, this, [this]() { emitPreview(); });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(QStringLiteral("%1 %").arg(value));
        emitPreview();
    });
    connect(m_systemTitleCheck, &QCheckBox::toggled, this, [this]() { emitPreview(); });
    connect(m_scaleCombo, &QComboBox::currentIndexChanged, this, [this]() { emitPreview(); });
    return page;
}

QWidget *SettingsDialog::buildPanelsPage()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto *panelsGroup = new QGroupBox(tr("Видимость панелей"), page);
    auto *panelsLayout = new QVBoxLayout(panelsGroup);
    m_sidebarCheck = new QCheckBox(tr("Боковая панель со списком сессий"), panelsGroup);
    m_statusBarCheck = new QCheckBox(tr("Строка состояния (каталог, размер, тема)"), panelsGroup);
    m_menuBarCheck = new QCheckBox(tr("Главное меню"), panelsGroup);
    m_tabBarCheck = new QCheckBox(tr("Полоса вкладок"), panelsGroup);
    panelsLayout->addWidget(m_sidebarCheck);
    panelsLayout->addWidget(m_statusBarCheck);
    panelsLayout->addWidget(m_menuBarCheck);
    panelsLayout->addWidget(m_tabBarCheck);
    panelsLayout->addWidget(hintLabel(tr("Скрытые панели не занимают место на экране; "
                                         "боковая панель по-прежнему переключается клавишей F9, "
                                         "меню возвращается сочетанием из раздела «Горячие клавиши» "
                                         "или сбросом настроек."),
                                      panelsGroup));
    layout->addWidget(panelsGroup);

    auto *scrollGroup = new QGroupBox(tr("Прокрутка"), page);
    auto *scrollForm = new QFormLayout(scrollGroup);

    m_scrollbackSpin = new QSpinBox(scrollGroup);
    m_scrollbackSpin->setRange(Settings::kMinScrollback, Settings::kMaxScrollback);
    m_scrollbackSpin->setSingleStep(500);
    m_scrollbackSpin->setSuffix(tr(" строк"));
    scrollForm->addRow(tr("Глубина буфера:"), m_scrollbackSpin);
    scrollForm->addRow(QString(), hintLabel(tr("Сколько строк истории хранит каждая сессия. "
                                               "Большие значения требуют больше памяти "
                                               "(≈ 4 КБ на строку)."),
                                            scrollGroup));

    m_wheelSpin = new QSpinBox(scrollGroup);
    m_wheelSpin->setRange(Settings::kMinWheelLines, Settings::kMaxWheelLines);
    m_wheelSpin->setSuffix(tr(" строк"));
    scrollForm->addRow(tr("Шаг колеса мыши:"), m_wheelSpin);

    m_scrollOutputCheck = new QCheckBox(tr("Прокручивать к новому выводу"), scrollGroup);
    m_scrollOutputCheck->setToolTip(tr("Если выключено, при чтении истории новый вывод "
                                       "не «уводит» экран вниз."));
    scrollForm->addRow(QString(), m_scrollOutputCheck);

    m_scrollKeystrokeCheck = new QCheckBox(tr("Возвращаться вниз при вводе"), scrollGroup);
    scrollForm->addRow(QString(), m_scrollKeystrokeCheck);
    layout->addWidget(scrollGroup);
    layout->addStretch(1);

    for (QCheckBox *check : {m_sidebarCheck, m_statusBarCheck, m_menuBarCheck, m_tabBarCheck})
        connect(check, &QCheckBox::toggled, this, [this]() { emitPreview(); });
    for (QSpinBox *spin : {m_scrollbackSpin, m_wheelSpin})
        connect(spin, &QSpinBox::valueChanged, this, [this]() { emitPreview(); });
    for (QCheckBox *check : {m_scrollOutputCheck, m_scrollKeystrokeCheck})
        connect(check, &QCheckBox::toggled, this, [this]() { emitPreview(); });
    return page;
}

QWidget *SettingsDialog::buildBehaviorPage()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto *shellGroup = new QGroupBox(tr("Оболочка и каталог"), page);
    auto *shellForm = new QFormLayout(shellGroup);
    m_shellCombo = new QComboBox(shellGroup);
    m_shellCombo->setEditable(true);
    m_shellCombo->setInsertPolicy(QComboBox::NoInsert);

    QStringList shells;
    QFile shellsFile(QStringLiteral("/etc/shells"));
    if (shellsFile.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : shellsFile.readAll().split('\n')) {
            const QString path = QString::fromLocal8Bit(line).trimmed();
            if (path.isEmpty() || path.startsWith(QLatin1Char('#')))
                continue;
            if (QFileInfo(path).isExecutable())
                shells.append(path);
        }
    }
    if (!shells.contains(Settings::defaultShell()))
        shells.prepend(Settings::defaultShell());
    shells.removeDuplicates();
    m_shellCombo->addItems(shells);
    shellForm->addRow(tr("Оболочка:"), m_shellCombo);
    shellForm->addRow(QString(), hintLabel(tr("BlackCore запускает оболочку в своём терминале "
                                               "и не меняет системную оболочку пользователя, "
                                               "не изменяет .bashrc и .zshrc."),
                                            shellGroup));

    auto *startRow = new QWidget(shellGroup);
    auto *startLayout = new QHBoxLayout(startRow);
    startLayout->setContentsMargins(0, 0, 0, 0);
    m_startDirEdit = new QLineEdit(startRow);
    m_startDirEdit->setPlaceholderText(tr("по умолчанию: последний использованный каталог"));
    auto *browseButton = new QPushButton(tr("Обзор…"), startRow);
    startLayout->addWidget(m_startDirEdit, 1);
    startLayout->addWidget(browseButton);
    shellForm->addRow(tr("Стартовый каталог:"), startRow);
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Стартовый каталог"), m_startDirEdit->text());
        if (!directory.isEmpty())
            m_startDirEdit->setText(directory);
    });
    layout->addWidget(shellGroup);

    auto *tabsGroup = new QGroupBox(tr("Вкладки и сессии"), page);
    auto *tabsLayout = new QVBoxLayout(tabsGroup);
    m_restoreCheck = new QCheckBox(tr("Восстанавливать вкладки, панели и каталоги при запуске"),
                                   tabsGroup);
    m_restoreCheck->setToolTip(tr("Команды при восстановлении не выполняются."));
    m_newTabDirCheck = new QCheckBox(tr("Новая вкладка в каталоге активной панели"), tabsGroup);
    tabsLayout->addWidget(m_restoreCheck);
    tabsLayout->addWidget(m_newTabDirCheck);
    layout->addWidget(tabsGroup);

    auto *warnGroup = new QGroupBox(tr("Предупреждения"), page);
    auto *warnLayout = new QVBoxLayout(warnGroup);
    m_warnCloseCheck = new QCheckBox(tr("Спрашивать при закрытии вкладки или панели "
                                        "с запущенным процессом"),
                                     warnGroup);
    m_confirmWindowCheck = new QCheckBox(tr("Спрашивать при закрытии окна"), warnGroup);
    m_confirmPasteCheck = new QCheckBox(tr("Подтверждать вставку нескольких строк"), warnGroup);
    m_bellCheck = new QCheckBox(tr("Звуковой сигнал терминала (bell)"), warnGroup);
    warnLayout->addWidget(m_warnCloseCheck);
    warnLayout->addWidget(m_confirmWindowCheck);
    warnLayout->addWidget(m_confirmPasteCheck);
    warnLayout->addWidget(m_bellCheck);
    layout->addWidget(warnGroup);

    auto *saveGroup = new QGroupBox(tr("Сохранение"), page);
    auto *saveLayout = new QVBoxLayout(saveGroup);
    m_autoSaveCheck = new QCheckBox(tr("Сохранять настройки сразу после изменения"), saveGroup);
    saveLayout->addWidget(m_autoSaveCheck);
    saveLayout->addWidget(hintLabel(tr("Настройки хранятся в файле конфигурации BlackCore; "
                                       "файлы оболочки (.bashrc, .zshrc) не изменяются."),
                                    saveGroup));
    layout->addWidget(saveGroup);
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildNotificationsPage()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto *masterGroup = new QGroupBox(tr("Уведомления"), page);
    auto *masterLayout = new QVBoxLayout(masterGroup);
    m_notifyEnabledCheck = new QCheckBox(tr("Показывать уведомления"), masterGroup);
    m_notifyEnabledCheck->setObjectName(QStringLiteral("notifyEnabled"));
    m_notifyEnabledCheck->setToolTip(tr("Полное отключение: ни одного уведомления, включая ошибки"));
    masterLayout->addWidget(m_notifyEnabledCheck);
    masterLayout->addWidget(hintLabel(tr("Если выключить этот пункт, BlackCore не показывает "
                                         "никаких уведомлений. На работу терминала это не влияет."),
                                      masterGroup));
    layout->addWidget(masterGroup);

    auto *kindsGroup = new QGroupBox(tr("Что уведомлять"), page);
    auto *kindsLayout = new QVBoxLayout(kindsGroup);
    m_notifyLongCheck = new QCheckBox(tr("Завершение долгой команды"), kindsGroup);
    m_notifyLongCheck->setObjectName(QStringLiteral("notifyLongCommand"));
    m_notifyScriptCheck = new QCheckBox(tr("Завершение сохранённого сценария"), kindsGroup);
    m_notifyScriptCheck->setObjectName(QStringLiteral("notifyScript"));
    m_notifySshCheck = new QCheckBox(tr("Потеря SSH-соединения"), kindsGroup);
    m_notifySshCheck->setObjectName(QStringLiteral("notifySsh"));
    m_notifyErrorsCheck = new QCheckBox(tr("Ошибки приложения"), kindsGroup);
    m_notifyErrorsCheck->setObjectName(QStringLiteral("notifyErrors"));
    m_notifyDesktopCheck = new QCheckBox(tr("Дублировать системными уведомлениями рабочего стола"),
                                         kindsGroup);
    m_notifyDesktopCheck->setObjectName(QStringLiteral("notifyDesktop"));
    m_notifyDesktopCheck->setToolTip(tr("Работает, если в системе доступен лоток уведомлений"));

    auto *secondsRow = new QWidget(kindsGroup);
    auto *secondsLayout = new QHBoxLayout(secondsRow);
    secondsLayout->setContentsMargins(0, 0, 0, 0);
    auto *secondsLabel = new QLabel(tr("Команда считается долгой, если длится дольше:"), secondsRow);
    m_notifySecondsSpin = new QSpinBox(secondsRow);
    m_notifySecondsSpin->setObjectName(QStringLiteral("notifySeconds"));
    m_notifySecondsSpin->setRange(NotificationSettings::kMinSeconds,
                                  NotificationSettings::kMaxSeconds);
    m_notifySecondsSpin->setSuffix(tr(" с"));
    secondsLayout->addWidget(secondsLabel);
    secondsLayout->addWidget(m_notifySecondsSpin);
    secondsLayout->addStretch(1);

    kindsLayout->addWidget(m_notifyLongCheck);
    kindsLayout->addWidget(secondsRow);
    kindsLayout->addWidget(m_notifyScriptCheck);
    kindsLayout->addWidget(m_notifySshCheck);
    kindsLayout->addWidget(m_notifyErrorsCheck);
    kindsLayout->addWidget(m_notifyDesktopCheck);
    layout->addWidget(kindsGroup);

    auto *historyGroup = new QGroupBox(tr("История"), page);
    auto *historyLayout = new QVBoxLayout(historyGroup);
    auto *recordCheck = new QCheckBox(tr("Вести историю команд и действий приложения"), historyGroup);
    m_recordHistoryCheck = recordCheck;
    recordCheck->setObjectName(QStringLiteral("recordHistory"));
    recordCheck->setChecked(Settings::instance().recordHistory());
    recordCheck->setToolTip(tr("История хранится локально и очищается по вашей команде"));
    auto *clearRow = new QHBoxLayout;
    auto *clearCommandsButton = new QPushButton(tr("Очистить историю команд"), historyGroup);
    auto *clearActivityButton = new QPushButton(tr("Очистить историю действий"), historyGroup);
    clearRow->addWidget(clearCommandsButton);
    clearRow->addWidget(clearActivityButton);
    clearRow->addStretch(1);
    historyLayout->addWidget(recordCheck);
    historyLayout->addLayout(clearRow);
    historyLayout->addWidget(hintLabel(tr("В историю попадают только команды, запущенные через "
                                          "BlackCore; ввод в терминале не записывается. Пароли и "
                                          "токены маскируются и в журнале, и в истории."),
                                       historyGroup));
    layout->addWidget(historyGroup);
    layout->addStretch(1);

    connect(recordCheck, &QCheckBox::toggled, this, [](bool enabled) {
        Settings::instance().setRecordHistory(enabled);
        Settings::instance().save();
    });
    connect(clearCommandsButton, &QPushButton::clicked, this, []() {
        CommandHistory::clear();
    });
    connect(clearActivityButton, &QPushButton::clicked, this, []() {
        ActivityLog::clear();
    });
    connect(m_notifyEnabledCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_updating)
            return;
        // Полное отключение блокирует и частные настройки, но не сбрасывает их
        m_notifyLongCheck->setEnabled(enabled);
        m_notifySecondsSpin->setEnabled(enabled);
        m_notifyScriptCheck->setEnabled(enabled);
        m_notifySshCheck->setEnabled(enabled);
        m_notifyErrorsCheck->setEnabled(enabled);
        m_notifyDesktopCheck->setEnabled(enabled);
    });

    return page;
}

QWidget *SettingsDialog::buildShortcutsPage()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_shortcutTable = new QTableWidget(page);
    m_shortcutTable->setColumnCount(3);
    m_shortcutTable->setHorizontalHeaderLabels(
        {tr("Раздел"), tr("Действие"), tr("Клавиши")});
    m_shortcutTable->verticalHeader()->setVisible(false);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_shortcutTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_shortcutTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                     | QAbstractItemView::SelectedClicked
                                     | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_shortcutTable, 1);

    layout->addWidget(hintLabel(tr("Дважды щёлкните по клавишам, чтобы назначить новые "
                                   "(Esc — отменить ввод, Delete — отключить). "
                                   "Изменения применяются после «Применить» или «Сохранить»."),
                                page));

    auto *buttonsRow = new QHBoxLayout;
    auto *resetOne = new QPushButton(tr("Вернуть по умолчанию"), page);
    auto *resetAll = new QPushButton(tr("Сбросить все клавиши"), page);
    buttonsRow->addWidget(resetOne);
    buttonsRow->addWidget(resetAll);
    buttonsRow->addStretch(1);
    layout->addLayout(buttonsRow);

    connect(resetOne, &QPushButton::clicked, this, [this]() {
        const int row = m_shortcutTable->currentRow();
        if (row < 0)
            return;
        const QString id = m_shortcutTable->item(row, 1)->data(Qt::UserRole).toString();
        auto *editor = qobject_cast<QKeySequenceEdit *>(m_shortcutTable->cellWidget(row, 2));
        const QKeySequence key = ShortcutManager::defaultKey(id);
        if (editor)
            editor->setKeySequence(key);
    });
    connect(resetAll, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
            const QString id = m_shortcutTable->item(row, 1)->data(Qt::UserRole).toString();
            auto *editor = qobject_cast<QKeySequenceEdit *>(m_shortcutTable->cellWidget(row, 2));
            if (editor && editor->keySequence() != ShortcutManager::defaultKey(id))
                editor->setKeySequence(ShortcutManager::defaultKey(id));
        }
    });

    return page;
}

QWidget *SettingsDialog::buildConfigPage()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto *fileGroup = new QGroupBox(tr("Файл настроек"), page);
    auto *fileLayout = new QVBoxLayout(fileGroup);
    m_configPathLabel = new QLabel(ConfigIO::configFilePath(), fileGroup);
    m_configPathLabel->setWordWrap(true);
    m_configPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileLayout->addWidget(hintLabel(tr("Все настройки BlackCore хранятся в этом файле:"), fileGroup));
    fileLayout->addWidget(m_configPathLabel);
    layout->addWidget(fileGroup);

    auto *actionsGroup = new QGroupBox(tr("Экспорт и импорт"), page);
    auto *actionsLayout = new QVBoxLayout(actionsGroup);
    auto *row = new QHBoxLayout;
    auto *exportButton = new QPushButton(tr("Экспортировать…"), actionsGroup);
    exportButton->setToolTip(tr("Сохранить настройки, горячие клавиши, профили и рабочие "
                                "пространства в один файл JSON"));
    auto *importButton = new QPushButton(tr("Импортировать…"), actionsGroup);
    importButton->setToolTip(tr("Проверить файл, создать резервную копию и применить настройки"));
    row->addWidget(exportButton);
    row->addWidget(importButton);
    row->addStretch(1);
    actionsLayout->addLayout(row);

    m_backupLabel = hintLabel(QString(), actionsGroup);
    actionsLayout->addWidget(m_backupLabel);
    actionsLayout->addWidget(hintLabel(tr("Перед импортом и сбросом автоматически создаётся "
                                          "резервная копия в каталоге состояния BlackCore."),
                                       actionsGroup));
    layout->addWidget(actionsGroup);

    auto *resetGroup = new QGroupBox(tr("Сброс"), page);
    auto *resetLayout = new QVBoxLayout(resetGroup);
    auto *resetButton = new QPushButton(tr("Сбросить все настройки"), resetGroup);
    resetButton->setToolTip(tr("Вернуть темы, шрифты, поведение и горячие клавиши "
                               "к значениям по умолчанию (с резервной копией)"));
    resetLayout->addWidget(resetButton);
    resetLayout->addWidget(hintLabel(tr("Сбрасываются только настройки BlackCore. "
                                        "Файлы оболочки и системные настройки не затрагиваются."),
                                     resetGroup));
    layout->addWidget(resetGroup);
    layout->addStretch(1);

    m_configButtons = {exportButton, importButton, resetButton};

    connect(exportButton, &QPushButton::clicked, this, &SettingsDialog::exportConfig);
    connect(importButton, &QPushButton::clicked, this, &SettingsDialog::importConfig);
    connect(resetButton, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);
    return page;
}

void SettingsDialog::loadValues()
{
    m_updating = true;

    const AppearanceSettings appearanceValues = AppearanceSettings::fromSettings();
    const int themeIndex = m_themeCombo->findData(appearanceValues.themeId);
    m_themeCombo->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    m_themeHint->setText(
        ThemeManager::theme(m_themeCombo->currentData().toString()).description);

    m_fontCombo->setCurrentFont(QFont(appearanceValues.fontFamily));
    m_fontSizeSpin->setValue(appearanceValues.fontSize);
    m_lineSpacingSpin->setValue(qRound(appearanceValues.lineSpacing * 100));
    m_paddingSpin->setValue(appearanceValues.padding);
    m_cursorCombo->setCurrentIndex(m_cursorCombo->findData(static_cast<int>(appearanceValues.cursorStyle)));
    m_cursorBlinkCheck->setChecked(appearanceValues.cursorBlink);
    m_opacitySlider->setValue(appearanceValues.windowOpacity);
    m_opacityValue->setText(QStringLiteral("%1 %").arg(appearanceValues.windowOpacity));
    m_systemTitleCheck->setChecked(appearanceValues.useSystemTitleBar);

    int scaleIndex = 0;
    for (int i = 0; i < m_scaleCombo->count(); ++i) {
        if (qFuzzyCompare(m_scaleCombo->itemData(i).toReal(), appearanceValues.uiScale))
            scaleIndex = i;
    }
    m_scaleCombo->setCurrentIndex(scaleIndex);

    m_sidebarCheck->setChecked(appearanceValues.sidebarVisible);
    m_statusBarCheck->setChecked(appearanceValues.statusBarVisible);
    m_menuBarCheck->setChecked(appearanceValues.menuBarVisible);
    m_tabBarCheck->setChecked(appearanceValues.tabBarVisible);
    m_scrollbackSpin->setValue(appearanceValues.scrollbackLines);
    m_wheelSpin->setValue(appearanceValues.wheelScrollLines);
    m_scrollOutputCheck->setChecked(appearanceValues.scrollOnOutput);
    m_scrollKeystrokeCheck->setChecked(appearanceValues.scrollOnKeystroke);

    const BehaviorSettings behaviorValues = BehaviorSettings::fromSettings();
    const int shellIndex = m_shellCombo->findText(behaviorValues.shell);
    if (shellIndex >= 0)
        m_shellCombo->setCurrentIndex(shellIndex);
    else
        m_shellCombo->setEditText(behaviorValues.shell);
    m_startDirEdit->setText(behaviorValues.startDirectory);
    m_restoreCheck->setChecked(behaviorValues.restoreSession);
    m_newTabDirCheck->setChecked(behaviorValues.newTabInCurrentDirectory);
    m_warnCloseCheck->setChecked(behaviorValues.warnOnCloseRunning);
    m_confirmWindowCheck->setChecked(behaviorValues.confirmCloseWindow);
    m_confirmPasteCheck->setChecked(behaviorValues.confirmMultilinePaste);
    m_bellCheck->setChecked(behaviorValues.bellAudible);
    m_autoSaveCheck->setChecked(behaviorValues.autoSave);

    // Уведомления: общий выключатель, виды, порог и история
    const NotificationSettings notifications = NotificationSettings::fromSettings();
    m_notifyEnabledCheck->setChecked(notifications.enabled);
    m_notifyLongCheck->setChecked(notifications.longCommand);
    m_notifyScriptCheck->setChecked(notifications.script);
    m_notifySshCheck->setChecked(notifications.sshDisconnected);
    m_notifyErrorsCheck->setChecked(notifications.errors);
    m_notifyDesktopCheck->setChecked(notifications.desktop);
    m_notifySecondsSpin->setValue(notifications.longCommandSeconds);
    m_notifyLongCheck->setEnabled(notifications.enabled);
    m_notifySecondsSpin->setEnabled(notifications.enabled);
    m_notifyScriptCheck->setEnabled(notifications.enabled);
    m_notifySshCheck->setEnabled(notifications.enabled);
    m_notifyErrorsCheck->setEnabled(notifications.enabled);
    m_notifyDesktopCheck->setEnabled(notifications.enabled);
    if (m_recordHistoryCheck)
        m_recordHistoryCheck->setChecked(Settings::instance().recordHistory());

    loadShortcutTable();

    m_updating = false;
}

void SettingsDialog::loadShortcutTable()
{
    const QList<ShortcutSpec> &catalog = ShortcutManager::catalog();
    m_shortcutTable->setRowCount(catalog.size());

    for (int row = 0; row < catalog.size(); ++row) {
        const ShortcutSpec &spec = catalog.at(row);

        auto *groupItem = new QTableWidgetItem(spec.group);
        groupItem->setFlags(Qt::ItemIsEnabled);
        m_shortcutTable->setItem(row, 0, groupItem);

        auto *titleItem = new QTableWidgetItem(spec.title);
        titleItem->setFlags(Qt::ItemIsEnabled);
        titleItem->setData(Qt::UserRole, spec.id);
        m_shortcutTable->setItem(row, 1, titleItem);

        auto *editor = new QKeySequenceEdit(ShortcutManager::key(spec.id), m_shortcutTable);
        editor->setClearButtonEnabled(true);
        m_shortcutTable->setCellWidget(row, 2, editor);
    }
    m_shortcutTable->resizeRowsToContents();
}

void SettingsDialog::emitPreview()
{
    if (m_updating)
        return;
    emit previewChanged(appearance());
}

AppearanceSettings SettingsDialog::appearance() const
{
    AppearanceSettings values;
    values.themeId = m_themeCombo->currentData().toString();
    values.fontFamily = m_fontCombo->currentFont().family();
    values.fontSize = m_fontSizeSpin->value();
    values.lineSpacing = m_lineSpacingSpin->value() / 100.0;
    values.padding = m_paddingSpin->value();
    values.cursorStyle = static_cast<CursorStyle>(m_cursorCombo->currentData().toInt());
    values.cursorBlink = m_cursorBlinkCheck->isChecked();
    values.windowOpacity = m_opacitySlider->value();
    values.useSystemTitleBar = m_systemTitleCheck->isChecked();
    values.uiScale = m_scaleCombo->currentData().toReal();
    values.sidebarVisible = m_sidebarCheck->isChecked();
    values.statusBarVisible = m_statusBarCheck->isChecked();
    values.menuBarVisible = m_menuBarCheck->isChecked();
    values.tabBarVisible = m_tabBarCheck->isChecked();
    values.scrollbackLines = m_scrollbackSpin->value();
    values.wheelScrollLines = m_wheelSpin->value();
    values.scrollOnOutput = m_scrollOutputCheck->isChecked();
    values.scrollOnKeystroke = m_scrollKeystrokeCheck->isChecked();
    return values;
}

BehaviorSettings SettingsDialog::behavior() const
{
    BehaviorSettings values;
    values.shell = m_shellCombo->currentText().trimmed();
    values.startDirectory = m_startDirEdit->text().trimmed();
    values.restoreSession = m_restoreCheck->isChecked();
    values.newTabInCurrentDirectory = m_newTabDirCheck->isChecked();
    values.warnOnCloseRunning = m_warnCloseCheck->isChecked();
    values.confirmCloseWindow = m_confirmWindowCheck->isChecked();
    values.confirmMultilinePaste = m_confirmPasteCheck->isChecked();
    values.bellAudible = m_bellCheck->isChecked();
    values.autoSave = m_autoSaveCheck->isChecked();
    return values;
}

void SettingsDialog::apply()
{
    // Горячие клавиши: проверяем корректность и конфликты до сохранения
    QStringList conflicts;
    for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
        const QString id = m_shortcutTable->item(row, 1)->data(Qt::UserRole).toString();
        auto *editor = qobject_cast<QKeySequenceEdit *>(m_shortcutTable->cellWidget(row, 2));
        if (!editor)
            continue;
        const QKeySequence key = editor->keySequence();
        ShortcutManager::setKey(id, key);
    }
    conflicts = ShortcutManager::conflicts();

    NotificationSettings notifications = NotificationSettings::fromSettings();
    notifications.enabled = m_notifyEnabledCheck->isChecked();
    notifications.longCommand = m_notifyLongCheck->isChecked();
    notifications.script = m_notifyScriptCheck->isChecked();
    notifications.sshDisconnected = m_notifySshCheck->isChecked();
    notifications.errors = m_notifyErrorsCheck->isChecked();
    notifications.desktop = m_notifyDesktopCheck->isChecked();
    notifications.longCommandSeconds = m_notifySecondsSpin->value();
    NotificationCenter::instance().setSettings(notifications);

    behavior().applyToSettings();
    appearance().applyToSettings();
    Settings::instance().save();

    emit settingsApplied();
    emit previewChanged(appearance());

    if (!conflicts.isEmpty()) {
        QMessageBox::warning(this, tr("Горячие клавиши"),
                             tr("Некоторые действия используют одинаковые клавиши:\n\n%1\n\n"
                                "Сработает то действие, которое раньше добавлено в окно.")
                                 .arg(conflicts.join(QLatin1Char('\n'))));
    }
}

void SettingsDialog::resetToDefaults()
{
    const auto answer = QMessageBox::question(
        this, tr("Сбросить настройки?"),
        tr("Вернуть все настройки BlackCore к значениям по умолчанию?\n\n"
           "Перед сбросом будет создана резервная копия текущего файла настроек.\n"
           "Файлы .bashrc/.zshrc и системная оболочка не затрагиваются."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString backupPath;
    QString error;
    if (!ConfigIO::resetToDefaults(&backupPath, &error)) {
        QMessageBox::warning(this, tr("Сброс настроек"), error);
        return;
    }

    m_updating = true;
    loadValues();
    m_updating = false;

    m_backupLabel->setText(tr("Резервная копия: %1").arg(backupPath));
    emit settingsApplied();
    emit previewChanged(appearance());
}

void SettingsDialog::exportConfig()
{
    const QString suggested = QDir::homePath()
        + QStringLiteral("/blackcore-config-%1.json")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Экспорт конфигурации"), suggested,
        tr("Конфигурация BlackCore (*.json);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!ConfigIO::exportToFile(path, &error)) {
        QMessageBox::warning(this, tr("Экспорт конфигурации"), error);
        return;
    }

    m_backupLabel->setText(tr("Экспортировано: %1").arg(path));
    QMessageBox::information(this, tr("Экспорт конфигурации"),
                             tr("Настройки сохранены в файл:\n%1").arg(path));
}

void SettingsDialog::importConfig()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Импорт конфигурации"), QDir::homePath(),
        tr("Конфигурация BlackCore (*.json);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    QStringList warnings;
    QString error;
    if (!ConfigIO::importFromFile(path, &error, &warnings)) {
        QMessageBox::warning(this, tr("Импорт конфигурации"),
                             tr("Файл не применён.\n\n%1").arg(error));
        return;
    }

    m_importedPath = path;
    m_updating = true;
    loadValues();
    m_updating = false;

    QString text = tr("Настройки импортированы из:\n%1").arg(path);
    if (!warnings.isEmpty())
        text += QStringLiteral("\n\n") + warnings.join(QLatin1Char('\n'));
    m_backupLabel->setText(text);
    QMessageBox::information(this, tr("Импорт конфигурации"), text);

    emit settingsApplied();
    emit previewChanged(appearance());
}

} // namespace BlackCore
