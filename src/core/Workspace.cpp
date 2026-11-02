// SPDX-License-Identifier: MIT
#include "Workspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

namespace BlackCore {
namespace {

constexpr const char *kWorkspacesKey = "workspaces/list";

QString tr(const char *text)
{
    return QCoreApplication::translate("Workspace", text);
}

PaneSpec pane(const QString &title, const QString &directory, const QString &command)
{
    PaneSpec spec;
    spec.title = title;
    spec.workingDirectory = directory;
    spec.command = command;
    spec.runCommand = !command.isEmpty();
    return spec;
}

Workspace makeWorkspace(const QString &id, const QString &name, const QString &description,
                        const QSharedPointer<LayoutNode> &layout)
{
    Workspace workspace;
    workspace.id = id;
    workspace.name = name;
    workspace.description = description;
    workspace.builtin = true;
    workspace.layout = layout;
    return workspace;
}

QJsonObject workspaceToJson(const Workspace &workspace)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), workspace.id);
    object.insert(QStringLiteral("name"), workspace.name);
    object.insert(QStringLiteral("description"), workspace.description);
    object.insert(QStringLiteral("layout"), workspace.layout ? workspace.layout->toJson() : QJsonObject());
    return object;
}

Workspace workspaceFromJson(const QJsonObject &object)
{
    Workspace workspace;
    workspace.id = object.value(QStringLiteral("id")).toString();
    workspace.name = object.value(QStringLiteral("name")).toString();
    workspace.description = object.value(QStringLiteral("description")).toString();
    workspace.builtin = false;
    workspace.layout = LayoutNode::fromJson(object.value(QStringLiteral("layout")).toObject());
    return workspace;
}

} // namespace

QStringList CommandPolicy::allowedPrefixes()
{
    // Только «читающие» команды: они не меняют систему и не удаляют данные
    return {
        QStringLiteral("git status"), QStringLiteral("git log"), QStringLiteral("git branch"),
        QStringLiteral("git diff"), QStringLiteral("git remote"),
        QStringLiteral("ls"), QStringLiteral("pwd"), QStringLiteral("tree"),
        QStringLiteral("top"), QStringLiteral("htop"), QStringLiteral("btop"),
        QStringLiteral("journalctl"), QStringLiteral("tail -f"), QStringLiteral("tail -n"),
        QStringLiteral("ss"), QStringLiteral("ip a"), QStringLiteral("ip addr"),
        QStringLiteral("networkctl"), QStringLiteral("nmcli"),
        QStringLiteral("systemctl status"), QStringLiteral("dmesg"), QStringLiteral("free"),
        QStringLiteral("df"), QStringLiteral("du"), QStringLiteral("uptime"),
        QStringLiteral("watch"), QStringLiteral("uname"), QStringLiteral("lscpu"),
        QStringLiteral("lsblk"), QStringLiteral("sensors"),
    };
}

bool CommandPolicy::isSafe(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
        return false;

    // Составные команды, перенаправления и подстановки — вне политики:
    // их нельзя гарантированно считать безопасными
    static const QStringList forbidden{QStringLiteral(";"), QStringLiteral("&&"), QStringLiteral("||"),
                                       QStringLiteral("|"), QStringLiteral(">"), QStringLiteral("<"),
                                       QStringLiteral("$("), QStringLiteral("`"), QStringLiteral("&")};
    for (const QString &token : forbidden) {
        if (token == QLatin1String("|") && trimmed.startsWith(QLatin1Char('|')))
            continue;
        if (trimmed.contains(token)) {
            // «tail -f file | grep …» тоже не выполняем автоматически
            return false;
        }
    }

    for (const QString &prefix : allowedPrefixes()) {
        if (trimmed == prefix || trimmed.startsWith(prefix + QLatin1Char(' ')))
            return true;
    }
    return false;
}

