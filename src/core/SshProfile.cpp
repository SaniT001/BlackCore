// SPDX-License-Identifier: MIT
#include "SshProfile.h"

#include "Logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace BlackCore {
namespace {

constexpr const char *kProfilesKey = "ssh/profiles";

//! Оболочка безопасного аргумента: всё, что может быть истолковано shell
QString shellQuote(const QString &value)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9._:@%+/,=~-]+$"));
    if (safe.match(value).hasMatch())
        return value;

    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace

// ---------------------------------------------------------------------------
// Профиль
// ---------------------------------------------------------------------------

QJsonObject SshProfile::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("host"), host);
    object.insert(QStringLiteral("port"), port);
    object.insert(QStringLiteral("user"), user);
    object.insert(QStringLiteral("identityFile"), identityFile);
    return object;
}

SshProfile SshProfile::fromJson(const QJsonObject &object)
{
    SshProfile profile;
    profile.id = object.value(QStringLiteral("id")).toString();
    profile.name = object.value(QStringLiteral("name")).toString();
    profile.host = object.value(QStringLiteral("host")).toString();
    profile.port = object.value(QStringLiteral("port")).toInt(22);
    profile.user = object.value(QStringLiteral("user")).toString();
    profile.identityFile = object.value(QStringLiteral("identityFile")).toString();
    if (profile.id.isEmpty())
        profile.id = SshManager::makeId(profile.name);
    return profile;
}

// ---------------------------------------------------------------------------
// Хранилище
// ---------------------------------------------------------------------------

QList<SshProfile> SshManager::profiles()
{
    QSettings store;
    const QString raw = store.value(QLatin1String(kProfilesKey)).toString();
    if (raw.trimmed().isEmpty())
        return {};

    const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8());
    if (!document.isArray()) {
        Log::warn(QStringLiteral("Список SSH-профилей повреждён — раздел проигнорирован"));
        return {};
    }

    QList<SshProfile> list;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        const SshProfile profile = SshProfile::fromJson(value.toObject());
        if (!profile.id.isEmpty() && !profile.host.isEmpty())
            list.append(profile);
    }
    return list;
}

SshProfile SshManager::profile(const QString &id)
{
    for (const SshProfile &candidate : profiles()) {
        if (candidate.id == id)
            return candidate;
    }
    return {};
}

void SshManager::saveProfile(const SshProfile &profile)
{
    QList<SshProfile> list = profiles();
    SshProfile stored = profile;
    if (stored.id.isEmpty())
        stored.id = makeId(stored.name);
    stored.port = qBound(1, stored.port, 65535);

    bool replaced = false;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id == stored.id) {
            list[i] = stored;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        list.append(stored);

    QJsonArray array;
    for (const SshProfile &item : list)
        array.append(item.toJson());

    QSettings store;
    store.setValue(QLatin1String(kProfilesKey),
                   QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    store.sync();
}

void SshManager::removeProfile(const QString &id)
{
    QList<SshProfile> list = profiles();
    QJsonArray array;
    for (const SshProfile &item : list) {
        if (item.id != id)
            array.append(item.toJson());
    }

    QSettings store;
    store.setValue(QLatin1String(kProfilesKey),
                   QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    store.sync();
}

QString SshManager::makeId(const QString &name)
{
    static const QRegularExpression latin(QStringLiteral("[^A-Za-z0-9]+"));
    QString slug = name.toLower();
    slug.replace(latin, QStringLiteral("-"));
    slug = slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (slug.isEmpty())
        slug = QStringLiteral("ssh");
    return QStringLiteral("ssh-%1").arg(slug.left(40));
}

// ---------------------------------------------------------------------------
// Проверки
// ---------------------------------------------------------------------------

bool SshManager::isValidHost(const QString &host)
{
    const QString trimmed = host.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 255)
        return false;
    // Отклоняем всё, что может быть истолковано оболочкой как команда
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9._:-]*[A-Za-z0-9])?$"));
    if (!pattern.match(trimmed).hasMatch())
        return false;
    if (trimmed.contains(QStringLiteral("..")))
        return false;
    return true;
}

bool SshManager::isValidUser(const QString &user)
{
    const QString trimmed = user.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 64)
        return false;
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9._-]+$"));
    return pattern.match(trimmed).hasMatch();
}

bool SshManager::isValidPort(int port)
{
    return port >= 1 && port <= 65535;
}

QString SshManager::validate(const SshProfile &profile)
{
    if (profile.name.trimmed().isEmpty())
        return QStringLiteral("не указано имя подключения");
    if (profile.host.trimmed().isEmpty())
        return QStringLiteral("не указан адрес сервера");
    if (!isValidHost(profile.host))
        return QStringLiteral("адрес сервера содержит недопустимые символы");
    if (!profile.user.isEmpty() && !isValidUser(profile.user))
        return QStringLiteral("имя пользователя содержит недопустимые символы");
    if (!isValidPort(profile.port))
        return QStringLiteral("порт должен быть в диапазоне 1…65535");
    if (!profile.identityFile.isEmpty()) {
        const QFileInfo key(profile.identityFile);
        if (!key.exists() || !key.isFile())
            return QStringLiteral("файл ключа не найден: %1").arg(profile.identityFile);
    }
    return QString();
}

QStringList SshManager::availableKeys()
{
    const QString sshDirectory = QDir::homePath() + QStringLiteral("/.ssh");
    QDir directory(sshDirectory);
    if (!directory.exists())
        return {};

    // Читаем только список имён: содержимое ключей не открывается
    QStringList keys;
    const QStringList files = directory.entryList({QStringLiteral("id_*"), QStringLiteral("*.pub"),
                                                   QStringLiteral("*.pem")},
                                                  QDir::Files, QDir::Name);
    for (const QString &file : files) {
        if (file.endsWith(QStringLiteral(".pub")))
            continue;
        if (file == QStringLiteral("known_hosts") || file == QStringLiteral("config"))
            continue;
        keys.append(directory.absoluteFilePath(file));
    }
    return keys;
}

QString SshManager::sshExecutable()
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ssh"));
    if (!fromPath.isEmpty())
        return fromPath;
    for (const QString &candidate : {QStringLiteral("/usr/bin/ssh"), QStringLiteral("/bin/ssh")}) {
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    return QString();
}

QStringList SshManager::buildArguments(const SshProfile &profile, QString *error)
{
    if (error)
        error->clear();

    const QString problem = validate(profile);
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return {};
    }

    QStringList arguments;
    arguments << QStringLiteral("-p") << QString::number(profile.port);

    // Ключ передаём только если он существует — остальное решает ~/.ssh/config
    if (!profile.identityFile.isEmpty())
        arguments << QStringLiteral("-i") << profile.identityFile;

    if (!profile.user.isEmpty())
        arguments << QStringLiteral("-l") << profile.user.trimmed();

    arguments << profile.host.trimmed();
    return arguments;
}

QString SshManager::buildCommand(const SshProfile &profile, QString *error)
{
    const QStringList arguments = buildArguments(profile, error);
    if (arguments.isEmpty())
        return QString();

    QStringList quoted;
    quoted << shellQuote(QStringLiteral("ssh"));
    for (const QString &argument : arguments)
        quoted << shellQuote(argument);
    return quoted.join(QLatin1Char(' '));
}

} // namespace BlackCore
