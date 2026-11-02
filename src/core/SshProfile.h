// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace BlackCore {

//! Профиль SSH-подключения.
/*!
 *  Пароли и приватные ключи **не хранятся**: профиль содержит только адрес,
 *  порт, имя пользователя и путь к ключу. Само подключение выполняет системный
 *  клиент `ssh`, который сам работает с ssh-agent и ~/.ssh/config.
 */
struct SshProfile
{
    QString id;
    QString name;              //!< имя подключения («Рабочий сервер»)
    QString host;              //!< адрес сервера (DNS-имя или IP)
    int port = 22;             //!< порт 1…65535
    QString user;              //!< имя пользователя
    QString identityFile;      //!< путь к ключу; пусто — по умолчанию из ~/.ssh/config

    bool isValid() const { return !name.trimmed().isEmpty() && !host.trimmed().isEmpty(); }

    QJsonObject toJson() const;
    static SshProfile fromJson(const QJsonObject &object);
};

//! Хранилище SSH-профилей и безопасная сборка команды подключения.
class SshManager
{
public:
    //! Все профили пользователя
    static QList<SshProfile> profiles();
    static SshProfile profile(const QString &id);
    static void saveProfile(const SshProfile &profile);
    static void removeProfile(const QString &id);
    static QString makeId(const QString &name);

    //! Проверка адреса сервера: буквы, цифры, точка, дефис, двоеточие
    static bool isValidHost(const QString &host);
    //! Проверка имени пользователя
    static bool isValidUser(const QString &user);
    static bool isValidPort(int port);

    //! Ошибка в профиле (пустая строка — профиль корректен)
    static QString validate(const SshProfile &profile);

    //! Приватные ключи в ~/.ssh (только имена файлов — содержимое не читается)
    static QStringList availableKeys();

    //! Команда подключения для системного ssh; пустая строка при ошибке
    static QString buildCommand(const SshProfile &profile, QString *error = nullptr);

    //! Аргументы ssh (для тестов и журнала)
    static QStringList buildArguments(const SshProfile &profile, QString *error = nullptr);

    //! Путь к системному клиенту ssh
    static QString sshExecutable();
};

} // namespace BlackCore
