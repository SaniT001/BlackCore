// SPDX-License-Identifier: MIT
#include "NotificationToast.h"
#include "UiIcons.h"

#include "../core/Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace BlackCore {

NotificationToast::NotificationToast(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("notificationToast"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setVisible(false);
    setFixedWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);

    auto *header = new QHBoxLayout;
    m_kindLabel = new QLabel(this);
    m_kindLabel->setProperty("role", QStringLiteral("secondary"));
    m_closeButton = new QPushButton(this);
    m_closeButton->setFlat(true);
    m_closeButton->setFixedSize(24, 24);
    m_closeButton->setIconSize(QSize(UiIcons::kActionSize, UiIcons::kActionSize));
    m_closeButton->setToolTip(tr("Скрыть"));
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    header->addWidget(m_kindLabel, 1);
    header->addWidget(m_closeButton);
    layout->addLayout(header);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);
    layout->addWidget(m_titleLabel);

    m_bodyLabel = new QLabel(this);
    m_bodyLabel->setProperty("role", QStringLiteral("secondary"));
    m_bodyLabel->setWordWrap(true);
    layout->addWidget(m_bodyLabel);

    m_showButton = new QPushButton(tr("История уведомлений"), this);
    m_showButton->setFlat(true);
    layout->addWidget(m_showButton);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() { dismiss(); });

    connect(m_closeButton, &QPushButton::clicked, this, [this]() { dismiss(); });
    connect(m_showButton, &QPushButton::clicked, this, [this]() {
        emit showHistoryRequested();
        dismiss();
    });
    refreshIcons();
}

void NotificationToast::refreshIcons()
{
    const BrandPalette colors = ThemeManager::colors();
    if (m_closeButton) {
        m_closeButton->setIcon(UiIcons::create(QStringLiteral("close"), colors.textPrimary,
                                                colors.error, colors.textSecondary));
    }
}

void NotificationToast::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshIcons();
}

void NotificationToast::setDisplayMs(int milliseconds)
{
    m_displayMs = qBound(1000, milliseconds, 60000);
}

void NotificationToast::push(const Notification &notification)
{
    if (!notification.isValid())
        return;
    m_queue.append(notification);
    if (!isShowing())
        showNext();
}

void NotificationToast::showNext()
{
    if (m_queue.isEmpty()) {
        m_current = Notification();
        setVisible(false);
        return;
    }

    m_current = m_queue.takeFirst();
    m_kindLabel->setText(m_current.kindName().toUpper());
    m_titleLabel->setText(m_current.title);
    m_bodyLabel->setText(m_current.body);
    m_bodyLabel->setVisible(!m_current.body.isEmpty());

    // Цвет полосы зависит от вида уведомления
    const BrandPalette colors = ThemeManager::colors();
    const QColor accent = m_current.kind == NotificationKind::Error ? colors.error
                          : m_current.kind == NotificationKind::SshDisconnected ? colors.textSecondary
                                                                                : colors.gold;
    setStyleSheet(QStringLiteral("QWidget#notificationToast {"
                                 "  background-color: %1;"
                                 "  border: 1px solid %2;"
                                 "  border-left: 3px solid %2;"
                                 "  border-radius: 6px;"
                                 "}")
                      .arg(colors.panel.name(), accent.name()));
    m_kindLabel->setStyleSheet(QStringLiteral("color: %1;").arg(accent.name()));
    refreshIcons();

    reposition();
    setVisible(true);
    raise();
    m_timer->start(m_displayMs);
}

void NotificationToast::dismiss()
{
    m_timer->stop();
    const Notification finished = m_current;
    m_current = Notification();
    setVisible(false);
    if (finished.isValid())
        emit dismissed(finished);
    if (!m_queue.isEmpty())
        showNext();
}

void NotificationToast::reposition()
{
    QWidget *host = parentWidget();
    if (!host)
        return;
    adjustSize();
    const int margin = 14;
    const QSize hint = sizeHint();
    resize(qMax(320, hint.width()), hint.height());
    move(host->width() - width() - margin, host->height() - height() - margin - 24);
}

void NotificationToast::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit showHistoryRequested();
        dismiss();
        return;
    }
    QWidget::mousePressEvent(event);
}

} // namespace BlackCore
