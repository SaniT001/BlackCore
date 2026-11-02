// SPDX-License-Identifier: MIT
#include "DialogHeader.h"
#include "UiIcons.h"

#include "../core/Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWindow>

namespace BlackCore {

DialogHeader::DialogHeader(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(38);
    setCursor(Qt::ArrowCursor);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 8, 0);
    layout->setSpacing(8);

    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/blackcore-32.png")).pixmap(18, 18));
    icon->setFixedSize(18, 18);
    layout->addWidget(icon);

    auto *label = new QLabel(title, this);
    QFont font = label->font();
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    layout->addWidget(label);
    layout->addStretch(1);

    m_closeButton = new QToolButton(this);
    m_closeButton->setObjectName(QStringLiteral("closeButton"));
    m_closeButton->setToolTip(tr("Закрыть"));
    m_closeButton->setFixedSize(26, 26);
    m_closeButton->setIconSize(QSize(UiIcons::kActionSize, UiIcons::kActionSize));
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_closeButton);
    QObject::connect(m_closeButton, &QToolButton::clicked, this, [this]() {
        if (QWidget *top = window())
            top->close();
    });
    refreshIcons();
}

void DialogHeader::refreshIcons()
{
    const BrandPalette colors = ThemeManager::colors();
    if (m_closeButton) {
        m_closeButton->setIcon(UiIcons::create(QStringLiteral("close"), colors.textPrimary,
                                                colors.error, colors.textSecondary));
    }
}

void DialogHeader::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshIcons();
}

void DialogHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_offset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void DialogHeader::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_offset.isNull()) {
        if (auto *handle = window()->windowHandle()) {
            if (handle->startSystemMove()) {
                m_offset = QPoint();
                return;
            }
        }
        window()->move(event->globalPosition().toPoint() - m_offset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void DialogHeader::mouseReleaseEvent(QMouseEvent *event)
{
    m_offset = QPoint();
    QWidget::mouseReleaseEvent(event);
}

void DialogHeader::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    const BrandPalette colors = ThemeManager::colors();
    painter.fillRect(rect(), colors.panel);
    painter.fillRect(QRect(0, height() - 1, width(), 1), colors.active);
}


} // namespace BlackCore
