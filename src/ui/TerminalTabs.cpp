// SPDX-License-Identifier: MIT
#include "TerminalTabs.h"
#include "UiIcons.h"

#include "../core/Profile.h"
#include "../core/Theme.h"
#include "../terminal/TerminalView.h"
#include "PaneFrame.h"
#include "PaneTree.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace BlackCore {

TerminalTabs::TerminalTabs(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *tabRow = new QWidget(this);
    m_tabRow = tabRow;
    auto *tabRowLayout = new QHBoxLayout(tabRow);
    tabRowLayout->setContentsMargins(6, 0, 6, 0);
    tabRowLayout->setSpacing(4);

    m_tabBar = new QTabBar(tabRow);
    m_tabBar->setExpanding(false);
    m_tabBar->setDrawBase(false);
    m_tabBar->setMovable(true);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setFocusPolicy(Qt::NoFocus);
    tabRowLayout->addWidget(m_tabBar, 1);

    m_newTabButton = new QToolButton(tabRow);
    m_newTabButton->setObjectName(QStringLiteral("windowButton"));
    m_newTabButton->setIconSize(QSize(UiIcons::kActionSize, UiIcons::kActionSize));
    m_newTabButton->setToolTip(tr("Новая вкладка (Ctrl+Shift+T)"));
    m_newTabButton->setFixedSize(24, 24);
    m_newTabButton->setFocusPolicy(Qt::NoFocus);
    m_newTabButton->setCursor(Qt::PointingHandCursor);
    tabRowLayout->addWidget(m_newTabButton, 0, Qt::AlignVCenter);

    layout->addWidget(tabRow);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, &TerminalTabs::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &TerminalTabs::tabCloseRequested);
    connect(m_tabBar, &QTabBar::tabBarDoubleClicked, this, &TerminalTabs::tabRenameRequested);
    connect(m_newTabButton, &QToolButton::clicked, this, &TerminalTabs::newTabRequested);
    refreshIcons();
}

void TerminalTabs::refreshIcons()
{
    const BrandPalette colors = ThemeManager::colors();
    if (m_newTabButton) {
        m_newTabButton->setIcon(UiIcons::create(QStringLiteral("new-tab"), colors.textPrimary,
                                                 colors.goldLight, colors.textSecondary));
    }
    if (!m_tabBar)
        return;
    for (int i = 0; i < m_tabBar->count(); ++i) {
        if (auto *button = qobject_cast<QToolButton *>(m_tabBar->tabButton(i, QTabBar::RightSide))) {
            button->setIcon(UiIcons::create(QStringLiteral("close"), colors.textPrimary,
                                             colors.error, colors.textSecondary));
        }
    }
}

void TerminalTabs::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshIcons();
}

int TerminalTabs::indexOfTree(const PaneTree *tree) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).tree == tree)
            return i;
    }
    return -1;
}

void TerminalTabs::connectTree(PaneTree *tree, int index)
{
    Q_UNUSED(index)
    connect(tree, &PaneTree::paneAdded, this, [this, tree](TerminalView *terminal) {
        emit terminalCreated(terminal, m_pendingStartNow);
        const int tabIndex = indexOfTree(tree);
        if (tabIndex >= 0)
            refreshTabTitle(tabIndex);
        emit tabCountChanged(count());
    });

    connect(tree, &PaneTree::paneRemoved, this, [this, tree]() {
        const int tabIndex = indexOfTree(tree);
        if (tabIndex >= 0)
            emit paneCountChanged(tabIndex);
    });

    connect(tree, &PaneTree::activePaneChanged, this, [this, tree](TerminalView *terminal) {
        if (indexOfTree(tree) == m_stack->currentIndex())
            emit activePaneChanged(terminal);
    });

    connect(tree, &PaneTree::paneCloseRequested, this, [this](TerminalView *terminal) {
        // Решение о закрытии (с проверкой процессов) принимает главное окно
        emit paneCloseRequested(terminal);
    });
}

PaneTree *TerminalTabs::createTab(const QString &title)
{
    auto *tree = new PaneTree(this);
    const int index = m_stack->addWidget(tree);

    m_entries.insert(index, TabEntry{tree, title.trimmed()});

    {
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->insertTab(index, title.isEmpty() ? tr("Терминал %1").arg(index + 1) : title);
        m_tabBar->setCurrentIndex(index);
        m_stack->setCurrentIndex(index);
    }

    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName(QStringLiteral("windowButton"));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setToolTip(tr("Закрыть вкладку (Ctrl+Shift+W)"));
    closeButton->setFixedSize(18, 18);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QToolButton::clicked, this, [this, tree]() {
        const int tabIndex = indexOfTree(tree);
        if (tabIndex >= 0)
            emit tabCloseRequested(tabIndex);
    });
    m_tabBar->setTabButton(index, QTabBar::RightSide, closeButton);
    refreshIcons();

    connectTree(tree, index);
    return tree;
}

PaneTree *TerminalTabs::addTab(const PaneSpec &spec, bool startNow)
{
    PaneTree *tree = createTab();
    const int index = indexOfTree(tree);

    m_pendingStartNow = startNow;
    tree->addPane(spec, startNow);
    m_pendingStartNow = true;

    refreshTabTitle(index);
    emit tabCountChanged(count());
    emit currentChanged(m_stack->currentIndex());
    return tree;
}

