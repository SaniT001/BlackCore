// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace BlackCore {

//! Категории библиотеки команд.
enum class CommandCategory {
    System = 0,   //!< система: процессы, питание, нагрузка
    Network,      //!< сеть: ping, ip, curl, ss
    Files,        //!< файлы: поиск, размеры, архивы
    Development,  //!< разработка: php, python, node
    Git,          //!< git
    Ssh,          //!< ssh и ключи
    Maintenance,  //!< обслуживание: пакеты, журналы, очистка
};

//! Сохранённая пользователем команда или сценарий.
struct SavedCommand
{
    QString id;
    QString name;
    QString description;
    CommandCategory category = CommandCategory::System;
    QString command;
    //! Рабочий каталог (пусто — каталог активной панели терминала)
    QString workingDirectory;
    //! Требовать подтверждение перед выполнением (по умолчанию — для изменяющих)
    bool needsConfirmation = true;
    QDateTime createdAt;
    QDateTime lastUsedAt;
    int useCount = 0;

    bool isValid() const { return !name.isEmpty() && !command.isEmpty(); }
    QString categoryName() const;
    QString categoryId() const;

    QJsonObject toJson() const;
    static SavedCommand fromJson(const QJsonObject &object);
};

//! Библиотека пользовательских команд: хранение, поиск, оценка опасности.
/*!
 *  Хранится в настройках (ключ `commands/list`) как JSON-массив, поэтому
 *  попадает в экспорт конфигурации. Пароли в командах не хранятся: если
 *  команда содержит секрет, он маскируется при сохранении в журнал, а сама
 *  команда выполняется только после подтверждения.
 */
class CommandLibrary
{
public:
    static QList<CommandCategory> categories();
    static QString categoryId(CommandCategory category);
    static QString categoryName(CommandCategory category);
    static CommandCategory categoryFromId(const QString &id);
    static CommandCategory categoryFromName(const QString &name);

    static QList<SavedCommand> commands();
    static SavedCommand command(const QString &id);
    static bool saveCommand(SavedCommand &command);
    static bool removeCommand(const QString &id);
    static QString makeId(const QString &name);

    //! Команды выбранной категории (или все при invalid)
    static QList<SavedCommand> byCategory(CommandCategory category);

    //! Поиск по названию, описанию, категории и тексту команды
    static QList<SavedCommand> search(const QString &query);

    //! Отметить выполнение (дата и счётчик использования)
    static void markUsed(const QString &id);

    //! Проверка перед сохранением: пустое название/команда, каталог не существует
    static QString validate(const SavedCommand &command);

    //! Команда может изменить систему или удалить данные
    static bool isDestructive(const QString &command);

    //! Нужно ли спрашивать подтверждение (читающие команды запускаются сразу)
    static bool needsConfirmation(const SavedCommand &command);

    //! Копия команды с подставленным рабочим каталогом (для вставки в терминал)
    static QString buildInvocation(const SavedCommand &command);

    //! Пустой список (используется при сбросе настроек)
    static void clear();
};

} // namespace BlackCore
