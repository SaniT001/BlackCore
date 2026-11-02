// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace BlackCore {

//! Виды уведомлений; каждый можно отключить отдельно.
enum class NotificationKind {
    LongCommand = 0,   //!< завершение долгой команды
    Script,            //!< завершение сохранённого сценария
    SshDisconnected,   //!< потеря SSH-соединения
    Error,             //!< ошибки приложения
};

//! Настройки уведомлений (хранятся в QSettings, раздел `notifications/`).
struct NotificationSettings
{
    bool enabled = true;             //!< общий выключатель
    bool longCommand = true;
    bool script = true;
    bool sshDisconnected = true;
    bool errors = true;
    //! Через сколько секунд команда считается «долгой» (5 … 3600)
    int longCommandSeconds = 10;
    //! Показывать системные уведомления рабочего стола (если доступны)
    bool desktop = false;

    static constexpr int kMinSeconds = 5;
    static constexpr int kMaxSeconds = 3600;

    static NotificationSettings fromSettings();
    void applyToSettings() const;

    bool kindEnabled(NotificationKind kind) const;
};

//! Одно показанное уведомление (оно же попадает в историю уведомлений).
struct Notification
{
    QString id;
    NotificationKind kind = NotificationKind::LongCommand;
    QString title;
    QString body;
    QDateTime at;
    bool read = false;

    QString kindName() const;
    bool isValid() const { return !title.isEmpty() && at.isValid(); }
};

//! Центр уведомлений: учитывает настройки, ведёт историю, рассылает сигнал.
/*!
 *  NotificationCenter не показывает ничего сам: он только решает, нужно ли
 *  уведомление с учётом настроек, и отправляет сигнал. Показ (всплывающая
 *  плашка и/или системное уведомление) выполняет окно. Полное отключение
 *  (settings.enabled == false) означает, что не приходит ни одного сигнала.
 */
class NotificationCenter : public QObject
{
    Q_OBJECT

public:
    static NotificationCenter &instance();

    NotificationSettings settings() const { return m_settings; }
    void setSettings(const NotificationSettings &settings);
    void reloadSettings();

    bool enabled() const { return m_settings.enabled; }

    //! Показать уведомление, если это разрешено настройками. Возвращает false, если подавлено.
    bool notify(NotificationKind kind, const QString &title, const QString &body = QString());

    //! Уведомление для завершения команды: решает, «долгая» ли она была
    bool notifyCommandFinished(const QString &command, int durationMs, int exitCode,
                               NotificationKind kind = NotificationKind::LongCommand);

    QList<Notification> history() const { return m_history; }
    int unreadCount() const;
    void markAllRead();
    void clearHistory();

    static int maxHistory();
    static QString kindName(NotificationKind kind);

    //! Тестовый помощник: подменить настройки, не трогая файл
    void setSettingsForTest(const NotificationSettings &settings) { m_settings = settings; }

signals:
    //! Уведомление разрешено настройками и должно быть показано
    void notified(const BlackCore::Notification &notification);

private:
    NotificationCenter();

    NotificationSettings m_settings;
    QList<Notification> m_history;
};

} // namespace BlackCore
