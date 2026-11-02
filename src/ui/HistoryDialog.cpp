// SPDX-License-Identifier: MIT
#include "HistoryDialog.h"

#include "DialogHeader.h"

#include "../core/Diagnostics.h"
#include "../core/Logger.h"
#include "../terminal/TerminalView.h"

#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace BlackCore {
namespace {

QLabel *summaryLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", QStringLiteral("secondary"));
    label->setWordWrap(true);
    return label;
}

QTableWidget *makeTable(const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->horizontalHeader()->setHighlightSections(false);
    return table;
}

QComboBox *makeTimeFilter(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    for (TimeFilter filter : HistoryQuery::filters())
        combo->addItem(HistoryQuery::filterName(filter), HistoryQuery::filterId(filter));
    return combo;
}

TimeFilter filterOf(QComboBox *combo)
{
    return HistoryQuery::filterFromId(combo->currentData().toString());
}

QLabel *summaryLabel(QWidget *parent)
{
    return summaryLabel(QString(), parent);
}

} // namespace

HistoryDialog::HistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("historyDialog"));
    setWindowTitle(tr("История и поиск"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    resize(920, 620);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(new DialogHeader(tr("История и поиск — BlackCore"), this));

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    outer->addLayout(layout, 1);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("historyTabs"));
    m_tabs->addTab(buildCommandsPage(), tr("Команды"));
    m_tabs->addTab(buildTerminalPage(), tr("Вывод терминала"));
    m_tabs->addTab(buildActivityPage(), tr("Действия"));
    m_tabs->addTab(buildLogPage(), tr("Журнал"));
    m_tabs->addTab(buildNotificationsPage(), tr("Уведомления"));
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // Qt не поставляет русских переводов, поэтому подписываем кнопку сами
    if (QPushButton *closeButton = buttons->button(QDialogButtonBox::Close)) {
        closeButton->setText(tr("Закрыть"));
        closeButton->setIcon(QIcon());
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshAll();
}

// ---------------------------------------------------------------------------
// Команды
// ---------------------------------------------------------------------------

QWidget *HistoryDialog::buildCommandsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *searchRow = new QHBoxLayout;
    m_historySearch = new QLineEdit(page);
    m_historySearch->setObjectName(QStringLiteral("historySearch"));
    m_historySearch->setPlaceholderText(tr("Поиск по команде, пометке или каталогу"));
    m_historySearch->setClearButtonEnabled(true);
    m_historyTime = makeTimeFilter(page);
    m_historyTime->setObjectName(QStringLiteral("historyTimeFilter"));
    searchRow->addWidget(m_historySearch, 1);
    searchRow->addWidget(m_historyTime);
    layout->addLayout(searchRow);

    m_historyTable = makeTable({tr("Когда"), tr("Команда"), tr("Источник"), tr("Длительность"),
                                tr("Код")},
                               page);
    m_historyTable->setObjectName(QStringLiteral("historyTable"));
    m_historyTable->horizontalHeader()->setStretchLastSection(false);
    m_historyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_historyTable, 1);

    m_historySummary = summaryLabel(page);
    layout->addWidget(m_historySummary);

    auto *buttons = new QHBoxLayout;
    auto *insertButton = new QPushButton(tr("Вставить в терминал"), page);
    insertButton->setObjectName(QStringLiteral("historyInsert"));
    insertButton->setMinimumWidth(190);
    auto *clearButton = new QPushButton(tr("Очистить историю команд"), page);
    clearButton->setObjectName(QStringLiteral("historyClear"));
    auto *refreshButton = new QPushButton(tr("Обновить"), page);
    buttons->addWidget(insertButton);
    buttons->addWidget(clearButton);
    buttons->addStretch(1);
    buttons->addWidget(refreshButton);
    layout->addLayout(buttons);

    layout->addWidget(summaryLabel(tr("В историю попадают только команды, запущенные через "
                                      "BlackCore (библиотека, палитра, панель разработчика, "
                                      "сценарии). Введённое вручную в терминале не записывается, "
                                      "секреты маскируются."),
                                   page));

    connect(m_historySearch, &QLineEdit::textChanged, this, [this](const QString &) {
        refreshHistory();
    });
    connect(m_historyTime, &QComboBox::currentIndexChanged, this, [this](int) { refreshHistory(); });
    connect(refreshButton, &QPushButton::clicked, this, [this]() { refreshHistory(); });
    connect(insertButton, &QPushButton::clicked, this, [this]() {
        const int row = m_historyTable->currentRow();
        if (row < 0 || row >= m_history.size())
            return;
        emit insertRequested(m_history.at(row).command);
    });
    connect(m_historyTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        const int row = m_historyTable->currentRow();
        if (row < 0 || row >= m_history.size())
            return;
        emit insertRequested(m_history.at(row).command);
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, tr("Очистить историю"),
            tr("Удалить историю выполненных команд (%1 записей)?\n\n"
               "Журнал приложения и история действий не затрагиваются.")
                .arg(CommandHistory::count()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        CommandHistory::clear();
        refreshHistory();
        emit historyCleared();
    });

    return page;
}

