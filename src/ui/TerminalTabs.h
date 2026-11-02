// SPDX-License-Identifier: MIT
#pragma once

#include "../core/PaneLayout.h"

#include <QStringList>
#include <QWidget>

class QStackedWidget;
class QTabBar;
class QToolButton;

namespace BlackCore {

class PaneTree;
class TerminalView;

//! Одна вкладка: дерево панелей + собственное имя.
struct TabEntry
{
    PaneTree *tree = nullptr;
    QString customTitle; //!< имя, заданное пользователем
};

//! Панель вкладок: полоса вкладок + стек деревьев панелей.
class TerminalTabs : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalTabs(QWidget *parent = nullptr);

    //! Новая вкладка с одной панелью (сессия запускается вызывающим кодом при startNow=false)
    PaneTree *addTab(const PaneSpec &spec = PaneSpec(), bool startNow = true);

    //! Новая вкладка из готовой раскладки (рабочее пространство)
    PaneTree *addTabFromLayout(const LayoutNode &layout, const QString &title = QString(),
                               bool startNow = true);

    PaneTree *tabAt(int index) const;
    PaneTree *currentTab() const;
    TerminalView *currentTerminal() const;
    TerminalView *activeTerminal() const;

    int count() const;
    int indexOf(const PaneTree *tree) const;
    int currentIndex() const;
    void setCurrentIndex(int index);

    QStringList sessionTitles() const;
    //! Имя вкладки, заданное пользователем (пусто — автоматическое)
    QString customTitle(int index) const;
    //! Индекс активной панели вкладки (для сохранения сессии)
    int activePaneIndex(int index) const;
    QString tabTitle(int index) const;

    //! Переименование вкладки (пустая строка — вернуть автоматическое имя)
    void renameTab(int index, const QString &title);
    //! Дублировать вкладку: та же раскладка, те же каталоги, новая сессия
    PaneTree *duplicateTab(int index);

    void removeTab(int index);

    //! Показать/скрыть полосу вкладок (сама вкладка остаётся рабочей)
    void setTabBarVisible(bool visible);
    bool isTabBarVisible() const;
    void setTabTitle(int index, const QString &title);
    void updateTabTitleFromTerminal(int index);

    //! Какие панели сейчас открыты (для сохранения сессии)
    QList<PaneTree *> trees() const;
    //! Все терминалы всех вкладок
    QList<TerminalView *> allTerminals() const;

signals:
    void currentChanged(int index);
    void tabCountChanged(int count);
    void newTabRequested();
    void tabCloseRequested(int index);
    void tabRenameRequested(int index);
    //! Пользователь просит закрыть панель — решение принимает главное окно
    void paneCloseRequested(BlackCore::TerminalView *terminal);

    //! Создан новый терминал (панель или новая вкладка)
    void terminalCreated(BlackCore::TerminalView *terminal, bool startNow);
    void activePaneChanged(BlackCore::TerminalView *terminal);
    void paneCountChanged(int index);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onTabChanged(int index);

private:
    PaneTree *createTab(const QString &title = QString());
    void refreshIcons();
    void connectTree(PaneTree *tree, int index);
    void refreshTabTitle(int index);
    int indexOfTree(const PaneTree *tree) const;

    QWidget *m_tabRow = nullptr;
    QTabBar *m_tabBar = nullptr;
    QToolButton *m_newTabButton = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<TabEntry> m_entries;
    bool m_pendingStartNow = true;
};

} // namespace BlackCore