PaneTree *TerminalTabs::addTabFromLayout(const LayoutNode &layout, const QString &title, bool startNow)
{
    PaneTree *tree = createTab(title);
    const int index = indexOfTree(tree);

    m_pendingStartNow = startNow;
    tree->applyLayout(layout, startNow);
    m_pendingStartNow = true;

    refreshTabTitle(index);
    emit tabCountChanged(count());
    emit currentChanged(m_stack->currentIndex());
    return tree;
}

PaneTree *TerminalTabs::tabAt(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return nullptr;
    return m_entries.at(index).tree;
}

PaneTree *TerminalTabs::currentTab() const
{
    return tabAt(m_stack->currentIndex());
}

TerminalView *TerminalTabs::currentTerminal() const
{
    PaneTree *tree = currentTab();
    return tree ? tree->activeTerminal() : nullptr;
}

TerminalView *TerminalTabs::activeTerminal() const
{
    return currentTerminal();
}

int TerminalTabs::indexOf(const PaneTree *tree) const
{
    return indexOfTree(tree);
}

int TerminalTabs::count() const
{
    return m_entries.size();
}

int TerminalTabs::currentIndex() const
{
    return m_stack->currentIndex();
}

void TerminalTabs::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_tabBar->setCurrentIndex(index);
    m_stack->setCurrentIndex(index);
}

void TerminalTabs::onTabChanged(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;

    m_stack->setCurrentIndex(index);
    if (PaneTree *tree = m_entries.at(index).tree) {
        if (TerminalView *terminal = tree->activeTerminal())
            terminal->setFocus();
    }
    emit currentChanged(index);
}

void TerminalTabs::refreshTabTitle(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;

    const TabEntry &entry = m_entries.at(index);
    QString title = entry.customTitle;

    if (title.isEmpty() && entry.tree) {
        const QList<TerminalView *> terminals = entry.tree->terminals();
        if (terminals.size() > 1) {
            // Несколько панелей: имя вкладки и количество панелей
            title = tr("Вкладка %1 · панелей: %2").arg(index + 1).arg(terminals.size());
        } else if (!terminals.isEmpty()) {
            const QString programTitle = terminals.first()->terminalTitle().trimmed();
            title = programTitle.isEmpty() ? tr("Терминал %1").arg(index + 1) : programTitle;
        } else {
            title = tr("Терминал %1").arg(index + 1);
        }
    }

    m_tabBar->setTabText(index, title);
    m_tabBar->setTabToolTip(index, title);
}

void TerminalTabs::renameTab(int index, const QString &title)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries[index].customTitle = title.trimmed();
    refreshTabTitle(index);
}

QString TerminalTabs::tabTitle(int index) const
{
    if (index < 0 || index >= m_tabBar->count())
        return QString();
    return m_tabBar->tabText(index);
}

PaneTree *TerminalTabs::duplicateTab(int index)
{
    PaneTree *source = tabAt(index);
    if (!source)
        return nullptr;

    const QSharedPointer<LayoutNode> layout = source->captureLayout();
    if (!layout)
        return nullptr;

    const QString title = m_entries.at(index).customTitle;
    PaneTree *copy = addTabFromLayout(*layout, title);
    return copy;
}

void TerminalTabs::setTabBarVisible(bool visible)
{
    if (m_tabRow)
        m_tabRow->setVisible(visible);
}

bool TerminalTabs::isTabBarVisible() const
{
    return m_tabRow && m_tabRow->isVisible();
}

void TerminalTabs::removeTab(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    if (m_entries.size() <= 1) {
        emit newTabRequested();
        return;
    }

    PaneTree *tree = m_entries.at(index).tree;
    m_entries.removeAt(index);
    m_stack->removeWidget(tree);
    tree->deleteLater();
    m_tabBar->removeTab(index);

    emit tabCountChanged(count());
    emit currentChanged(m_stack->currentIndex());
}

void TerminalTabs::setTabTitle(int index, const QString &title)
{
    if (index < 0 || index >= m_tabBar->count())
        return;
    m_tabBar->setTabText(index, title);
    m_tabBar->setTabToolTip(index, title);
}

void TerminalTabs::updateTabTitleFromTerminal(int index)
{
    refreshTabTitle(index);
}

QStringList TerminalTabs::sessionTitles() const
{
    QStringList titles;
    for (int i = 0; i < m_tabBar->count(); ++i)
        titles.append(m_tabBar->tabText(i));
    return titles;
}

QString TerminalTabs::customTitle(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return QString();
    return m_entries.at(index).customTitle;
}

int TerminalTabs::activePaneIndex(int index) const
{
    PaneTree *tree = tabAt(index);
    if (!tree)
        return 0;
    const QList<PaneFrame *> frames = tree->frames();
    const int active = frames.indexOf(tree->activeFrame());
    return active < 0 ? 0 : active;
}

QList<TerminalView *> TerminalTabs::allTerminals() const
{
    QList<TerminalView *> result;
    for (const TabEntry &entry : m_entries) {
        if (entry.tree)
            result.append(entry.tree->terminals());
    }
    return result;
}

QList<PaneTree *> TerminalTabs::trees() const
{
    QList<PaneTree *> result;
    for (const TabEntry &entry : m_entries)
        result.append(entry.tree);
    return result;
}

} // namespace BlackCore