void HistoryDialog::setHistoryQuery(const QString &query, TimeFilter filter)
{
    m_historySearch->setText(query);
    const int index = m_historyTime->findData(HistoryQuery::filterId(filter));
    if (index >= 0)
        m_historyTime->setCurrentIndex(index);
    refreshHistory();
}

void HistoryDialog::refreshHistory()
{
    m_history = CommandHistory::search(m_historySearch->text(), filterOf(m_historyTime));
    m_historyTable->setRowCount(m_history.size());
    const QDateTime now = QDateTime::currentDateTime();
    for (int row = 0; row < m_history.size(); ++row) {
        const HistoryEntry &entry = m_history.at(row);
        auto *when = new QTableWidgetItem(HistoryQuery::relativeTime(entry.at, now));
        when->setToolTip(entry.at.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")));
        m_historyTable->setItem(row, 0, when);
        auto *command = new QTableWidgetItem(entry.comment.isEmpty() ? entry.command : entry.comment);
        command->setToolTip(entry.command
                            + (entry.workingDirectory.isEmpty()
                                   ? QString()
                                   : QStringLiteral("\n%1").arg(entry.workingDirectory)));
        m_historyTable->setItem(row, 1, command);
        m_historyTable->setItem(row, 2, new QTableWidgetItem(entry.sourceName()));
        m_historyTable->setItem(row, 3, new QTableWidgetItem(entry.durationText()));
        m_historyTable->setItem(row, 4, new QTableWidgetItem(
                                             entry.exitCode < 0 ? QStringLiteral("—")
                                                                : QString::number(entry.exitCode)));
    }
    m_historyTable->resizeColumnToContents(0);
    m_historyTable->resizeColumnToContents(2);
    m_historyTable->setColumnWidth(3, 110);
    m_historyTable->setColumnWidth(4, 60);
    m_historySummary->setText(tr("Показано %1 из %2 · фильтр: %3")
                                  .arg(m_history.size())
                                  .arg(CommandHistory::count())
                                  .arg(HistoryQuery::filterName(filterOf(m_historyTime))));
}

int HistoryDialog::historyRowCount() const
{
    return m_history.size();
}

// ---------------------------------------------------------------------------
// Вывод терминала
// ---------------------------------------------------------------------------

QWidget *HistoryDialog::buildTerminalPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *searchRow = new QHBoxLayout;
    m_terminalSearch = new QLineEdit(page);
    m_terminalSearch->setObjectName(QStringLiteral("terminalSearch"));
    m_terminalSearch->setPlaceholderText(tr("Что найти в выводе текущего терминала"));
    m_terminalSearch->setClearButtonEnabled(true);
    m_caseSensitive = new QCheckBox(tr("Учитывать регистр"), page);
    m_caseSensitive->setObjectName(QStringLiteral("terminalSearchCase"));
    m_useRegex = new QCheckBox(tr("Регулярное выражение"), page);
    m_useRegex->setObjectName(QStringLiteral("terminalSearchRegex"));
    searchRow->addWidget(m_terminalSearch, 1);
    searchRow->addWidget(m_caseSensitive);
    searchRow->addWidget(m_useRegex);
    layout->addLayout(searchRow);

    m_matchList = new QListWidget(page);
    m_matchList->setObjectName(QStringLiteral("matchList"));
    m_matchList->setAlternatingRowColors(true);
    layout->addWidget(m_matchList, 1);

    m_matchSummary = summaryLabel(page);
    layout->addWidget(m_matchSummary);

    auto *buttons = new QHBoxLayout;
    auto *jumpButton = new QPushButton(tr("Перейти к строке"), page);
    jumpButton->setObjectName(QStringLiteral("matchJump"));
    auto *copyButton = new QPushButton(tr("Скопировать строку"), page);
    buttons->addWidget(jumpButton);
    buttons->addWidget(copyButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    layout->addWidget(summaryLabel(tr("Поиск идёт по всему буферу панели, включая прокрутку. "
                                      "Переход прокручивает терминал к найденной строке."),
                                   page));

    connect(m_terminalSearch, &QLineEdit::textChanged, this, [this](const QString &) {
        refreshMatches();
    });
    connect(m_caseSensitive, &QCheckBox::toggled, this, [this](bool) { refreshMatches(); });
    connect(m_useRegex, &QCheckBox::toggled, this, [this](bool) { refreshMatches(); });
    connect(jumpButton, &QPushButton::clicked, this, [this]() {
        const int row = m_matchList->currentRow();
        if (row < 0 || row >= m_matches.size())
            return;
        emit jumpToLineRequested(m_matches.at(row).line);
    });
    connect(m_matchList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        const int row = m_matchList->currentRow();
        if (row >= 0 && row < m_matches.size())
            emit jumpToLineRequested(m_matches.at(row).line);
    });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        const int row = m_matchList->currentRow();
        if (row < 0 || row >= m_matches.size())
            return;
        QGuiApplication::clipboard()->setText(m_matches.at(row).text);
    });

    return page;
}

