// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace BlackCore {

//! Фильтр по времени для истории и поиска.
enum class TimeFilter {
    Any = 0,   //!< всё время
    Hour,      //!< последний час
    Today,     //!< сегодня
    Day,       //!< последние 24 часа
    Week,      //!< последние 7 дней
    Month,     //!< последние 30 дней
};

//! Откуда пришла команда.
enum class CommandSource {
    Typed = 0,     //!< введена в терминале (в историю BlackCore не пишется)
    Library,       //!< из библиотеки команд
    Palette,       //!< из командной палитры
    Project,       //!< стартовая команда проекта
    Git,           //!< быстрая команда Git
    DevPanel,      //!< панель разработчика
    Script,        //!< сохранённый сценарий
};

//! Запись истории выполненных BlackCore команд.
struct HistoryEntry
{
    QString id;
    QString command;
    QString workingDirectory;
    CommandSource source = CommandSource::Library;
    QDateTime at;
    int durationMs = 0;
    int exitCode = -1;      //!< -1 — неизвестен
    QString comment;        //!< название команды или пометка

    QString sourceName() const;
    QString durationText() const;
    bool isValid() const { return !command.isEmpty() && at.isValid(); }
};

//! Запись истории действий приложения (навигация, настройки, подключения).
struct ActivityEntry
{
    QString id;
    QString action;    //!< короткое действие: «Открыта вкладка», «Применена тема»
    QString details;   //!< подробности (путь, имя профиля)
    QString category;  //!< «вкладки», «настройки», «SSH», «проекты», «монитор»
    QDateTime at;

    bool isValid() const { return !action.isEmpty() && at.isValid(); }
};

//! Общие функции фильтра по времени и «человеческое» время.
class HistoryQuery
{
public:
    static QList<TimeFilter> filters();
    static QString filterId(TimeFilter filter);
    static QString filterName(TimeFilter filter);
    static TimeFilter filterFromId(const QString &id);

    //! Попадает ли момент времени в фильтр (now — текущее время)
    static bool matches(const QDateTime &moment, TimeFilter filter,
                        const QDateTime &now = QDateTime::currentDateTime());

    //! «5 минут назад», «вчера, 14:03», «12.03.2026 14:03»
    static QString relativeTime(const QDateTime &moment,
                               const QDateTime &now = QDateTime::currentDateTime());
};

//! История команд, выполненных через BlackCore (не ввод пользователя в оболочке).
/*!
 *  В историю не попадают строки, введённые в терминале вручную, и любые
 *  секреты: команда сохраняется уже очищенной (Secrets::redact).
 */
class CommandHistory
{
public:
    static int maxEntries();

    static QList<HistoryEntry> entries();

    //! Записать выполнение команды и вернуть её идентификатор
    static QString record(const QString &command, CommandSource source,
                          const QString &workingDirectory = QString(),
                          const QString &comment = QString());

    //! Дополнить запись результатом (длительность и код возврата)
    static bool updateResult(const QString &id, int durationMs, int exitCode);

    static HistoryEntry entry(const QString &id);

    //! Поиск по тексту команды и пометке с фильтром по времени
    static QList<HistoryEntry> search(const QString &query, TimeFilter filter = TimeFilter::Any,
                                      int limit = 500);

    static bool remove(const QString &id);
    static void clear();
    static int count();
};

//! История действий приложения: что и когда делал пользователь в интерфейсе.
class ActivityLog
{
public:
    static int maxEntries();

    static QList<ActivityEntry> entries();

    static QString record(const QString &action, const QString &details = QString(),
                          const QString &category = QString());

    static QList<ActivityEntry> search(const QString &query, TimeFilter filter = TimeFilter::Any,
                                       int limit = 500);

    static void clear();
    static int count();
};

} // namespace BlackCore
