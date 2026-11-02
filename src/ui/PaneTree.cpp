// SPDX-License-Identifier: MIT
#include "PaneTree.h"

#include "../core/Logger.h"
#include "../terminal/TerminalView.h"
#include "PaneFrame.h"

#include <QLayout>
#include <QSplitter>
#include <QVBoxLayout>

namespace BlackCore {
namespace {

//! QSplitter с отложенными пропорциями: setSizes() применяется, когда у
//! сплиттера уже есть настоящая геометрия. Пока места мало, минимальные
//! размеры панелей искажают пропорции — тогда запрос сохраняется и
//! повторяется при следующем изменении размера.
class ProportionsSplitter : public QSplitter
{
public:
    explicit ProportionsSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent)
    {
    }

    void setProportions(const QList<int> &sizes)
    {
        m_pending = sizes;
        applyPending();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QSplitter::resizeEvent(event);
        applyPending();
    }

private:
    int extent() const
    {
        return orientation() == Qt::Horizontal ? width() : height();
    }

    bool proportionsMatch(const QList<int> &requested) const
    {
        const QList<int> actual = QSplitter::sizes();
        if (actual.size() != requested.size())
            return false;

        int requestedSum = 0;
        int actualSum = 0;
        for (int i = 0; i < requested.size(); ++i) {
            requestedSum += requested.at(i);
            actualSum += actual.at(i);
        }
        if (requestedSum <= 0 || actualSum <= 0)
            return false;

        for (int i = 0; i < requested.size(); ++i) {
            const double want = double(requested.at(i)) / requestedSum;
            const double got = double(actual.at(i)) / actualSum;
            if (qAbs(want - got) > 0.08)
                return false;
        }
        return true;
    }

    void applyPending()
    {
        if (count() == 0 || extent() <= 0)
            return;

        if (!m_pending.isEmpty()) {
            // Пропорции переводим в абсолютные пиксели: QSplitter не масштабирует
            // маленькие суммы, а «прилипает» к минимальным размерам панелей.
            int sum = 0;
            for (int value : m_pending)
                sum += value;
            if (sum <= 0)
                return;

            const int total = extent();
            QList<int> absolute;
            absolute.reserve(m_pending.size());
            for (int value : m_pending)
                absolute.append(qMax(1, total * value / sum));

            QSplitter::setSizes(absolute);
            if (proportionsMatch(absolute))
                m_pending.clear();
            return;
        }

        // Вырожденный случай: сплиттер создавался без геометрии, все дети
        // нулевой ширины — распределяем место поровну.
        bool hasExtent = false;
        for (int value : QSplitter::sizes()) {
            if (value > 0) {
                hasExtent = true;
                break;
            }
        }
        if (hasExtent)
            return;

        QList<int> equal;
        for (int i = 0; i < count(); ++i)
            equal.append(100);
        QSplitter::setSizes(equal);
    }

    QList<int> m_pending;
};

} // namespace

PaneTree::PaneTree(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_root = new ProportionsSplitter(Qt::Horizontal, this);
    m_root->setChildrenCollapsible(false);
    m_root->setHandleWidth(3);
    m_root->setOpaqueResize(true);
    layout->addWidget(m_root);

    connect(m_root, &QSplitter::splitterMoved, this, [this]() { emit layoutChanged(); });
}

PaneTree::~PaneTree() = default;

void PaneTree::attachFrame(PaneFrame *frame)
{
    connect(frame, &PaneFrame::activated, this, &PaneTree::onFrameActivated);
    connect(frame, &PaneFrame::closeRequested, this, [this](PaneFrame *paneFrame) {
        emit paneCloseRequested(paneFrame->terminal());
    });
    connect(frame->terminal(), &TerminalView::focusChanged, this, [this, frame](bool focused) {
        if (focused)
            onFrameActivated(frame);
    });
}

void PaneTree::onFrameActivated(PaneFrame *frame)
{
    if (!frame || frame == m_activeFrame) {
        emit paneActivated(frame);
        return;
    }
    setActiveFrame(frame);
    emit paneActivated(frame);
    emit activePaneChanged(frame->terminal());
}

PaneFrame *PaneTree::addPane(const PaneSpec &spec, bool startNow)
{
    Q_UNUSED(startNow);

    auto *frame = new PaneFrame(spec, this);
    attachFrame(frame);
    m_root->addWidget(frame);
    distributeRootEvenly();

    m_activeFrame = frame;
    frame->setActive(true);
    updateHeaderVisibility();

    emit paneAdded(frame->terminal());
    emit layoutChanged();
    emit activePaneChanged(frame->terminal());
    return frame;
}