void HistoryDialog::setTerminal(TerminalView *terminal)
{
    m_terminal = terminal;
    refreshMatches();
}

QList<TerminalMatch> HistoryDialog::findInText(const QString &text, const QString &needle,
                                               bool caseSensitive, bool useRegex, int limit)
{
    QList<TerminalMatch> matches;
    if (needle.isEmpty() || text.isEmpty())
        return matches;

    QRegularExpression expression;
    const bool regexMode = useRegex;
    if (regexMode) {
        expression.setPattern(needle);
        expression.setPatternOptions(caseSensitive ? QRegularExpression::NoPatternOption
                                                   : QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid())
            return matches;
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int line = 0; line < lines.size(); ++line) {
        const QString &raw = lines.at(line);
        if (raw.isEmpty())
            continue;
        if (regexMode) {
            auto it = expression.globalMatch(raw);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                TerminalMatch entry;
                entry.line = line;
                entry.column = match.capturedStart();
                entry.text = raw;
                matches.append(entry);
                if (matches.size() >= limit)
                    return matches;
                if (match.capturedLength() == 0)
                    break;
            }
        } else {
            const Qt::CaseSensitivity sensitivity =
                caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
            int from = 0;
            while (from >= 0 && from < raw.size()) {
                const int position = raw.indexOf(needle, from, sensitivity);
                if (position < 0)
                    break;
                TerminalMatch entry;
                entry.line = line;
                entry.column = position;
                entry.text = raw;
                matches.append(entry);
                if (matches.size() >= limit)
                    return matches;
                from = position + qMax(1, needle.size());
            }
        }
    }
    return matches;
}

