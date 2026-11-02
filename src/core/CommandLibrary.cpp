// SPDX-License-Identifier: MIT
#include "CommandLibrary.h"

#include "Secrets.h"
#include "Workspace.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>

namespace BlackCore {
namespace {

constexpr const char *kStorageKey = "commands/list";
constexpr int kMaxCommands = 200;

struct CategorySpec
{
    CommandCategory category;
    const char *id;
    const char *name;
};

const CategorySpec kCategories[] = {
    {CommandCategory::System, "system", "система"},
    {CommandCategory::Network, "network", "сеть"},
    {CommandCategory::Files, "files", "файлы"},
    {CommandCategory::Development, "development", "разработка"},
    {CommandCategory::Git, "git", "Git"},
    {CommandCategory::Ssh, "ssh", "SSH"},
    {CommandCategory::Maintenance, "maintenance", "обслуживание"},
};

QList<SavedCommand> readAll()
{
    QSettings store;
    const QString json = store.value(QLatin1String(kStorageKey)).toString();
    if (json.isEmpty())
        return {};

    QList<SavedCommand> list;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isArray())
        return {};
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        const SavedCommand command = SavedCommand::fromJson(value.toObject());
        if (!command.id.isEmpty() && command.isValid())
            list.append(command);
    }
    return list;
}

void writeAll(const QList<SavedCommand> &commands)
{
    QJsonArray array;
    for (const SavedCommand &command : commands)
        array.append(command.toJson());
    QSettings store;
    store.setValue(QLatin1String(kStorageKey),
                   QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    store.sync();
}

//! Признаки команд, которые могут изменить систему или удалить данные
struct DangerPattern
{
    const char *pattern;
    const char *reason;
};

const DangerPattern kDanger[] = {
    {R"(^\s*sudo\b)", "запуск с правами суперпользователя"},
    {R"(\brm\s+(-[a-zA-Z]*[rf][a-zA-Z]*\s+)+)", "удаление файлов"},
    {R"(\brm\s+-[a-zA-Z]*r)", "рекурсивное удаление"},
    {R"(\bshred\b)", "безвозвратное удаление"},
    {R"(\bdd\b)", "запись на устройство"},
    {R"(\bmkfs(\.\w+)?\b)", "форматирование"},
    {R"(\bfdisk\b|\bparted\b)", "разметка диска"},
    {R"(\btruncate\b)", "усечение файлов"},
    {R"(>\s*/dev/(sd|nvme|hd))", "запись на устройство"},
    {R"(\bmv\s+.*\s+/dev/null)", "уничтожение данных"},
    {R"(\bchmod\s+(-[a-zA-Z]+\s+)*[0-7]{3,4})", "изменение прав"},
    {R"(\bchown\b)", "изменение владельца"},
    {R"(\b(apt|apt-get|dnf|yum|pacman|zypper)\s+(install|remove|purge|upgrade|dist-upgrade|autoremove|update|-S))", "установка или удаление пакетов"},
    {R"(\bsystemctl\s+(restart|stop|disable|enable|mask|start))", "управление службами"},
    {R"(\bkill\s+-9\b|\bkillall\b|\bpkill\b)", "завершение процессов"},
    {R"(\b(reboot|shutdown|poweroff|halt|init\s+0)\b)", "перезагрузка или выключение"},
    {R"(\bgit\s+(reset\s+--hard|clean\s+-[a-zA-Z]*f|push\s+.*--force|push\s+-f))", "перезапись истории Git"},
    {R"(\bgit\s+checkout\s+--\s+\.)", "откат изменений Git"},
    {R"(\b(DROP|TRUNCATE|DELETE\s+FROM)\b)", "изменение базы данных"},
    {R"(\bcurl\b[^|]*\|\s*(sudo\s+)?(ba)?sh)", "запуск скачанного скрипта"},
    {R"(\bwget\b[^|]*\|\s*(sudo\s+)?(ba)?sh)", "запуск скачанного скрипта"},
    {R"(:\s*\(\s*\)\s*\{)", "самовоспроизводящаяся команда"},
    {R"(\bcrontab\s+-r\b)", "удаление заданий cron"},
    {R"(\buserdel\b|\bgroupdel\b|\bpasswd\b)", "управление пользователями"},
};

QString normalize(const QString &text)
{
    return text.toCaseFolded().simplified();
}

} // namespace