void PaneTree::distributeRootEvenly()
{
    auto *proportional = dynamic_cast<ProportionsSplitter *>(m_root);
    if (!proportional)
        return;
    QList<int> sizes;
    for (int i = 0; i < m_root->count(); ++i)
        sizes.append(100);
    proportional->setProportions(sizes);
}

QSplitter *PaneTree::splitterFor(QWidget *child) const
{
    if (!child)
        return nullptr;
    return qobject_cast<QSplitter *>(child->parentWidget());
}

PaneFrame *PaneTree::split(TerminalView *target, Qt::Orientation orientation, const PaneSpec &spec,
                           bool startNow)
{
    Q_UNUSED(startNow);

    PaneFrame *targetFrame = nullptr;
    for (PaneFrame *frame : frames()) {
        if (frame->terminal() == target) {
            targetFrame = frame;
            break;
        }
    }
    if (!targetFrame)
        targetFrame = m_activeFrame ? m_activeFrame : frames().value(0);
    if (!targetFrame)
        return addPane(spec, startNow);

    QSplitter *parentSplitter = splitterFor(targetFrame);
    auto *newFrame = new PaneFrame(spec, this);
    attachFrame(newFrame);

    if (parentSplitter && parentSplitter->orientation() == orientation) {
        // Одно направление — просто добавляем соседом
        const int index = parentSplitter->indexOf(targetFrame);
        parentSplitter->insertWidget(index + 1, newFrame);
        const int count = parentSplitter->count();
        QList<int> sizes;
        sizes.reserve(count);
        for (int i = 0; i < count; ++i)
            sizes.append(100);
        if (auto *proportional = dynamic_cast<ProportionsSplitter *>(parentSplitter))
            proportional->setProportions(sizes);
        else
            parentSplitter->setSizes(sizes);
    } else if (parentSplitter) {
        // Меняем направление: оборачиваем текущую панель в новый сплиттер
        const int index = parentSplitter->indexOf(targetFrame);
        auto *nested = new ProportionsSplitter(orientation, parentSplitter);
        nested->setChildrenCollapsible(false);
        nested->setHandleWidth(3);
        nested->setOpaqueResize(true);

        targetFrame->setParent(nested);
        nested->addWidget(targetFrame);
        nested->addWidget(newFrame);
        parentSplitter->insertWidget(index, nested);

        if (auto *proportional = dynamic_cast<ProportionsSplitter *>(nested))
            proportional->setProportions({50, 50});
        connect(nested, &QSplitter::splitterMoved, this, [this]() { emit layoutChanged(); });
    } else {
        m_root->addWidget(newFrame);
        distributeRootEvenly();
    }

    m_activeFrame = newFrame;
    newFrame->setActive(true);
    for (PaneFrame *frame : frames()) {
        if (frame != newFrame)
            frame->setActive(false);
    }

    updateHeaderVisibility();
    newFrame->terminal()->setFocus();

    emit paneAdded(newFrame->terminal());
    emit layoutChanged();
    emit activePaneChanged(newFrame->terminal());
    return newFrame;
}

void PaneTree::collapseSplitter(QSplitter *splitter)
{
    if (!splitter)
        return;

    // Пустой сплиттер удаляем, иначе в раскладке останется битый узел
    if (splitter->count() == 0 && splitter != m_root) {
        QSplitter *parentSplitter = qobject_cast<QSplitter *>(splitter->parentWidget());
        splitter->setParent(nullptr);
        splitter->deleteLater();
        if (parentSplitter)
            collapseSplitter(parentSplitter);
        return;
    }

    if (splitter == m_root)
        return;
    if (splitter->count() != 1)
        return;

    QWidget *child = splitter->widget(0);
    QSplitter *parent = qobject_cast<QSplitter *>(splitter->parentWidget());
    if (!parent || !child)
        return;

    const int index = parent->indexOf(splitter);
    child->setParent(parent);
    parent->insertWidget(index, child);
    splitter->deleteLater();

    if (parent != m_root)
        collapseSplitter(parent);
}

