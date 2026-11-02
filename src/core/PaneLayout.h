// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QList>
#include <QSharedPointer>
#include <QString>

namespace BlackCore {

//! Описание одной панели (листа раскладки).
struct PaneSpec
{
    QString title;                //!< имя панели (для заголовка и восстановления)
    QString workingDirectory;     //!< рабочий каталог; пусто — каталог по умолчанию
    QString profileId;            //!< профиль терминала; пусто — «по умолчанию»
    QString command;              //!< команда для запуска (только у шаблонов)
    bool runCommand = false;      //!< выполнять команду сразу (шаблоны рабочих пространств)

    PaneSpec() = default;
    static PaneSpec withDirectory(const QString &directory, const QString &title = QString());
};

//! Дерево раскладки: лист (панель) или сплит с двумя и более детьми.
class LayoutNode
{
public:
    enum class Type { Pane, Split };

    LayoutNode() = default;

    static QSharedPointer<LayoutNode> makePane(const PaneSpec &spec);
    static QSharedPointer<LayoutNode> makeSplit(Qt::Orientation orientation,
                                               const QList<QSharedPointer<LayoutNode>> &children,
                                               const QList<int> &sizes = {});

    Type type() const { return m_type; }
    bool isPane() const { return m_type == Type::Pane; }
    bool isSplit() const { return m_type == Type::Split; }

    const PaneSpec &pane() const { return m_pane; }
    void setPane(const PaneSpec &spec) { m_pane = spec; }

    Qt::Orientation orientation() const { return m_orientation; }
    const QList<QSharedPointer<LayoutNode>> &children() const { return m_children; }
    const QList<int> &sizes() const { return m_sizes; }

    //! Все панели дерева в порядке обхода слева-направо / сверху-вниз
    QList<PaneSpec> panes() const;
    int paneCount() const;

    //! Первая панель (удобно для шаблонов с одной панелью)
    PaneSpec firstPane() const;

    QJsonObject toJson() const;
    static QSharedPointer<LayoutNode> fromJson(const QJsonObject &object);

    //! Проверка, что дерево корректно (все листья — панели, у сплита ≥ 2 детей)
    bool isValid() const;

private:
    Type m_type = Type::Pane;
    PaneSpec m_pane;
    Qt::Orientation m_orientation = Qt::Horizontal;
    QList<QSharedPointer<LayoutNode>> m_children;
    QList<int> m_sizes;
};

} // namespace BlackCore