// ---------------------------------------------------------------------------
// SavedCommand
// ---------------------------------------------------------------------------

QString SavedCommand::categoryId() const
{
    return CommandLibrary::categoryId(category);
}

QString SavedCommand::categoryName() const
{
    return CommandLibrary::categoryName(category);
}

QJsonObject SavedCommand::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("category"), categoryId());
    object.insert(QStringLiteral("command"), command);
    object.insert(QStringLiteral("workingDirectory"), workingDirectory);
    object.insert(QStringLiteral("needsConfirmation"), needsConfirmation);
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("lastUsedAt"), lastUsedAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("useCount"), useCount);
    return object;
}

SavedCommand SavedCommand::fromJson(const QJsonObject &object)
{
    SavedCommand command;
    command.id = object.value(QStringLiteral("id")).toString();
    command.name = object.value(QStringLiteral("name")).toString();
    command.description = object.value(QStringLiteral("description")).toString();
    command.category = CommandLibrary::categoryFromId(object.value(QStringLiteral("category")).toString());
    command.command = object.value(QStringLiteral("command")).toString();
    command.workingDirectory = object.value(QStringLiteral("workingDirectory")).toString();
    command.needsConfirmation = object.value(QStringLiteral("needsConfirmation")).toBool(true);
    command.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    command.lastUsedAt = QDateTime::fromString(object.value(QStringLiteral("lastUsedAt")).toString(), Qt::ISODate);
    command.useCount = object.value(QStringLiteral("useCount")).toInt();
    return command;
}

// ---------------------------------------------------------------------------
// CommandLibrary
// ---------------------------------------------------------------------------

QList<CommandCategory> CommandLibrary::categories()
{
    QList<CommandCategory> list;
    for (const CategorySpec &spec : kCategories)
        list.append(spec.category);
    return list;
}

QString CommandLibrary::categoryId(CommandCategory category)
{
    for (const CategorySpec &spec : kCategories) {
        if (spec.category == category)
            return QString::fromLatin1(spec.id);
    }
    return QStringLiteral("system");
}

QString CommandLibrary::categoryName(CommandCategory category)
{
    for (const CategorySpec &spec : kCategories) {
        if (spec.category == category)
            return QString::fromUtf8(spec.name);
    }
    return QStringLiteral("система");
}

CommandCategory CommandLibrary::categoryFromId(const QString &id)
{
    const QString normalized = id.trimmed().toLower();
    for (const CategorySpec &spec : kCategories) {
        if (normalized == QLatin1String(spec.id))
            return spec.category;
    }
    return categoryFromName(id);
}

CommandCategory CommandLibrary::categoryFromName(const QString &name)
{
    const QString normalized = normalize(name);
    for (const CategorySpec &spec : kCategories) {
        if (normalized == QString::fromUtf8(spec.name).toLower())
            return spec.category;
    }
    return CommandCategory::System;
}

QList<SavedCommand> CommandLibrary::commands()
{
    return readAll();
}

SavedCommand CommandLibrary::command(const QString &id)
{
    for (const SavedCommand &command : readAll()) {
        if (command.id == id)
            return command;
    }
    return SavedCommand();
}

bool CommandLibrary::saveCommand(SavedCommand &command)
{
    QList<SavedCommand> all = readAll();
    if (command.id.isEmpty())
        command.id = makeId(command.name);
    if (command.createdAt.isNull())
        command.createdAt = QDateTime::currentDateTime();

    bool replaced = false;
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id == command.id) {
            all[i] = command;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        if (all.size() >= kMaxCommands)
            return false;
        all.append(command);
    }
    writeAll(all);
    return true;
}