QList<TerminalMatch> HistoryDialog::searchTerminal(const QString &needle, bool caseSensitive,
                                                   bool useRegex, int limit) const
{
    if (!m_terminal || needle.isEmpty())
        return {};
    return findInText(m_terminal->bufferText(), needle, caseSensitive, useRegex, limit);
}

void HistoryDialog::refreshMatches()
{
    m_matchList->clear();
    m_matches.clear();

    const QString needle = m_terminalSearch->text();
    if (!m_terminal || needle.isEmpty()) {
        m_matchSummary->setText(m_terminal ? tr("Введите текст для поиска")
                                           : tr("Нет активного терминала"));
        return;
    }

    const bool regexMode = m_useRegex->isChecked();
    if (regexMode) {
        QRegularExpression expression(needle);
        if (!expression.isValid()) {
            m_matchSummary->setText(tr("Неверное регулярное выражение: %1").arg(expression.errorString()));
            return;
        }
    }

    m_matches = findInText(m_terminal->bufferText(), needle, m_caseSensitive->isChecked(),
                           regexMode);
    for (const TerminalMatch &match : m_matches) {
        auto *item = new QListWidgetItem(QStringLiteral("%1: %2")
                                            .arg(match.line + 1, 6)
                                            .arg(match.text.trimmed()),
                                        m_matchList);
        item->setToolTip(match.text);
    }
    if (m_matchList->count() > 0)
        m_matchList->setCurrentRow(0);

    m_matchSummary->setText(m_matches.isEmpty()
                                ? tr("Ничего не найдено в буфере (%1 строк)").arg(m_terminal->scrollbackCount())
                                : tr("Найдено совпадений: %1").arg(m_matches.size()));
}

int HistoryDialog::selectedMatchLine() const
{
    const int row = m_matchList->currentRow();
    if (row < 0 || row >= m_matches.size())
        return -1;
    return m_matches.at(row).line;
}

// ---------------------------------------------------------------------------
// Действия
// ---------------------------------------------------------------------------

QWidget *HistoryDialog::buildActivityPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *searchRow = new QHBoxLayout;
    m_activitySearch = new QLineEdit(page);
    m_activitySearch->setObjectName(QStringLiteral("activitySearch"));
    m_activitySearch->setPlaceholderText(tr("Поиск по действиям приложения"));
    m_activitySearch->setClearButtonEnabled(true);
    m_activityTime = makeTimeFilter(page);
    searchRow->addWidget(m_activitySearch, 1);
    searchRow->addWidget(m_activityTime);
    layout->addLayout(searchRow);

    m_activityTable = makeTable({tr("Когда"), tr("Действие"), tr("Подробности"), tr("Раздел")}, page);
    m_activityTable->setObjectName(QStringLiteral("activityTable"));
    m_activityTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_activityTable, 1);

    m_activitySummary = summaryLabel(page);
    layout->addWidget(m_activitySummary);

    auto *buttons = new QHBoxLayout;
    auto *clearButton = new QPushButton(tr("Очистить историю действий"), page);
    clearButton->setObjectName(QStringLiteral("activityClear"));
    buttons->addWidget(clearButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(m_activitySearch, &QLineEdit::textChanged, this, [this](const QString &) {
        refreshActivity();
    });
    connect(m_activityTime, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshActivity();
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, tr("Очистить историю действий"),
            tr("Удалить историю действий приложения (%1 записей)?").arg(ActivityLog::count()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        ActivityLog::clear();
        refreshActivity();
        emit activityCleared();
    });

    return page;
}

void HistoryDialog::setActivityQuery(const QString &query, TimeFilter filter)
{
    m_activitySearch->setText(query);
    const int index = m_activityTime->findData(HistoryQuery::filterId(filter));
    if (index >= 0)
        m_activityTime->setCurrentIndex(index);
    refreshActivity();
}

