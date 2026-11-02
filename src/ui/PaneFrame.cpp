// SPDX-License-Identifier: MIT
#include "PaneFrame.h"
#include "UiIcons.h"

#include "../core/Logger.h"
#include "../core/Profile.h"
#include "../core/Theme.h"
#include "../terminal/TerminalView.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace BlackCore {

PaneFrame::PaneFrame(const PaneSpec &spec, QWidget *parent)
    : QWidget(parent)
    , m_spec(spec)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setFixedHeight(26);
    m_header->setCursor(Qt::ArrowCursor);
    auto *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(6);

    m_titleLabel = new QLabel(m_header);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSizeF(qMax(7.5, titleFont.pointSizeF() - 1.0));
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    headerLayout->addWidget(m_titleLabel);

    m_infoLabel = new QLabel(m_header);
    m_infoLabel->setProperty("role", QStringLiteral("secondary"));
    m_infoLabel->setFont(titleFont);
    headerLayout->addWidget(m_infoLabel, 1);

    m_closeButton = new QToolButton(m_header);
    m_closeButton->setObjectName(QStringLiteral("windowButton"));
    m_closeButton->setIconSize(QSize(14, 14));
    refreshIcons();
    m_closeButton->setToolTip(tr("Закрыть панель (Ctrl+Shift+X)"));
    m_closeButton->setFixedSize(18, 18);
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(m_closeButton);

    layout->addWidget(m_header);

    m_terminal = new TerminalView(this);
    layout->addWidget(m_terminal, 1);

    connect(m_closeButton, &QToolButton::clicked, this, [this]() { emit closeRequested(this); });
    connect(m_terminal, &TerminalView::titleChanged, this, &PaneFrame::updateInfo);

    applySpecToTerminal();

    setPaneTitle(spec.title);
    updateInfo();
    setActive(false);
}

void PaneFrame::refreshIcons()
{
    if (!m_closeButton)
        return;
    const BrandPalette colors = ThemeManager::colors();
    m_closeButton->setIcon(UiIcons::create(QStringLiteral("close"), colors.textPrimary,
                                            colors.error, colors.textSecondary));
}

void PaneFrame::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshIcons();
}

void PaneFrame::applySpecToTerminal()
{
    if (!m_terminal)
        return;

    // Профиль: оболочка, шрифт, курсор, прокрутка — только для этой панели
    const TerminalProfile profile = ProfileManager::profile(m_spec.profileId);
    if (!profile.shell.isEmpty())
        m_terminal->setShellOverride(profile.shell);
    if (profile.fontSize > 0 || !profile.fontFamily.isEmpty())
        m_terminal->applyProfileAppearance(profile.fontFamily, profile.fontSize,
                                           profile.cursorStyle, profile.scrollback);

    if (!m_spec.workingDirectory.isEmpty())
        m_terminal->setStartDirectory(m_spec.workingDirectory);

    // Команды выполняются только у шаблонов рабочих пространств:
    // при восстановлении сессии runCommand/command уже очищены SessionStore.
    if (m_spec.runCommand && !m_spec.command.isEmpty()) {
        Log::info(tr("Панель «%1»: запуск «%2»").arg(m_spec.title, m_spec.command));
        m_terminal->setInitialCommand(m_spec.command);
    }
}

void PaneFrame::setPaneTitle(const QString &title)
{
    m_spec.title = title;
    const QString shown = title.trimmed().isEmpty() ? tr("Терминал") : title.trimmed();
    m_titleLabel->setText(shown);
    m_titleLabel->setToolTip(shown);
}

void PaneFrame::setPaneSpec(const PaneSpec &spec)
{
    m_spec = spec;
    setPaneTitle(spec.title);
    updateInfo();
}

PaneSpec PaneFrame::paneSpec() const
{
    PaneSpec spec = m_spec;
    if (spec.title.trimmed().isEmpty())
        spec.title = tr("Терминал");
    // Актуальный каталог берём из процесса оболочки
    const QString directory = m_terminal->workingDirectory();
    if (!directory.isEmpty())
        spec.workingDirectory = directory;
    return spec;
}

void PaneFrame::setHeaderVisible(bool visible)
{
    m_headerVisible = visible;
    m_header->setVisible(visible);
}

bool PaneFrame::isHeaderVisible() const
{
    return m_headerVisible;
}

void PaneFrame::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    const BrandPalette colors = ThemeManager::colors();
    m_titleLabel->setStyleSheet(QStringLiteral("color: %1;")
                                    .arg(active ? colors.goldLight.name() : colors.textSecondary.name()));
    update();
}

void PaneFrame::updateInfo()
{
    const QString directory = m_terminal->workingDirectory();
    if (directory.isEmpty()) {
        m_infoLabel->clear();
        return;
    }
    const QString shown = QDir::toNativeSeparators(directory);
    m_infoLabel->setText(shown);
    m_infoLabel->setToolTip(shown);
}

void PaneFrame::mousePressEvent(QMouseEvent *event)
{
    emit activated(this);
    QWidget::mousePressEvent(event);
}

void PaneFrame::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (!m_headerVisible)
        return;

    // Тонкая золотая линия под заголовком активной панели
    QPainter painter(this);
    const BrandPalette colors = ThemeManager::colors();
    painter.fillRect(QRect(0, m_header->height() - 1, width(), 1),
                     m_active ? colors.gold : colors.active);
}

} // namespace BlackCore
