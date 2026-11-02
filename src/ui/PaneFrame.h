// SPDX-License-Identifier: MIT
#pragma once

#include "../core/PaneLayout.h"

#include <QWidget>

class QLabel;
class QPaintEvent;
class QToolButton;

namespace BlackCore {

class TerminalView;

//! Панель терминала с необязательным заголовком (имя, каталог, закрытие).
class PaneFrame : public QWidget
{
    Q_OBJECT

public:
    explicit PaneFrame(const PaneSpec &spec, QWidget *parent = nullptr);

    TerminalView *terminal() const { return m_terminal; }

    QString paneTitle() const { return m_spec.title; }
    void setPaneTitle(const QString &title);

    PaneSpec paneSpec() const;
    void setPaneSpec(const PaneSpec &spec);

    void setHeaderVisible(bool visible);
    bool isHeaderVisible() const;
    void setActive(bool active);
    bool isActive() const { return m_active; }

    void updateInfo();

    //! Применить каталог/профиль/команду спецификации к терминалу панели
    void applySpecToTerminal();

signals:
    void closeRequested(BlackCore::PaneFrame *frame);
    void activated(BlackCore::PaneFrame *frame);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void refreshIcons();

    PaneSpec m_spec;
    TerminalView *m_terminal = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QToolButton *m_closeButton = nullptr;
    bool m_active = false;
    bool m_headerVisible = true;
};

} // namespace BlackCore
