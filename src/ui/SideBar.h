// SPDX-License-Identifier: MIT
#pragma once

#include <QStringList>
#include <QPair>
#include <QVector>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QEvent;

namespace BlackCore {

//! Боковая панель: список сессий, быстрые действия и данные о текущей сессии.
class SideBar : public QWidget
{
    Q_OBJECT

public:
    explicit SideBar(QWidget *parent = nullptr);

    void setSessions(const QStringList &titles, int currentIndex);
    void setCurrentIndex(int index);
    void setSessionInfo(const QString &shell, const QString &directory, const QString &size,
                        int pid, bool running);
    void setCounters(int sessions);

signals:
    void newTabRequested();
    void sessionActivated(int index);
    void copyRequested();
    void pasteRequested();
    void executeRequested();
    void clearRequested();
    void settingsRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateCounters(int sessions);
    void refreshActionIcons();

    QLabel *m_sessionsCount = nullptr;
    QListWidget *m_sessionList = nullptr;
    QLabel *m_shellValue = nullptr;
    QLabel *m_directoryValue = nullptr;
    QLabel *m_sizeValue = nullptr;
    QLabel *m_pidValue = nullptr;
    QLabel *m_footer = nullptr;
    QVector<QPair<QPushButton *, QString>> m_actionButtons;
};

} // namespace BlackCore