void HistoryDialog::refreshActivity()
{
    m_activity = ActivityLog::search(m_activitySearch->text(), filterOf(m_activityTime));
    m_activityTable->setRowCount(m_activity.size());
    const QDateTime now = QDateTime::currentDateTime();
    for (int row = 0; row < m_activity.size(); ++row) {
        const ActivityEntry &entry = m_activity.at(row);
        m_activityTable->setItem(row, 0, new QTableWidgetItem(HistoryQuery::relativeTime(entry.at, now)));
        m_activityTable->setItem(row, 1, new QTableWidgetItem(entry.action));
        m_activityTable->setItem(row, 2, new QTableWidgetItem(entry.details));
        m_activityTable->setItem(row, 3, new QTableWidgetItem(entry.category));
    }
    m_activityTable->resizeColumnToContents(0);
    m_activityTable->resizeColumnToContents(3);
    m_activitySummary->setText(tr("Показано %1 из %2 · фильтр: %3")
                                   .arg(m_activity.size())
                                   .arg(ActivityLog::count())
                                   .arg(HistoryQuery::filterName(filterOf(m_activityTime))));
}

// ---------------------------------------------------------------------------
// Журнал
// ---------------------------------------------------------------------------

QWidget *HistoryDialog::buildLogPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_logSummary = summaryLabel(page);
    layout->addWidget(m_logSummary);

    m_logView = new QPlainTextEdit(page);
    m_logView->setObjectName(QStringLiteral("logView"));
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(mono.pointSizeF() - 1);
    m_logView->setFont(mono);
    layout->addWidget(m_logView, 1);

    auto *buttons = new QHBoxLayout;
    auto *refreshButton = new QPushButton(tr("Обновить"), page);
    auto *openButton = new QPushButton(tr("Открыть файл"), page);
    auto *insertButton = new QPushButton(tr("tail в терминале"), page);
    buttons->addWidget(refreshButton);
    buttons->addWidget(openButton);
    buttons->addWidget(insertButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    layout->addWidget(summaryLabel(tr("Журнал BlackCore: пароли, токены и приватные ключи "
                                      "маскируются при записи — ни в файл, ни в stderr они не "
                                      "попадают."),
                                   page));

    connect(refreshButton, &QPushButton::clicked, this, [this]() { refreshLog(); });
    connect(openButton, &QPushButton::clicked, this, [this]() {
        const QString path = Diagnostics::logPath();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::information(this, tr("Журнал"), tr("Файл журнала ещё не создан."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(insertButton, &QPushButton::clicked, this, [this]() {
        const QString path = Diagnostics::logPath();
        if (!path.isEmpty())
            emit insertRequested(QStringLiteral("tail -n 100 %1").arg(path));
    });

    return page;
}

QWidget *HistoryDialog::buildNotificationsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_notificationSummary = summaryLabel(page);
    layout->addWidget(m_notificationSummary);

    m_notificationTable = makeTable({tr("Когда"), tr("Вид"), tr("Заголовок"), tr("Текст")}, page);
    m_notificationTable->setObjectName(QStringLiteral("notificationTable"));
    m_notificationTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(m_notificationTable, 1);

    auto *buttons = new QHBoxLayout;
    auto *refreshButton = new QPushButton(tr("Обновить"), page);
    auto *readButton = new QPushButton(tr("Отметить прочитанными"), page);
    auto *clearButton = new QPushButton(tr("Очистить историю уведомлений"), page);
    buttons->addWidget(refreshButton);
    buttons->addWidget(readButton);
    buttons->addWidget(clearButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto *disableButton = new QPushButton(tr("Отключить уведомления полностью"), page);
    disableButton->setObjectName(QStringLiteral("notificationsDisable"));
    auto *enableButton = new QPushButton(tr("Включить уведомления"), page);
    enableButton->setObjectName(QStringLiteral("notificationsEnable"));
    auto *toggleRow = new QHBoxLayout;
    toggleRow->addWidget(enableButton);
    toggleRow->addWidget(disableButton);
    toggleRow->addStretch(1);
    layout->addLayout(toggleRow);

    layout->addWidget(summaryLabel(tr("Уведомления можно отключить полностью или по видам — "
                                      "раздел «Уведомления» в настройках. История хранится "
                                      "локально и очищается по вашему запросу."),
                                   page));

    connect(refreshButton, &QPushButton::clicked, this, [this]() { refreshNotifications(); });
    connect(readButton, &QPushButton::clicked, this, [this]() {
        NotificationCenter::instance().markAllRead();
        refreshNotifications();
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        NotificationCenter::instance().clearHistory();
        refreshNotifications();
        emit notificationsCleared();
    });
    connect(disableButton, &QPushButton::clicked, this, [this]() {
        NotificationSettings settings = NotificationCenter::instance().settings();
        settings.enabled = false;
        NotificationCenter::instance().setSettings(settings);
        refreshNotifications();
    });
    connect(enableButton, &QPushButton::clicked, this, [this]() {
        NotificationSettings settings = NotificationCenter::instance().settings();
        settings.enabled = true;
        NotificationCenter::instance().setSettings(settings);
        refreshNotifications();
    });

    return page;
}

void HistoryDialog::refreshNotifications()
{
    m_notifications = NotificationCenter::instance().history();
    if (m_notificationTable->rowCount() != m_notifications.size())
        m_notificationTable->setRowCount(m_notifications.size());

    const QDateTime now = QDateTime::currentDateTime();
    for (int row = 0; row < m_notifications.size(); ++row) {
        const Notification &notification = m_notifications.at(row);
        m_notificationTable->setItem(row, 0,
                                     new QTableWidgetItem(HistoryQuery::relativeTime(notification.at, now)));
        m_notificationTable->setItem(row, 1, new QTableWidgetItem(notification.kindName()));
        m_notificationTable->setItem(row, 2, new QTableWidgetItem(notification.title));
        m_notificationTable->setItem(row, 3,
                                     new QTableWidgetItem(notification.body.simplified().left(120)));
    }

    const NotificationCenter &center = NotificationCenter::instance();
    const NotificationSettings settings = center.settings();
    m_notificationSummary->setText(
        settings.enabled
            ? tr("Всего: %1 · непрочитанных: %2").arg(center.history().size()).arg(center.unreadCount())
            : tr("Уведомления полностью отключены · в истории: %1").arg(center.history().size()));
}

void HistoryDialog::refreshLog()
{
    const QString path = Diagnostics::logPath();
    if (path.isEmpty()) {
        m_logSummary->setText(tr("Журнал в файл недоступен"));
        m_logView->setPlainText(tr("Запустите BlackCore из терминала: сообщения будут видны в stderr."));
        return;
    }
    m_logSummary->setText(tr("%1 · %2").arg(path, QString::number(Diagnostics::logSize()) + tr(" байт")));
    const QString tail = Diagnostics::tailLog(400);
    m_logView->setPlainText(tail.isEmpty() ? tr("Журнал пуст.") : tail);
    auto *bar = m_logView->verticalScrollBar();
    bar->setValue(bar->maximum());
}

// ---------------------------------------------------------------------------
// Общее
// ---------------------------------------------------------------------------

void HistoryDialog::openSection(int index)
{
    if (!m_tabs)
        return;
    m_tabs->setCurrentIndex(qBound(0, index, m_tabs->count() - 1));
}

int HistoryDialog::currentSection() const
{
    return m_tabs ? m_tabs->currentIndex() : 0;
}

void HistoryDialog::refreshAll()
{
    refreshHistory();
    refreshActivity();
    refreshMatches();
    refreshLog();
    refreshNotifications();
}

} // namespace BlackCore
