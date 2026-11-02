// SPDX-License-Identifier: MIT
#include "Project.h"

#include "GitInfo.h"
#include "Logger.h"
#include "Workspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>

namespace BlackCore {
namespace {

constexpr const char *kProjectsKey = "projects/list";

bool fileExists(const QString &directory, const QString &name)
{
    return QFileInfo(directory + QLatin1Char('/') + name).isFile();
}

} // namespace

// ---------------------------------------------------------------------------
// Стартовая команда
// ---------------------------------------------------------------------------

QJsonObject ProjectCommand::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("command"), command);
    object.insert(QStringLiteral("needsConfirmation"), needsConfirmation);
    return object;
}

ProjectCommand ProjectCommand::fromJson(const QJsonObject &object)
{
    ProjectCommand command;
    command.name = object.value(QStringLiteral("name")).toString();
    command.command = object.value(QStringLiteral("command")).toString();
    command.needsConfirmation = object.value(QStringLiteral("needsConfirmation")).toBool(true);
    return command;
}

// ---------------------------------------------------------------------------
// Проект
// ---------------------------------------------------------------------------

QJsonObject Project::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("favorite"), favorite);
    if (layout)
        object.insert(QStringLiteral("layout"), layout->toJson());

    QJsonArray commandArray;
    for (const ProjectCommand &command : commands)
        commandArray.append(command.toJson());
    object.insert(QStringLiteral("commands"), commandArray);
    return object;
}

Project Project::fromJson(const QJsonObject &object)
{
    Project project;
    project.id = object.value(QStringLiteral("id")).toString();
    project.name = object.value(QStringLiteral("name")).toString();
    project.path = object.value(QStringLiteral("path")).toString();
    project.description = object.value(QStringLiteral("description")).toString();
    project.favorite = object.value(QStringLiteral("favorite")).toBool(true);

    const QJsonObject layoutObject = object.value(QStringLiteral("layout")).toObject();
    if (!layoutObject.isEmpty())
        project.layout = LayoutNode::fromJson(layoutObject);

    for (const QJsonValue &value : object.value(QStringLiteral("commands")).toArray()) {
        if (!value.isObject())
            continue;
        const ProjectCommand command = ProjectCommand::fromJson(value.toObject());
        if (!command.command.trimmed().isEmpty())
            project.commands.append(command);
    }

    if (project.id.isEmpty())
        project.id = ProjectManager::makeId(project.name);
    return project;
}

// ---------------------------------------------------------------------------
// Хранилище
// ---------------------------------------------------------------------------

QList<Project> ProjectManager::projects()
{
    QSettings store;
    const QString raw = store.value(QLatin1String(kProjectsKey)).toString();
    if (raw.trimmed().isEmpty())
        return {};

    const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8());
    if (!document.isArray()) {
        Log::warn(QStringLiteral("Список проектов повреждён — раздел проигнорирован"));
        return {};
    }

    QList<Project> list;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        const Project project = Project::fromJson(value.toObject());
        if (project.isValid())
            list.append(project);
    }
    return list;
}

QList<Project> ProjectManager::favorites()
{
    QList<Project> result;
    for (const Project &project : projects()) {
        if (project.favorite)
            result.append(project);
    }
    return result;
}

Project ProjectManager::project(const QString &id)
{
    for (const Project &candidate : projects()) {
        if (candidate.id == id)
            return candidate;
    }
    return {};
}

Project ProjectManager::projectForPath(const QString &directory)
{
    if (directory.isEmpty())
        return {};

    const QString absolute = QFileInfo(directory).absoluteFilePath();
    Project best;
    int bestLength = 0;
    for (const Project &candidate : projects()) {
        const QString path = QFileInfo(candidate.path).absoluteFilePath();
        if (path.isEmpty())
            continue;
        if (absolute == path || absolute.startsWith(path + QLatin1Char('/'))) {
            if (path.size() > bestLength) {
                best = candidate;
                bestLength = path.size();
            }
        }
    }
    return best;
}