bool PaneTree::closePane(TerminalView *terminal)
{
    PaneFrame *frame = nullptr;
    for (PaneFrame *candidate : frames()) {
        if (candidate->terminal() == terminal) {
            frame = candidate;
            break;
        }
    }
    if (!frame)
        return false;

    QSplitter *parent = splitterFor(frame);
    const int indexOfFrame = parent ? parent->indexOf(frame) : 0;

    if (m_activeFrame == frame)
        m_activeFrame = nullptr;

    frame->setParent(nullptr);
    frame->deleteLater();

    if (parent) {
        collapseSplitter(parent);
        // После схлопывания переносим фокус на соседнюю панель
        if (!m_activeFrame) {
            const QList<PaneFrame *> remaining = frames();
            if (!remaining.isEmpty()) {
                PaneFrame *next = remaining.value(qBound(0, indexOfFrame, remaining.size() - 1));
                m_activeFrame = next;
                next->setActive(true);
                next->terminal()->setFocus();
                emit activePaneChanged(next->terminal());
            }
        }
    }

    updateHeaderVisibility();
    emit paneRemoved();
    emit layoutChanged();
    return true;
}

QList<PaneFrame *> PaneTree::frames() const
{
    return findChildren<PaneFrame *>(Qt::FindChildrenRecursively);
}

QList<TerminalView *> PaneTree::terminals() const
{
    QList<TerminalView *> result;
    for (PaneFrame *frame : frames())
        result.append(frame->terminal());
    return result;
}

TerminalView *PaneTree::activeTerminal() const
{
    if (!m_activeFrame)
        return terminals().value(0, nullptr);
    return m_activeFrame->terminal();
}

PaneFrame *PaneTree::activeFrame() const
{
    if (!m_activeFrame)
        return frames().value(0, nullptr);
    return m_activeFrame;
}

void PaneTree::setActiveFrame(PaneFrame *frame)
{
    if (!frame)
        return;
    m_activeFrame = frame;
    for (PaneFrame *candidate : frames())
        candidate->setActive(candidate == frame);
}

void PaneTree::setActiveTerminal(TerminalView *terminal)
{
    for (PaneFrame *frame : frames()) {
        if (frame->terminal() == terminal) {
            setActiveFrame(frame);
            frame->terminal()->setFocus();
            return;
        }
    }
}

void PaneTree::focusNextPane(int delta)
{
    QList<PaneFrame *> all = frames();
    if (all.size() < 2)
        return;

    int index = all.indexOf(activeFrame());
    if (index < 0)
        index = 0;
    index = (index + delta) % all.size();
    if (index < 0)
        index += all.size();

    PaneFrame *next = all.at(index);
    setActiveFrame(next);
    next->terminal()->setFocus();
    emit activePaneChanged(next->terminal());
}

void PaneTree::focusPane(int index)
{
    const QList<PaneFrame *> all = frames();
    if (index < 0 || index >= all.size())
        return;
    PaneFrame *frame = all.at(index);
    setActiveFrame(frame);
    frame->terminal()->setFocus();
    emit activePaneChanged(frame->terminal());
}

void PaneTree::focusDirection(Qt::Edge direction)
{
    PaneFrame *current = activeFrame();
    const QList<PaneFrame *> all = frames();
    if (!current || all.size() < 2)
        return;

    // Геометрия панелей берётся относительно PaneTree: панели могут быть
    // вложены в разные сплиттеры, поэтому локальные координаты несравнимы.
    auto rectInTree = [this](PaneFrame *frame) {
        return QRect(frame->mapTo(this, QPoint(0, 0)), frame->size());
    };

    const QRect origin = rectInTree(current);
    const QPoint originCenter = origin.center();

    PaneFrame *best = nullptr;
    int bestDistance = 0;

    for (PaneFrame *candidate : all) {
        if (candidate == current)
            continue;

        const QRect rect = rectInTree(candidate);
        const QPoint center = rect.center();
        const int dx = center.x() - originCenter.x();
        const int dy = center.y() - originCenter.y();

        bool matches = false;
        switch (direction) {
        case Qt::LeftEdge:
            matches = dx < 0 && qAbs(dy) < rect.height();
            break;
        case Qt::RightEdge:
            matches = dx > 0 && qAbs(dy) < rect.height();
            break;
        case Qt::TopEdge:
            matches = dy < 0 && qAbs(dx) < rect.width();
            break;
        case Qt::BottomEdge:
            matches = dy > 0 && qAbs(dx) < rect.width();
            break;
        }
        if (!matches)
            continue;

        const int distance = qAbs(dx) + qAbs(dy);
        if (!best || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }

    if (!best)
        return;

    setActiveFrame(best);
    best->terminal()->setFocus();
    emit activePaneChanged(best->terminal());
}

int PaneTree::paneCount() const
{
    return frames().size();
}

void PaneTree::collectSplitters(QWidget *root, QList<QSplitter *> &out) const
{
    if (auto *splitter = qobject_cast<QSplitter *>(root))
        out.append(splitter);
    const QList<QWidget *> children = root->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children) {
        if (child != root)
            collectSplitters(child, out);
    }
}