QList<Workspace> WorkspaceManager::builtin()
{
    QList<Workspace> result;

    // --- «Разработка»: код и Git | запуск проекта | логи ---
    {
        const QString projects = []() {
            const QString candidate = QDir::homePath() + QStringLiteral("/projects");
            return QFileInfo(candidate).isDir() ? candidate : QDir::homePath();
        }();

        const auto left = LayoutNode::makePane(pane(tr("Код и Git"), projects,
                                                    QStringLiteral("git status -sb")));
        const auto middle = LayoutNode::makePane(pane(tr("Запуск проекта"), projects, QString()));
        const auto right = LayoutNode::makePane(pane(tr("Логи"), projects,
                                                     QStringLiteral("journalctl --user -f -n 40")));

        auto layout = LayoutNode::makeSplit(Qt::Horizontal, {left, middle, right}, {50, 25, 25});
        result.append(makeWorkspace(QStringLiteral("builtin.dev"),
                                    tr("Разработка"),
                                    tr("Код и Git · запуск проекта · просмотр логов"),
                                    layout));
    }

    // --- «Система»: диагностика | процессы | сеть ---
    {
        const auto left = LayoutNode::makePane(pane(tr("Диагностика"), QDir::homePath(),
                                                    QStringLiteral("dmesg --level=err,warn -T")));
        const auto middle = LayoutNode::makePane(pane(tr("Процессы"), QDir::homePath(),
                                                      QStringLiteral("btop")));
        const auto right = LayoutNode::makePane(pane(tr("Сеть"), QDir::homePath(),
                                                     QStringLiteral("ss -tulpn")));

        auto layout = LayoutNode::makeSplit(Qt::Horizontal, {left, middle, right}, {34, 33, 33});
        result.append(makeWorkspace(QStringLiteral("builtin.system"),
                                    tr("Система"),
                                    tr("Диагностика · процессы · сеть"),
                                    layout));
    }

    // --- «Сравнение»: две панели вертикально ---
    {
        const auto top = LayoutNode::makePane(pane(tr("Верхняя панель"), QDir::homePath(), QString()));
        const auto bottom = LayoutNode::makePane(pane(tr("Нижняя панель"), QDir::homePath(), QString()));
        auto layout = LayoutNode::makeSplit(Qt::Vertical, {top, bottom}, {50, 50});
        result.append(makeWorkspace(QStringLiteral("builtin.pair"),
                                    tr("Две панели"),
                                    tr("Вертикальное разделение: вывод и ввод рядом"),
                                    layout));
    }

    return result;
}

QList<Workspace> WorkspaceManager::userWorkspaces()
{
    QSettings store;
    const QJsonArray array = QJsonArray::fromVariantList(
        store.value(QLatin1String(kWorkspacesKey)).toList());

    QList<Workspace> result;
    for (const QJsonValue &value : array) {
        const Workspace workspace = workspaceFromJson(value.toObject());
        if (!workspace.id.isEmpty() && workspace.layout && workspace.layout->isValid())
            result.append(workspace);
    }
    return result;
}

QList<Workspace> WorkspaceManager::all()
{
    QList<Workspace> result = builtin();
    result.append(userWorkspaces());
    return result;
}

Workspace WorkspaceManager::workspace(const QString &id)
{
    for (const Workspace &workspace : all()) {
        if (workspace.id == id)
            return workspace;
    }
    return {};
}

void WorkspaceManager::saveUser(const Workspace &workspace)
{
    if (workspace.id.isEmpty() || workspace.builtin || !workspace.layout)
        return;

    QVariantList storage;
    bool replaced = false;
    for (const Workspace &existing : userWorkspaces()) {
        if (existing.id == workspace.id) {
            storage.append(workspaceToJson(workspace).toVariantMap());
            replaced = true;
        } else {
            storage.append(workspaceToJson(existing).toVariantMap());
        }
    }
    if (!replaced)
        storage.append(workspaceToJson(workspace).toVariantMap());

    QSettings store;
    store.setValue(QLatin1String(kWorkspacesKey), storage);
    store.sync();
}

void WorkspaceManager::removeUser(const QString &id)
{
    QVariantList storage;
    for (const Workspace &existing : userWorkspaces()) {
        if (existing.id != id)
            storage.append(workspaceToJson(existing).toVariantMap());
    }

    QSettings store;
    store.setValue(QLatin1String(kWorkspacesKey), storage);
    store.sync();
}

QString WorkspaceManager::makeId(const QString &name)
{
    const QString slug = name.toLower().simplified().replace(QLatin1Char(' '), QLatin1Char('-'));
    return QStringLiteral("user.%1.%2")
        .arg(slug, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
}

Workspace WorkspaceManager::fromCurrent(const QString &name, const QString &description,
                                        const QSharedPointer<LayoutNode> &layout)
{
    Workspace workspace;
    workspace.id = makeId(name);
    workspace.name = name;
    workspace.description = description;
    workspace.builtin = false;
    workspace.layout = layout;
    return workspace;
}

} // namespace BlackCore