bool CommandLibrary::removeCommand(const QString &id)
{
    QList<SavedCommand> all = readAll();
    const int before = all.size();
    for (int i = all.size() - 1; i >= 0; --i) {
        if (all.at(i).id == id)
            all.removeAt(i);
    }
    if (all.size() == before)
        return false;
    writeAll(all);
    return true;
}

QString CommandLibrary::makeId(const QString &name)
{
    QString base = name.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9а-я]+")), QStringLiteral("-"));
    base = base.trimmed();
    while (base.startsWith(QLatin1Char('-')))
        base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    if (base.isEmpty())
        base = QStringLiteral("command");

    const QByteArray probe = base.toUtf8() + QByteArray::number(QDateTime::currentMSecsSinceEpoch());
    const QString suffix = QString::fromLatin1(
        QCryptographicHash::hash(probe, QCryptographicHash::Sha1).toHex().left(4));
    return base.left(32) + QLatin1Char('-') + suffix;
}

QList<SavedCommand> CommandLibrary::byCategory(CommandCategory category)
{
    QList<SavedCommand> list;
    for (const SavedCommand &command : readAll()) {
        if (command.category == category)
            list.append(command);
    }
    return list;
}

QList<SavedCommand> CommandLibrary::search(const QString &query)
{
    const QString needle = normalize(query);
    const QList<SavedCommand> all = readAll();
    if (needle.isEmpty())
        return all;

    QList<SavedCommand> exact;
    QList<SavedCommand> partial;
    for (const SavedCommand &command : all) {
        const QString haystack = normalize(QStringLiteral("%1 %2 %3 %4")
                                               .arg(command.name, command.description,
                                                    command.categoryName(), command.command));
        if (haystack.contains(needle))
            exact.append(command);
        else {
            // Совпадение по началу слов тоже считаем полезным
            bool allWords = true;
            for (const QString &word : needle.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (!haystack.contains(word))
                    allWords = false;
            }
            if (allWords)
                partial.append(command);
        }
    }
    return exact + partial;
}

void CommandLibrary::markUsed(const QString &id)
{
    QList<SavedCommand> all = readAll();
    for (SavedCommand &command : all) {
        if (command.id != id)
            continue;
        command.lastUsedAt = QDateTime::currentDateTime();
        ++command.useCount;
        writeAll(all);
        return;
    }
}

QString CommandLibrary::validate(const SavedCommand &command)
{
    if (command.name.trimmed().isEmpty())
        return QStringLiteral("Укажите название команды.");
    if (command.command.trimmed().isEmpty())
        return QStringLiteral("Укажите команду или сценарий.");
    if (!command.workingDirectory.isEmpty() && !QFileInfo(command.workingDirectory).isDir())
        return QStringLiteral("Рабочий каталог не существует: %1").arg(command.workingDirectory);
    if (command.name.size() > 120)
        return QStringLiteral("Слишком длинное название.");
    if (command.command.size() > 4000)
        return QStringLiteral("Слишком длинный сценарий.");
    return QString();
}

bool CommandLibrary::isDestructive(const QString &command)
{
    const QString text = command.trimmed();
    if (text.isEmpty())
        return false;
    for (const DangerPattern &pattern : kDanger) {
        if (QRegularExpression(QString::fromLatin1(pattern.pattern),
                              QRegularExpression::CaseInsensitiveOption)
                .match(text)
                .hasMatch())
            return true;
    }
    return false;
}

bool CommandLibrary::needsConfirmation(const SavedCommand &command)
{
    if (command.needsConfirmation)
        return true;
    // Даже если пользователь снял флаг, изменяющие команды спрашивают подтверждение
    return !CommandPolicy::isSafe(command.command) || isDestructive(command.command);
}

QString CommandLibrary::buildInvocation(const SavedCommand &command)
{
    return command.command.trimmed();
}

void CommandLibrary::clear()
{
    QSettings store;
    store.remove(QLatin1String(kStorageKey));
    store.sync();
}

} // namespace BlackCore
