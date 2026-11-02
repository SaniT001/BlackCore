// SPDX-License-Identifier: MIT
#include "PaneLayout.h"

#include <QJsonArray>
#include <QVariant>

namespace BlackCore {

PaneSpec PaneSpec::withDirectory(const QString &directory, const QString &title)
{
    PaneSpec spec;
    spec.workingDirectory = directory;
    spec.title = title;
    return spec;
}

QSharedPointer<LayoutNode> LayoutNode::makePane(const PaneSpec &spec)
{
    auto node = QSharedPointer<LayoutNode>::create();
    node->m_type = Type::Pane;
    node->m_pane = spec;
    return node;
}

QSharedPointer<LayoutNode> LayoutNode::makeSplit(Qt::Orientation orientation,
                                                 const QList<QSharedPointer<LayoutNode>> &children,
                                                 const QList<int> &sizes)
{
    auto node = QSharedPointer<LayoutNode>::create();
    node->m_type = Type::Split;
    node->m_orientation = orientation;
    node->m_children = children;
    node->m_sizes = sizes;
    return node;
}

QList<PaneSpec> LayoutNode::panes() const
{
    if (isPane())
        return {m_pane};

    QList<PaneSpec> result;
    for (const auto &child : m_children)
        result.append(child ? child->panes() : QList<PaneSpec>());
    return result;
}

int LayoutNode::paneCount() const
{
    if (isPane())
        return 1;
    int total = 0;
    for (const auto &child : m_children)
        total += child ? child->paneCount() : 0;
    return total;
}

PaneSpec LayoutNode::firstPane() const
{
    if (isPane())
        return m_pane;
    for (const auto &child : m_children) {
        if (child) {
            const PaneSpec spec = child->firstPane();
            if (!spec.title.isEmpty() || !spec.workingDirectory.isEmpty() || !spec.command.isEmpty())
                return spec;
            return spec;
        }
    }
    return {};
}

bool LayoutNode::isValid() const
{
    if (isPane())
        return true;
    if (m_children.size() < 2)
        return false;
    for (const auto &child : m_children) {
        if (!child || !child->isValid())
            return false;
    }
    return true;
}

QJsonObject LayoutNode::toJson() const
{
    QJsonObject object;
    if (isPane()) {
        object.insert(QStringLiteral("type"), QStringLiteral("pane"));
        object.insert(QStringLiteral("title"), m_pane.title);
        object.insert(QStringLiteral("cwd"), m_pane.workingDirectory);
        object.insert(QStringLiteral("profile"), m_pane.profileId);
        return object;
    }

    object.insert(QStringLiteral("type"), QStringLiteral("split"));
    object.insert(QStringLiteral("orientation"),
                  m_orientation == Qt::Horizontal ? QStringLiteral("horizontal")
                                                  : QStringLiteral("vertical"));

    QJsonArray childrenArray;
    for (const auto &child : m_children) {
        if (child)
            childrenArray.append(child->toJson());
    }
    object.insert(QStringLiteral("children"), childrenArray);

    if (!m_sizes.isEmpty()) {
        QJsonArray sizesArray;
        for (const int size : m_sizes)
            sizesArray.append(size);
        object.insert(QStringLiteral("sizes"), sizesArray);
    }
    return object;
}

QSharedPointer<LayoutNode> LayoutNode::fromJson(const QJsonObject &object)
{
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("split")) {
        const QString orientation = object.value(QStringLiteral("orientation")).toString();
        QList<QSharedPointer<LayoutNode>> children;
        const QJsonArray childrenArray = object.value(QStringLiteral("children")).toArray();
        for (const QJsonValue &value : childrenArray) {
            auto child = fromJson(value.toObject());
            if (child)
                children.append(child);
        }

        QList<int> sizes;
        const QJsonArray sizesArray = object.value(QStringLiteral("sizes")).toArray();
        for (const QJsonValue &value : sizesArray)
            sizes.append(value.toInt());

        if (children.size() < 2) {
            // Повреждённый сплит схлопываем в единственного ребёнка или в пустую панель
            if (children.size() == 1)
                return children.first();
            return makePane(PaneSpec());
        }

        return makeSplit(orientation == QLatin1String("vertical") ? Qt::Vertical : Qt::Horizontal,
                         children, sizes);
    }

    PaneSpec spec;
    spec.title = object.value(QStringLiteral("title")).toString();
    spec.workingDirectory = object.value(QStringLiteral("cwd")).toString();
    spec.profileId = object.value(QStringLiteral("profile")).toString();
    return makePane(spec);
}

} // namespace BlackCore
