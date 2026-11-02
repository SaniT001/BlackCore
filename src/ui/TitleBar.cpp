// SPDX-License-Identifier: MIT
#include "TitleBar.h"
#include "UiIcons.h"

#include "../core/AppInfo.h"
#include "../core/Theme.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWindow>

namespace BlackCore {
namespace {

constexpr int kTitleBarHeight = 38;
constexpr int kButtonSize = 26;
constexpr int kIconSize = 20;

QToolButton *makeWindowButton(const QString &tooltip, const QString &objectName)
{
    auto *button = new QToolButton;
    button->setObjectName(objectName);
    button->setToolTip(tooltip);
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIconSize(QSize(UiIcons::kActionSize, UiIcons::kActionSize));
    return button;
}

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kTitleBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    m_sidebarButton = new QToolButton(this);
    m_sidebarButton->setObjectName(QStringLiteral("sidebarToggle"));
    m_sidebarButton->setIconSize(QSize(UiIcons::kActionSize, UiIcons::kActionSize));
    m_sidebarButton->setToolTip(tr("Показать/скрыть боковую панель (F9)"));
    m_sidebarButton->setCheckable(true);
    m_sidebarButton->setChecked(true);
    m_sidebarButton->setFixedSize(26, 26);
    m_sidebarButton->setFocusPolicy(Qt::NoFocus);
    m_sidebarButton->setCursor(Qt::ArrowCursor);
    layout->addWidget(m_sidebarButton);
    connect(m_sidebarButton, &QToolButton::toggled, this, &TitleBar::sidebarToggled);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QIcon(QStringLiteral(":/icons/blackcore-32.png")).pixmap(kIconSize, kIconSize));
    m_iconLabel->setFixedSize(kIconSize, kIconSize);
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(QStringLiteral("BlackCore"), this);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QFont titleFont = m_titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    layout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(versionString(), this);
    m_subtitleLabel->setProperty("role", QStringLiteral("secondary"));
    m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_subtitleLabel);

    layout->addStretch(1);

    m_minimizeButton = makeWindowButton(tr("Свернуть"), QStringLiteral("windowButton"));
    m_maximizeButton = makeWindowButton(tr("Развернуть"), QStringLiteral("windowButton"));
    m_closeButton = makeWindowButton(tr("Закрыть"), QStringLiteral("closeButton"));

    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);

    connect(m_minimizeButton, &QToolButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_maximizeButton, &QToolButton::clicked, this, &TitleBar::maximizeRestoreRequested);
    connect(m_closeButton, &QToolButton::clicked, this, &TitleBar::closeRequested);
    refreshIcons();
}

QWidget *TitleBar::windowOrSelf() const
{
    return window();
}

void TitleBar::setTitle(const QString &title)
{
    m_titleLabel->setText(title.isEmpty() ? QStringLiteral("BlackCore") : title);
}

void TitleBar::setSubtitle(const QString &subtitle)
{
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
}

void TitleBar::setWindowMaximized(bool maximized)
{
    m_windowMaximized = maximized;
    m_maximizeButton->setToolTip(maximized ? tr("Восстановить") : tr("Развернуть"));
    refreshIcons();
}

void TitleBar::refreshIcons()
{
    const BrandPalette colors = ThemeManager::colors();
    const auto icon = [&](const QString &name) {
        return UiIcons::create(name, colors.textPrimary, colors.goldLight, colors.textSecondary);
    };
    if (m_sidebarButton)
        m_sidebarButton->setIcon(icon(QStringLiteral("panel-left")));
    if (m_minimizeButton)
        m_minimizeButton->setIcon(icon(QStringLiteral("minimize")));
    if (m_maximizeButton)
        m_maximizeButton->setIcon(icon(m_windowMaximized ? QStringLiteral("restore")
                                                   : QStringLiteral("maximize")));
    if (m_closeButton)
        m_closeButton->setIcon(UiIcons::create(QStringLiteral("close"), colors.textPrimary,
                                                colors.error, colors.textSecondary));
}

void TitleBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshIcons();
}

void TitleBar::setSidebarChecked(bool checked)
{
    const QSignalBlocker blocker(m_sidebarButton);
    m_sidebarButton->setChecked(checked);
}

void TitleBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), ThemeManager::colors().panel);

    // Тонкая золотая линия под заголовком — фирменная деталь
    const QColor accent = ThemeManager::colors().active;
    painter.fillRect(QRect(0, height() - 1, width(), 1), accent);
}

void TitleBar::beginDrag(const QPoint &globalPosition)
{
    QWidget *target = windowOrSelf();
    if (target->isMaximized()) {
        // Перетаскивание развёрнутого окна — восстанавливаем и продолжаем движение
        const qreal ratio = globalPosition.x() / qreal(qMax(1, target->width()));
        target->showNormal();
        const QPoint newPosition(globalPosition.x() - int(target->width() * ratio),
                                 qMax(0, globalPosition.y() - height() / 2));
        target->move(newPosition);
        m_dragOffset = QPoint(int(target->width() * ratio), height() / 2);
        return;
    }

    m_dragOffset = globalPosition - target->frameGeometry().topLeft();
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_dragging = true;
    m_dragMoved = false;
    beginDrag(event->globalPosition().toPoint());
    event->accept();
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint globalPosition = event->globalPosition().toPoint();
    QWidget *target = windowOrSelf();

    if (target->windowHandle() && !m_dragMoved) {
        // Предпочитаем системное перемещение (корректно работает в Cinnamon/Muffin)
        if (target->windowHandle()->startSystemMove()) {
            m_dragMoved = true;
            m_dragging = false;
            return;
        }
    }

    m_dragMoved = true;
    target->move(globalPosition - m_dragOffset);
    event->accept();
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    m_dragMoved = false;
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRestoreRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace BlackCore
