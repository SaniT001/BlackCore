// SPDX-License-Identifier: MIT
#pragma once

#include "PaneLayout.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace BlackCore {

//! Стартовая команда проекта (выполняется только после подтверждения).
struct ProjectCommand
{
    QString name;                  //!< подпись кнопки («Запустить сервер»)
    QString command;               //!< команда оболочки
    bool needsConfirmation = true; //!< спрашивать перед запуском

    QJsonObject toJson() const;
    static ProjectCommand fromJson(const QJsonObject &object);
};

//! Проект: избранный каталог + необязательная раскладка + стартовые команды.
struct Project
{
    QString id;
    QString name;
    QString path;                  //!< каталог проекта
    QString description;
    bool favorite = true;
    QSharedPointer<LayoutNode> layout;    //!< рабочее пространство проекта
    QList<ProjectCommand> commands;       //!< сохранённые стартовые команды

    bool isValid() const { return !id.isEmpty() && !name.trimmed().isEmpty(); }

    QJsonObject toJson() const;
    static Project fromJson(const QJsonObject &object);
};

//! Хранилище проектов.
class ProjectManager
{
public:
    static QList<Project> projects();
    static QList<Project> favorites();
    static Project project(const QString &id);
    //! Проект, в который входит каталог (или пустая структура)
    static Project projectForPath(const QString &directory);

    static void saveProject(const Project &project);
    static void removeProject(const QString &id);
    static QString makeId(const QString &name);

    //! Заготовки команд для распространённых стеков (без установки инструментов)
    static QList<ProjectCommand> suggestedCommands(const QString &directory);
    //! Стек, определённый по файлам проекта: php / python / node / git
    static QString detectStack(const QString &directory);

    //! Проверка пути проекта
    static QString validate(const Project &project);

    //! Раскладка по умолчанию для проекта: терминал + (если есть git) панель логов
    static QSharedPointer<LayoutNode> defaultLayout(const QString &path);
};

} // namespace BlackCore
