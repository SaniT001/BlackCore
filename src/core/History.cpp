// SPDX-License-Identifier: MIT
#include "History.h"

#include "Secrets.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>

namespace BlackCore {
namespace {

constexpr const char *kCommandsKey = "history/commands";
constexpr const char *kActivityKey = "history/activity";
constexpr int kMaxCommands = 500;
constexpr int kMaxActivity = 500;

struct SourceSpec
{
    CommandSource source;
    const char *id;
    const char *name;
};

const SourceSpec kSources[] = {
    {CommandSource::Typed, "typed", "терминал"},
    {CommandSource::Library, "library", "библиотека"},
    {CommandSource::Palette, "palette", "палитра"},
    {CommandSource::Project, "project", "проект"},
    {CommandSource::Git, "git", "Git"},
    {CommandSource::DevPanel, "devpanel", "панель разработчика"},
    {CommandSource::Script, "script", "сценарий"},
};

QString newId(const QString &prefix, const QString &text)
{
    const QByteArray probe = (prefix + text + QString::number(QDateTime::currentMSecsSinceEpoch()))
                                 .toUtf8();
    return prefix + QString::fromLatin1(
                        QCryptographicHash::hash(probe, QCryptographicHash::Sha1).toHex().left(10));
}

QString sourceId(CommandSource source)
{
    for (const SourceSpec &spec : kSources) {
        if (spec.source == source)
            return QString::fromLatin1(spec.id);
    }
    return QStringLiteral("library");
}

CommandSource sourceFromId(const QString &id)
{
    for (const SourceSpec &spec : kSources) {
        if (id == QLatin1String(spec.id))
            return spec.source;
    }
    return CommandSource::Library;
}

QList<HistoryEntry> readCommands()
{
    QSettings store;
    const QJsonDocument document =
        QJsonDocument::fromJson(store.value(QLatin1String(kCommandsKey)).toString().toUtf8());
    QList<HistoryEntry> list;
    if (!document.isArray())
        return list;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        HistoryEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.command = object.value(QStringLiteral("command")).toString();
        entry.workingDirectory = object.value(QStringLiteral("dir")).toString();
        entry.source = sourceFromId(object.value(QStringLiteral("source")).toString());
        entry.at = QDateTime::fromString(object.value(QStringLiteral("at")).toString(), Qt::ISODate);
        entry.durationMs = object.value(QStringLiteral("duration")).toInt();
        entry.exitCode = object.value(QStringLiteral("exit")).toInt(-1);
        entry.comment = object.value(QStringLiteral("comment")).toString();
        if (entry.isValid())
            list.append(entry);
    }
    return list;
}

void writeCommands(const QList<HistoryEntry> &entries)
{
    QJsonArray array;
    for (const HistoryEntry &entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), entry.id);
        object.insert(QStringLiteral("command"), entry.command);
        object.insert(QStringLiteral("dir"), entry.workingDirectory);
        object.insert(QStringLiteral("source"), sourceId(entry.source));
        object.insert(QStringLiteral("at"), entry.at.toString(Qt::ISODate));
        object.insert(QStringLiteral("duration"), entry.durationMs);
        object.insert(QStringLiteral("exit"), entry.exitCode);
        object.insert(QStringLiteral("comment"), entry.comment);
        array.append(object);
    }
    QSettings store;
    store.setValue(QLatin1String(kCommandsKey),
                   QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    store.sync();
}

QList<ActivityEntry> readActivity()
{
    QSettings store;
    const QJsonDocument document =
        QJsonDocument::fromJson(store.value(QLatin1String(kActivityKey)).toString().toUtf8());
    QList<ActivityEntry> list;
    if (!document.isArray())
        return list;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        ActivityEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.action = object.value(QStringLiteral("action")).toString();
        entry.details = object.value(QStringLiteral("details")).toString();
        entry.category = object.value(QStringLiteral("category")).toString();
        entry.at = QDateTime::fromString(object.value(QStringLiteral("at")).toString(), Qt::ISODate);
        if (entry.isValid())
            list.append(entry);
    }
    return list;
}

void writeActivity(const QList<ActivityEntry> &entries)
{
    QJsonArray array;
    for (const ActivityEntry &entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), entry.id);
        object.insert(QStringLiteral("action"), entry.action);
        object.insert(QStringLiteral("details"), Secrets::redact(entry.details));
        object.insert(QStringLiteral("category"), entry.category);
        object.insert(QStringLiteral("at"), entry.at.toString(Qt::ISODate));
        array.append(object);
    }
    QSettings store;
    store.setValue(QLatin1String(kActivityKey),
                   QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    store.sync();
}