QSharedPointer<LayoutNode> PaneTree::captureLayout() const
{
    const QList<PaneFrame *> all = frames();
    if (all.isEmpty())
        return LayoutNode::makePane(PaneSpec());

    std::function<QSharedPointer<LayoutNode>(QWidget *)> build =
        [&build](QWidget *widget) -> QSharedPointer<LayoutNode> {
            if (auto *frame = qobject_cast<PaneFrame *>(widget))
                return LayoutNode::makePane(frame->paneSpec());

            if (auto *splitter = qobject_cast<QSplitter *>(widget)) {
                QList<QSharedPointer<LayoutNode>> children;
                for (int i = 0; i < splitter->count(); ++i)
                    children.append(build(splitter->widget(i)));
                if (children.size() == 1)
                    return children.first();
                return LayoutNode::makeSplit(splitter->orientation(), children, splitter->sizes());
            }
            return QSharedPointer<LayoutNode>();
        };

    auto layout = build(m_root);
    if (!layout)
        return LayoutNode::makePane(all.first()->paneSpec());
    return layout;
}

QList<PaneFrame *> PaneTree::applyLayout(const LayoutNode &layout, bool startNow)
{
    Q_UNUSED(startNow);
    m_restoring = true;

    // Полная перестройка: старые панели удаляем
    for (PaneFrame *frame : frames()) {
        frame->setParent(nullptr);
        frame->deleteLater();
    }
    m_activeFrame = nullptr;
    while (m_root->count() > 0) {
        QWidget *widget = m_root->widget(0);
        widget->setParent(nullptr);
    }

    QList<PaneFrame *> created;

    std::function<QWidget *(const LayoutNode &)> build = [&](const LayoutNode &node) -> QWidget * {
        if (node.isPane()) {
            auto *frame = new PaneFrame(node.pane(), this);
            attachFrame(frame);
            created.append(frame);
            return frame;
        }

        auto *splitter = new ProportionsSplitter(node.orientation(), this);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(3);
        splitter->setOpaqueResize(true);
        for (const auto &child : node.children()) {
            if (!child)
                continue;
            QWidget *childWidget = build(*child);
            if (childWidget)
                splitter->addWidget(childWidget);
        }
        // Пропорции задаём коэффициентами растяжения: сплиттер ещё не имеет
        // геометрии, и setSizes() с нулевой шириной оставил бы панели нулевыми.
        if (!node.sizes().isEmpty() && node.sizes().size() == splitter->count())
            splitter->setProportions(node.sizes());
        connect(splitter, &QSplitter::splitterMoved, this, [this]() { emit layoutChanged(); });
        return splitter;
    };

    QWidget *rootWidget = build(layout);
    if (rootWidget) {
        // Раскладку вставляем в корневой сплиттер целиком: вложенные сплиттеры
        // сохраняют структуру, а не «расплющиваются» в один ряд.
        rootWidget->setParent(m_root);
        m_root->addWidget(rootWidget);
        // Виджеты строились до появления геометрии и остались скрытыми —
        // QSplitter не учитывает скрытые виджеты при распределении места.
        rootWidget->show();
        for (QSplitter *splitter : rootWidget->findChildren<QSplitter *>())
            splitter->show();
        for (PaneFrame *frame : rootWidget->findChildren<PaneFrame *>())
            frame->show();
        distributeRootEvenly();
    }
    if (!created.isEmpty()) {
        m_activeFrame = created.first();
        for (PaneFrame *frame : created)
            frame->setActive(frame == m_activeFrame);
    }

    m_restoring = false;
    updateHeaderVisibility();
    emit layoutChanged();

    for (PaneFrame *frame : created)
        emit paneAdded(frame->terminal());
    if (m_activeFrame)
        emit activePaneChanged(m_activeFrame->terminal());

    return created;
}

void PaneTree::updateHeaderVisibility()
{
    if (m_restoring)
        return;
    const bool showHeaders = paneCount() > 1;
    for (PaneFrame *frame : frames())
        frame->setHeaderVisible(showHeaders);
}

} // namespace BlackCore
