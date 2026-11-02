// SPDX-License-Identifier: MIT
#pragma once

#include "../core/PaneLayout.h"

#include <QList>
#include <QWidget>

class QSplitter;

namespace BlackCore {

class PaneFrame;
class TerminalView;

//! Дерево панелей одной вкладки: сплиты, фокус, независимые сессии.
class PaneTree : public QWidget
{
    Q_OBJECT

public:
    explicit PaneTree(QWidget *parent = nullptr);
    ~PaneTree() override;

    //! Создать первую панель. startNow=false — сессия запускается вызывающим кодом.
    PaneFrame *addPane(const PaneSpec &spec, bool startNow = true);

    //! Разделить панель с указанным терминалом. Возвращает новую панель.
    PaneFrame *split(TerminalView *target, Qt::Orientation orientation, const PaneSpec &spec,
                     bool startNow = true);

    //! Закрыть панель. Возвращает true, если панель была удалена.
    bool closePane(TerminalView *terminal);

    QList<PaneFrame *> frames() const;
    QList<TerminalView *> terminals() const;

    TerminalView *activeTerminal() const;
    PaneFrame *activeFrame() const;
    void setActiveFrame(PaneFrame *frame);
    void setActiveTerminal(TerminalView *terminal);

    //! Переключение фокуса между панелями по кругу
    void focusNextPane(int delta = 1);
    void focusPane(int index);
    //! Фокус на панель в указанном направлении (Qt::LeftEdge/RightEdge/TopEdge/BottomEdge)
    void focusDirection(Qt::Edge direction);

    int paneCount() const;
    bool isSplit() const { return paneCount() > 1; }

    //! Текущая раскладка (для сохранения, дублирования, рабочих пространств)
    QSharedPointer<LayoutNode> captureLayout() const;

    //! Применить раскладку (панели создаются, сессии запускает вызывающий код)
    QList<PaneFrame *> applyLayout(const LayoutNode &layout, bool startNow);

    //! Показать/скрыть заголовки панелей (скрыты, пока панель одна)
    void updateHeaderVisibility();

signals:
    void paneAdded(BlackCore::TerminalView *terminal);
    void paneRemoved();
    void layoutChanged();
    void activePaneChanged(BlackCore::TerminalView *terminal);
    void paneCloseRequested(BlackCore::TerminalView *terminal);
    void paneActivated(BlackCore::PaneFrame *frame);

private:
    void attachFrame(PaneFrame *frame);
    void onFrameActivated(PaneFrame *frame);
    void collapseSplitter(QSplitter *splitter);
    QSplitter *splitterFor(QWidget *child) const;
    //! Распределить место корневого сплиттера между детьми
    void distributeRootEvenly();
    void collectSplitters(QWidget *root, QList<QSplitter *> &out) const;

    QSplitter *m_root = nullptr;
    PaneFrame *m_activeFrame = nullptr;
    bool m_restoring = false;
};

} // namespace BlackCore
