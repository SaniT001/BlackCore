// SPDX-License-Identifier: MIT
#pragma once

#include "../core/Notifications.h"

#include <QList>
#include <QWidget>

class QEvent;

class QLabel;
class QPushButton;
class QTimer;

namespace BlackCore {

//! Всплывающая плашка уведомления в правом нижнем углу окна.
/*!
 *  Плашка не забирает фокус и не мешает работе: показывается поверх окна,
 *  автоматически скрывается через несколько секунд. Если уведомление приходит
 *  во время показа другого, оно встаёт в очередь.
 */
class NotificationToast : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationToast(QWidget *parent = nullptr);

    //! Показать уведомление (с накоплением в очереди)
    void push(const Notification &notification);

    //! Текущее показанное уведомление (для тестов)
    Notification current() const { return m_current; }
    bool isShowing() const { return m_current.isValid() && isVisible(); }
    int queuedCount() const { return m_queue.size(); }

    //! Закрыть текущее и показать следующее из очереди
    void dismiss();

    //! Время показа одной плашки (мс)
    void setDisplayMs(int milliseconds);
    int displayMs() const { return m_displayMs; }

    //! Пересчитать положение относительно родителя
    void reposition();

signals:
    //! Пользователь закрыл плашку или она скрылась сама
    void dismissed(const BlackCore::Notification &notification);
    //! Пользователь нажал «Показать» (окно открывает историю уведомлений)
    void showHistoryRequested();

protected:
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void showNext();
    void refreshIcons();

    QLabel *m_kindLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_bodyLabel = nullptr;
    QPushButton *m_showButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QTimer *m_timer = nullptr;

    Notification m_current;
    QList<Notification> m_queue;
    int m_displayMs = 6000;
};

} // namespace BlackCore
