// SPDX-License-Identifier: MIT
#pragma once

#include "PaneLayout.h"

#include <QList>
#include <QString>

namespace BlackCore {

//! Рабочее пространство: именованная раскладка панелей.
struct Workspace
{
    QString id;
    QString name;
    QString description;
    bool builtin = false;
    QSharedPointer<LayoutNode> layout;

    PaneSpec firstPane() const { return layout ? layout->firstPane() : PaneSpec(); }
    int paneCount() const { return layout ? layout->paneCount() : 0; }
};

//! Политика запуска команд: защита от повторного выполнения опасных действий.
class CommandPolicy
{
public:
    //! Команда считается безопасной, если она только читает состояние системы
    static bool isSafe(const QString &command);

    //! Разрешённые префиксы (для документации и тестов)
    static QStringList allowedPrefixes();
};

//! Реестр рабочих пространств: встроенные + сохранённые пользователем.
class WorkspaceManager
{
public:
    //! Встроенные шаблоны («Разработка», «Система») + пользовательские
    static QList<Workspace> all();
    static QList<Workspace> builtin();
    static QList<Workspace> userWorkspaces();

    static Workspace workspace(const QString &id);

    static void saveUser(const Workspace &workspace);
    static void removeUser(const QString &id);

    //! Уникальный идентификатор для пользовательского пространства
    static QString makeId(const QString &name);

    //! Раскладка «как сейчас» из набора панелей (используется «Сохранить как…»)
    static Workspace fromCurrent(const QString &name, const QString &description,
                                 const QSharedPointer<LayoutNode> &layout);
};

} // namespace BlackCore
