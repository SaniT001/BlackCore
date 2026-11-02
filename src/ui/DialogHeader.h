// SPDX-License-Identifier: MIT
#pragma once

#include <QWidget>

class QEvent;
class QToolButton;

namespace BlackCore {

//! Собственный заголовок диалога в стиле BlackCore: перетаскивание + кнопка закрытия.
class DialogHeader : public QWidget
{
public:
    explicit DialogHeader(const QString &title, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void refreshIcons();

    QPoint m_offset;
    QToolButton *m_closeButton = nullptr;
};

} // namespace BlackCore
