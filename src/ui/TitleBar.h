// SPDX-License-Identifier: MIT
#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace BlackCore {

//! Фирменная строка заголовка (используется вместо системной рамки).
/*!
 *  Слева — переключатель боковой панели, иконка и название, справа — кнопки
 *  «свернуть / развернуть / закрыть». Перетаскивание и двойной клик работают
 *  и в Cinnamon, и в других оконных менеджерах.
 */
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setSubtitle(const QString &subtitle);
    void setWindowMaximized(bool maximized);
    void setSidebarChecked(bool checked);

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();
    void sidebarToggled(bool visible);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void beginDrag(const QPoint &globalPosition);
    void refreshIcons();
    QWidget *windowOrSelf() const;

    QToolButton *m_sidebarButton = nullptr;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;

    bool m_windowMaximized = false;
    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_dragOffset;
};

} // namespace BlackCore