void ProjectManager::saveProject(const Project &project)
{
    QList<Project> list = projects();
    Project stored = project;
    if (stored.id.isEmpty())
        stored.id = makeId(stored.name);

    bool replaced = false;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id == stored.id) {
            list[i] = stored;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        list.append(stored);

    QJsonArray array;
    for (const Project &item : list)
        array.append(item.toJson());

    QSettings store;
    store.setValue(QLatin1String(kProjectsKey),
                   QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    store.sync();
}

void ProjectManager::removeProject(const QString &id)
{
    QJsonArray array;
    for (const Project &item : projects()) {
        if (item.id != id)
            array.append(item.toJson());
    }

    QSettings store;
    store.setValue(QLatin1String(kProjectsKey),
                   QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    store.sync();
}

QString ProjectManager::makeId(const QString &name)
{
    static const QRegularExpression latin(QStringLiteral("[^A-Za-z0-9]+"));
    QString slug = name.toLower();
    slug.replace(latin, QStringLiteral("-"));
    slug = slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (slug.isEmpty())
        slug = QStringLiteral("project");
    return QStringLiteral("project-%1").arg(slug.left(40));
}

QString ProjectManager::detectStack(const QString &directory)
{
    const QDir dir(directory);
    if (!dir.exists())
        return QString();

    if (fileExists(directory, QStringLiteral("composer.json")))
        return QStringLiteral("php");
    if (fileExists(directory, QStringLiteral("package.json")))
        return QStringLiteral("node");
    if (fileExists(directory, QStringLiteral("pyproject.toml"))
        || fileExists(directory, QStringLiteral("requirements.txt"))
        || fileExists(directory, QStringLiteral("setup.py"))
        || fileExists(directory, QStringLiteral("manage.py")))
        return QStringLiteral("python");
    if (!GitService::findRepositoryRoot(directory).isEmpty())
        return QStringLiteral("git");
    return QString();
}

QList<ProjectCommand> ProjectManager::suggestedCommands(const QString &directory)
{
    // Подсказки только предлагаются: BlackCore ничего не устанавливает и не запускает сам
    QList<ProjectCommand> commands;
    const QString stack = detectStack(directory);

    if (fileExists(directory, QStringLiteral("composer.json"))) {
        commands.append({QStringLiteral("PHP: composer install"),
                         QStringLiteral("composer install"), true});
        commands.append({QStringLiteral("PHP: встроенный сервер"),
                         QStringLiteral("php -S 127.0.0.1:8000"), true});
        if (fileExists(directory, QStringLiteral("artisan")))
            commands.append({QStringLiteral("Laravel: php artisan serve"),
                             QStringLiteral("php artisan serve"), true});
    }

    if (fileExists(directory, QStringLiteral("manage.py"))) {
        commands.append({QStringLiteral("Django: миграции"),
                         QStringLiteral("python3 manage.py migrate"), true});
        commands.append({QStringLiteral("Django: сервер"),
                         QStringLiteral("python3 manage.py runserver"), true});
    }

    if (fileExists(directory, QStringLiteral("requirements.txt"))) {
        commands.append({QStringLiteral("Python: venv + зависимости"),
                         QStringLiteral("python3 -m venv .venv && . .venv/bin/activate && "
                                        "pip install -r requirements.txt"),
                         true});
    } else if (stack == QStringLiteral("python")) {
        commands.append({QStringLiteral("Python: тесты"),
                         QStringLiteral("python3 -m pytest -q"), true});
    }

    if (fileExists(directory, QStringLiteral("package.json"))) {
        commands.append({QStringLiteral("Node: npm install"), QStringLiteral("npm install"), true});
        commands.append({QStringLiteral("Node: npm run dev"), QStringLiteral("npm run dev"), true});
        commands.append({QStringLiteral("Node: npm test"), QStringLiteral("npm test"), true});
    }

    if (!GitService::findRepositoryRoot(directory).isEmpty()) {
        commands.append({QStringLiteral("Git: статус"), QStringLiteral("git status"), false});
        commands.append({QStringLiteral("Git: история"), 
                         QStringLiteral("git log --oneline --graph -20"), false});
    }

    return commands;
}

QString ProjectManager::validate(const Project &project)
{
    if (project.name.trimmed().isEmpty())
        return QStringLiteral("не указано имя проекта");
    if (project.path.trimmed().isEmpty())
        return QStringLiteral("не указан каталог проекта");
    const QFileInfo info(project.path);
    if (!info.exists())
        return QStringLiteral("каталог проекта не найден: %1").arg(project.path);
    if (!info.isDir())
        return QStringLiteral("путь проекта не является каталогом");
    return QString();
}

QSharedPointer<LayoutNode> ProjectManager::defaultLayout(const QString &path)
{
    PaneSpec shell;
    shell.title = QStringLiteral("Терминал");
    shell.workingDirectory = path;

    auto root = LayoutNode::makePane(shell);

    if (!GitService::findRepositoryRoot(path).isEmpty()) {
        PaneSpec gitPane;
        gitPane.title = QStringLiteral("Git");
        gitPane.workingDirectory = path;
        gitPane.command = QStringLiteral("git status -sb");
        gitPane.runCommand = true;
        root = LayoutNode::makeSplit(Qt::Vertical, {LayoutNode::makePane(shell),
                                                    LayoutNode::makePane(gitPane)},
                                     {70, 30});
    }

    return root;
}

} // namespace BlackCore