QString sourceNameFor(CommandSource source)
{
    for (const SourceSpec &spec : kSources) {
        if (spec.source == source)
            return QString::fromUtf8(spec.name);
    }
    return QString();
}

} // namespace

// ---------------------------------------------------------------------------
// HistoryEntry
// ---------------------------------------------------------------------------

QString HistoryEntry::sourceName() const
{
    return sourceNameFor(source);
}

QString HistoryEntry::durationText() const
{
    if (durationMs <= 0)
        return QStringLiteral("—");
    if (durationMs < 1000)
        return QStringLiteral("%1 мс").arg(durationMs);
    const double seconds = double(durationMs) / 1000.0;
    if (seconds < 60.0) {
        QString text = QString::number(seconds, 'f', 1);
        text.replace(QLatin1Char('.'), QLatin1Char(','));
        return QStringLiteral("%1 с").arg(text);
    }
    const int minutes = int(seconds) / 60;
    const int rest = int(seconds) % 60;
    return QStringLiteral("%1 мин %2 с").arg(minutes).arg(rest);
}

// ---------------------------------------------------------------------------
// HistoryQuery
// ---------------------------------------------------------------------------

QList<TimeFilter> HistoryQuery::filters()
{
    return {TimeFilter::Any, TimeFilter::Hour, TimeFilter::Today,
            TimeFilter::Day, TimeFilter::Week, TimeFilter::Month};
}

QString HistoryQuery::filterId(TimeFilter filter)
{
    switch (filter) {
    case TimeFilter::Any: return QStringLiteral("any");
    case TimeFilter::Hour: return QStringLiteral("hour");
    case TimeFilter::Today: return QStringLiteral("today");
    case TimeFilter::Day: return QStringLiteral("day");
    case TimeFilter::Week: return QStringLiteral("week");
    case TimeFilter::Month: return QStringLiteral("month");
    }
    return QStringLiteral("any");
}

QString HistoryQuery::filterName(TimeFilter filter)
{
    switch (filter) {
    case TimeFilter::Any: return QStringLiteral("всё время");
    case TimeFilter::Hour: return QStringLiteral("последний час");
    case TimeFilter::Today: return QStringLiteral("сегодня");
    case TimeFilter::Day: return QStringLiteral("последние 24 часа");
    case TimeFilter::Week: return QStringLiteral("последние 7 дней");
    case TimeFilter::Month: return QStringLiteral("последние 30 дней");
    }
    return QString();
}

TimeFilter HistoryQuery::filterFromId(const QString &id)
{
    for (TimeFilter filter : filters()) {
        if (filterId(filter) == id)
            return filter;
    }
    return TimeFilter::Any;
}

bool HistoryQuery::matches(const QDateTime &moment, TimeFilter filter, const QDateTime &now)
{
    if (!moment.isValid())
        return filter == TimeFilter::Any;
    switch (filter) {
    case TimeFilter::Any:
        return true;
    case TimeFilter::Hour:
        return moment.secsTo(now) <= 3600 && moment <= now.addSecs(60);
    case TimeFilter::Today:
        return moment.date() == now.date();
    case TimeFilter::Day:
        return moment.secsTo(now) <= 24 * 3600 && moment <= now.addSecs(60);
    case TimeFilter::Week:
        return moment.secsTo(now) <= 7 * 24 * 3600 && moment <= now.addSecs(60);
    case TimeFilter::Month:
        return moment.secsTo(now) <= 30 * 24 * 3600 && moment <= now.addSecs(60);
    }
    return true;
}

QString HistoryQuery::relativeTime(const QDateTime &moment, const QDateTime &now)
{
    if (!moment.isValid())
        return QStringLiteral("—");
    const qint64 seconds = moment.secsTo(now);
    if (seconds < 60 && seconds >= 0)
        return QStringLiteral("только что");
    if (seconds < 3600 && seconds >= 0)
        return QStringLiteral("%1 мин назад").arg(seconds / 60);
    if (moment.date() == now.date())
        return QStringLiteral("сегодня, %1").arg(moment.toString(QStringLiteral("HH:mm")));
    if (moment.date() == now.date().addDays(-1))
        return QStringLiteral("вчера, %1").arg(moment.toString(QStringLiteral("HH:mm")));
    if (moment.date().year() == now.date().year())
        return moment.toString(QStringLiteral("dd.MM, HH:mm"));
    return moment.toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

// ---------------------------------------------------------------------------
// CommandHistory
// ---------------------------------------------------------------------------

int CommandHistory::maxEntries()
{
    return kMaxCommands;
}

QList<HistoryEntry> CommandHistory::entries()
{
    return readCommands();
}

QString CommandHistory::record(const QString &command, CommandSource source,
                              const QString &workingDirectory, const QString &comment)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
        return QString();

    // В историю пишется уже очищенная команда: секреты не сохраняются
    HistoryEntry entry;
    entry.command = Secrets::redact(trimmed);
    entry.workingDirectory = workingDirectory;
    entry.source = source;
    entry.comment = comment;
    entry.at = QDateTime::currentDateTime();
    entry.id = newId(QStringLiteral("h"), entry.command + entry.at.toString(Qt::ISODateWithMs));

    QList<HistoryEntry> list = readCommands();
    list.prepend(entry);
    while (list.size() > kMaxCommands)
        list.removeLast();
    writeCommands(list);
    return entry.id;
}

