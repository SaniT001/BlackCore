// SPDX-License-Identifier: MIT
#include "Notifications.h"

#include "Secrets.h"

#include <QCryptographicHash>
#include <QSettings>

namespace BlackCore {
namespace {

constexpr const char *kEnabledKey = "notifications/enabled";
constexpr const char *kLongCommandKey = "notifications/longCommand";
constexpr const char *kScriptKey = "notifications/script";
constexpr const char *kSshKey = "notifications/sshDisconnected";
constexpr const char *kErrorsKey = "notifications/errors";
constexpr const char *kThresholdKey = "notifications/longCommandSeconds";
constexpr const char *kDesktopKey = "notifications/desktop";
constexpr int kMaxHistory = 100;

QString secondsText(int durationMs)
{
    const double seconds = double(durationMs) / 1000.0;
    QString text;
    if (seconds < 60.0)
        text = QString::number(seconds, 'f', 1);
    else
        text = QString::number(seconds / 60.0, 'f', 1);
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return seconds < 60.0 ? QStringLiteral("%1 с").arg(text) : QStringLiteral("%1 мин").arg(text);
}

} // namespace

// ---------------------------------------------------------------------------
// NotificationSettings
// ---------------------------------------------------------------------------

NotificationSettings NotificationSettings::fromSettings()
{
    QSettings store;
    NotificationSettings settings;
    settings.enabled = store.value(QLatin1String(kEnabledKey), true).toBool();
    settings.longCommand = store.value(QLatin1String(kLongCommandKey), true).toBool();
    settings.script = store.value(QLatin1String(kScriptKey), true).toBool();
    settings.sshDisconnected = store.value(QLatin1String(kSshKey), true).toBool();
    settings.errors = store.value(QLatin1String(kErrorsKey), true).toBool();
    settings.desktop = store.value(QLatin1String(kDesktopKey), false).toBool();
    settings.longCommandSeconds =
        qBound(kMinSeconds, store.value(QLatin1String(kThresholdKey), 10).toInt(), kMaxSeconds);
    return settings;
}

void NotificationSettings::applyToSettings() const
{
    QSettings store;
    store.setValue(QLatin1String(kEnabledKey), enabled);
    store.setValue(QLatin1String(kLongCommandKey), longCommand);
    store.setValue(QLatin1String(kScriptKey), script);
    store.setValue(QLatin1String(kSshKey), sshDisconnected);
    store.setValue(QLatin1String(kErrorsKey), errors);
    store.setValue(QLatin1String(kDesktopKey), desktop);
    store.setValue(QLatin1String(kThresholdKey),
                   qBound(kMinSeconds, longCommandSeconds, kMaxSeconds));
    store.sync();
}

bool NotificationSettings::kindEnabled(NotificationKind kind) const
{
    if (!enabled)
        return false;
    switch (kind) {
    case NotificationKind::LongCommand: return longCommand;
    case NotificationKind::Script: return script;
    case NotificationKind::SshDisconnected: return sshDisconnected;
    case NotificationKind::Error: return errors;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Notification
// ---------------------------------------------------------------------------

QString Notification::kindName() const
{
    return NotificationCenter::kindName(kind);
}

// ---------------------------------------------------------------------------
// NotificationCenter
// ---------------------------------------------------------------------------

NotificationCenter::NotificationCenter()
    : m_settings(NotificationSettings::fromSettings())
{
}

NotificationCenter &NotificationCenter::instance()
{
    static NotificationCenter center;
    return center;
}

int NotificationCenter::maxHistory()
{
    return kMaxHistory;
}

QString NotificationCenter::kindName(NotificationKind kind)
{
    switch (kind) {
    case NotificationKind::LongCommand: return QStringLiteral("команда");
    case NotificationKind::Script: return QStringLiteral("сценарий");
    case NotificationKind::SshDisconnected: return QStringLiteral("SSH");
    case NotificationKind::Error: return QStringLiteral("ошибка");
    }
    return QString();
}

void NotificationCenter::setSettings(const NotificationSettings &settings)
{
    m_settings = settings;
    m_settings.longCommandSeconds = qBound(NotificationSettings::kMinSeconds,
                                           settings.longCommandSeconds,
                                           NotificationSettings::kMaxSeconds);
    m_settings.applyToSettings();
}

void NotificationCenter::reloadSettings()
{
    m_settings = NotificationSettings::fromSettings();
}

bool NotificationCenter::notify(NotificationKind kind, const QString &title, const QString &body)
{
    if (!m_settings.kindEnabled(kind))
        return false;
    if (title.trimmed().isEmpty())
        return false;

    Notification notification;
    notification.kind = kind;
    // В уведомления (и их историю) не попадают секреты
    notification.title = Secrets::redact(title).left(200);
    notification.body = Secrets::redact(body).left(1000);
    notification.at = QDateTime::currentDateTime();
    notification.id = QStringLiteral("n")
        + QString::fromLatin1(QCryptographicHash::hash(
                                  (notification.title + notification.at.toString(Qt::ISODateWithMs)).toUtf8(),
                                  QCryptographicHash::Sha1)
                                  .toHex()
                                  .left(10));

    m_history.prepend(notification);
    while (m_history.size() > kMaxHistory)
        m_history.removeLast();

    emit notified(notification);
    return true;
}

bool NotificationCenter::notifyCommandFinished(const QString &command, int durationMs, int exitCode,
                                               NotificationKind kind)
{
    if (!m_settings.kindEnabled(kind))
        return false;
    if (durationMs < m_settings.longCommandSeconds * 1000)
        return false;

    const QString title = kind == NotificationKind::Script
        ? QStringLiteral("Сценарий завершён")
        : QStringLiteral("Команда завершена");
    QString body = QStringLiteral("%1 · %2").arg(secondsText(durationMs),
                                                 exitCode < 0
                                                     ? QStringLiteral("код возврата неизвестен")
                                                     : (exitCode == 0
                                                            ? QStringLiteral("успешно")
                                                            : QStringLiteral("код %1").arg(exitCode)));
    if (!command.isEmpty())
        body = QStringLiteral("%1\n%2").arg(Secrets::redact(command).left(200), body);
    return notify(kind, title, body);
}

int NotificationCenter::unreadCount() const
{
    int count = 0;
    for (const Notification &notification : m_history) {
        if (!notification.read)
            ++count;
    }
    return count;
}

void NotificationCenter::markAllRead()
{
    for (Notification &notification : m_history)
        notification.read = true;
}

void NotificationCenter::clearHistory()
{
    m_history.clear();
}

} // namespace BlackCore
