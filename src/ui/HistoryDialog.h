// SPDX-License-Identifier: MIT
#pragma once

#include "../core/History.h"
#include "../core/Notifications.h"

#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace BlackCore {

class TerminalView;

//! Совпадение при поиске по выводу терминала.
struct TerminalMatch
{
    int line = 0;     //!< абсолютный номер строки в буфере (с учётом прокрутки)
    int column = 0;
    QString text;

    bool isValid() const { return !text.isEmpty(); }
};

//! История и поиск: команды, вывод терминала, действия, журнал.
/*!
 *  Диалог ничего не меняет в системе: «вставить» только помещает текст в
 *  приглашение терминала, очистка истории выполняется по явному запросу
 *  пользователя и не затрагивает журнал приложения.
 */
class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(QWidget *parent = nullptr);

    //! Терминал, по выводу которого идёт поиск (может быть nullptr)
    void setTerminal(TerminalView *terminal);

    void openSection(int index);
    int currentSection() const;

    // --- история команд ---
    void setHistoryQuery(const QString &query, TimeFilter filter = TimeFilter::Any);
    QList<HistoryEntry> visibleHistory() const { return m_history; }
    int historyRowCount() const;

    // --- история действий ---
    void setActivityQuery(const QString &query, TimeFilter filter = TimeFilter::Any);
    QList<ActivityEntry> visibleActivity() const { return m_activity; }

    // --- поиск по выводу ---
    //! Выполнить поиск по буферу терминала
    QList<TerminalMatch> searchTerminal(const QString &needle, bool caseSensitive = false,
                                        bool useRegex = false, int limit = 300) const;
    QList<TerminalMatch> visibleMatches() const { return m_matches; }
    int selectedMatchLine() const;

    //! Поиск по тексту (статический, используется в тестах)
    static QList<TerminalMatch> findInText(const QString &text, const QString &needle,
                                          bool caseSensitive = false, bool useRegex = false,
                                          int limit = 300);

    // --- уведомления ---
    void refreshNotifications();
    QList<Notification> visibleNotifications() const { return m_notifications; }

    void refreshAll();

signals:
    //! Вставить команду в приглашение активного терминала
    void insertRequested(const QString &command);
    //! Перейти к строке в буфере терминала
    void jumpToLineRequested(int line);
    //! История команд очищена пользователем
    void historyCleared();
    //! История действий очищена пользователем
    void activityCleared();
    //! История уведомлений очищена
    void notificationsCleared();

private:
    QWidget *buildCommandsPage();
    QWidget *buildTerminalPage();
    QWidget *buildActivityPage();
    QWidget *buildLogPage();
    QWidget *buildNotificationsPage();

    void refreshHistory();
    void refreshActivity();
    void refreshMatches();
    void refreshLog();

    QTabWidget *m_tabs = nullptr;
    TerminalView *m_terminal = nullptr;

    // команды
    QLineEdit *m_historySearch = nullptr;
    QComboBox *m_historyTime = nullptr;
    QTableWidget *m_historyTable = nullptr;
    QLabel *m_historySummary = nullptr;
    QList<HistoryEntry> m_history;

    // вывод терминала
    QLineEdit *m_terminalSearch = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_useRegex = nullptr;
    QListWidget *m_matchList = nullptr;
    QLabel *m_matchSummary = nullptr;
    QList<TerminalMatch> m_matches;

    // действия
    QLineEdit *m_activitySearch = nullptr;
    QComboBox *m_activityTime = nullptr;
    QTableWidget *m_activityTable = nullptr;
    QLabel *m_activitySummary = nullptr;
    QList<ActivityEntry> m_activity;

    // журнал
    QPlainTextEdit *m_logView = nullptr;
    QLabel *m_logSummary = nullptr;

    // уведомления
    QTableWidget *m_notificationTable = nullptr;
    QLabel *m_notificationSummary = nullptr;
    QList<Notification> m_notifications;
};

} // namespace BlackCore