bool CommandHistory::updateResult(const QString &id, int durationMs, int exitCode)
{
    if (id.isEmpty())
        return false;
    QList<HistoryEntry> list = readCommands();
    for (HistoryEntry &entry : list) {
        if (entry.id != id)
            continue;
        entry.durationMs = qMax(0, durationMs);
        entry.exitCode = exitCode;
        writeCommands(list);
        return true;
    }
    return false;
}

HistoryEntry CommandHistory::entry(const QString &id)
{
    for (const HistoryEntry &entry : readCommands()) {
        if (entry.id == id)
            return entry;
    }
    return HistoryEntry();
}

QList<HistoryEntry> CommandHistory::search(const QString &query, TimeFilter filter, int limit)
{
    const QString needle = query.trimmed().toCaseFolded();
    const QDateTime now = QDateTime::currentDateTime();
    QList<HistoryEntry> result;
    for (const HistoryEntry &entry : readCommands()) {
        if (!HistoryQuery::matches(entry.at, filter, now))
            continue;
        if (!needle.isEmpty()) {
            const QString haystack = (entry.command + QLatin1Char(' ') + entry.comment + QLatin1Char(' ')
                                      + entry.sourceName() + QLatin1Char(' ') + entry.workingDirectory)
                                         .toCaseFolded();
            bool all = true;
            for (const QString &word : needle.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (!haystack.contains(word))
                    all = false;
            }
            if (!all)
                continue;
        }
        result.append(entry);
        if (result.size() >= limit)
            break;
    }
    return result;
}

bool CommandHistory::remove(const QString &id)
{
    QList<HistoryEntry> list = readCommands();
    const int before = list.size();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).id == id)
            list.removeAt(i);
    }
    if (list.size() == before)
        return false;
    writeCommands(list);
    return true;
}

void CommandHistory::clear()
{
    QSettings store;
    store.remove(QLatin1String(kCommandsKey));
    store.sync();
}

int CommandHistory::count()
{
    return readCommands().size();
}

// ---------------------------------------------------------------------------
// ActivityLog
// ---------------------------------------------------------------------------

int ActivityLog::maxEntries()
{
    return kMaxActivity;
}

QList<ActivityEntry> ActivityLog::entries()
{
    return readActivity();
}

QString ActivityLog::record(const QString &action, const QString &details, const QString &category)
{
    if (action.trimmed().isEmpty())
        return QString();

    ActivityEntry entry;
    entry.action = action.trimmed();
    entry.details = details;
    entry.category = category;
    entry.at = QDateTime::currentDateTime();
    entry.id = newId(QStringLiteral("a"), entry.action + entry.at.toString(Qt::ISODateWithMs));

    QList<ActivityEntry> list = readActivity();
    list.prepend(entry);
    while (list.size() > kMaxActivity)
        list.removeLast();
    writeActivity(list);
    return entry.id;
}

QList<ActivityEntry> ActivityLog::search(const QString &query, TimeFilter filter, int limit)
{
    const QString needle = query.trimmed().toCaseFolded();
    const QDateTime now = QDateTime::currentDateTime();
    QList<ActivityEntry> result;
    for (const ActivityEntry &entry : readActivity()) {
        if (!HistoryQuery::matches(entry.at, filter, now))
            continue;
        if (!needle.isEmpty()) {
            const QString haystack = (entry.action + QLatin1Char(' ') + entry.details
                                      + QLatin1Char(' ') + entry.category)
                                         .toCaseFolded();
            if (!haystack.contains(needle))
                continue;
        }
        result.append(entry);
        if (result.size() >= limit)
            break;
    }
    return result;
}

void ActivityLog::clear()
{
    QSettings store;
    store.remove(QLatin1String(kActivityKey));
    store.sync();
}

int ActivityLog::count()
{
    return readActivity().size();
}

} // namespace BlackCore
