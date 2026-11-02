// SPDX-License-Identifier: MIT
#include "SessionStore.h"

#include "Logger.h"
#include "Profile.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <functional>

namespace BlackCore {
namespace {

constexpr int kFormatVersion = 1;

QString stateDirectory()
{
    // Один каталог состояния на всё приложение (журнал, сбои, сессия):
    // QStandardPaths добавил бы сюда ещё имена организации и приложения.
    QString directory = qEnvironmentVariable("XDG_STATE_HOME");
    if (directory.isEmpty() || !QDir(directory).isAbsolute())
        directory = QDir::homePath() + QStringLiteral("/.local/state");
    directory += QStringLiteral("/blackcore");
    QDir().mkpath(directory);
    return directory;
}

QJsonObject tabToJson(const TabState &tab)
{
    QJsonObject object;
    object.insert(QStringLiteral("title"), tab.title);
    object.insert(QStringLiteral("customTitle"), tab.customTitle);
    object.insert(QStringLiteral("activePane"), tab.activePane);
    object.insert(QStringLiteral("layout"), tab.layout ? tab.layout->toJson() : QJsonObject());
    return object;
}

TabState tabFromJson(const QJsonObject &object)
{
    TabState tab;
    tab.title = object.value(QStringLiteral("title")).toString();
    tab.customTitle = object.value(QStringLiteral("customTitle")).toString();
    tab.activePane = qMax(0, object.value(QStringLiteral("activePane")).toInt(0));
    tab.layout = LayoutNode::fromJson(object.value(QStringLiteral("layout")).toObject());
    return tab;
}

} // namespace

QString SessionStore::storagePath()
{
    return stateDirectory() + QStringLiteral("/session.json");
}

bool SessionStore::restoreEnabled()
{
    // Единый источник истины — Settings, чтобы значение попадало в экспорт настроек
    return Settings::instance().restoreSession();
}

void SessionStore::setRestoreEnabled(bool enabled)
{
    Settings &settings = Settings::instance();
    settings.setRestoreSession(enabled);
    settings.save();
}

QString SessionStore::sanitizeDirectory(const QString &directory)
{
    if (directory.isEmpty())
        return QDir::homePath();

    const QFileInfo info(directory);
    if (info.exists() && info.isDir() && info.isReadable())
        return info.absoluteFilePath();

    // Каталог исчез (удалён, отмонтирован диск, изменился пользователь)
    Log::warn(QStringLiteral("Каталог сессии недоступен и заменён на домашний: %1").arg(directory));
    return QDir::homePath();
}

SessionState SessionStore::sanitize(const SessionState &state)
{
    SessionState result;

    for (const TabState &tab : state.tabs) {
        if (!tab.layout || !tab.layout->isValid())
            continue;

        const QList<PaneSpec> panes = tab.layout->panes();
        if (panes.isEmpty())
            continue;

        // Проходим по дереву и исправляем каталоги/профили
        std::function<void(const QSharedPointer<LayoutNode> &)> fix =
            [&fix](const QSharedPointer<LayoutNode> &node) {
                if (!node)
                    return;
                if (node->isPane()) {
                    PaneSpec spec = node->pane();
                    spec.workingDirectory = sanitizeDirectory(spec.workingDirectory);
                    spec.profileId = ProfileManager::normalizeId(spec.profileId);
                    // Команда при восстановлении не выполняется
                    spec.command.clear();
                    spec.runCommand = false;
                    node->setPane(spec);
                    return;
                }
                for (const auto &child : node->children())
                    fix(child);
            };
        fix(tab.layout);

        TabState fixed = tab;
        fixed.activePane = qBound(0, tab.activePane, static_cast<int>(panes.size()) - 1);
        result.tabs.append(fixed);
    }

    result.currentTab = qBound(0, state.currentTab, qMax(0, static_cast<int>(result.tabs.size()) - 1));
    return result;
}

void SessionStore::save(const SessionState &state)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kFormatVersion);

    QJsonArray tabs;
    for (const TabState &tab : state.tabs) {
        if (!tab.layout || !tab.layout->isValid())
            continue;
        tabs.append(tabToJson(tab));
    }
    root.insert(QStringLiteral("tabs"), tabs);
    root.insert(QStringLiteral("currentTab"), state.currentTab);

    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        Log::warn(QStringLiteral("Не удалось сохранить сессию: %1").arg(file.errorString()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        Log::warn(QStringLiteral("Не удалось записать файл сессии"));
}

SessionState SessionStore::load()
{
    SessionState state;

    QFile file(storagePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return state;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        Log::warn(QStringLiteral("Файл сессии повреждён (%1), восстановление пропущено")
                      .arg(error.errorString()));
        return state;
    }

    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version > kFormatVersion) {
        Log::warn(QStringLiteral("Файл сессии создан более новой версией (%1)").arg(version));
        return state;
    }

    for (const QJsonValue &value : root.value(QStringLiteral("tabs")).toArray())
        state.tabs.append(tabFromJson(value.toObject()));
    state.currentTab = root.value(QStringLiteral("currentTab")).toInt(0);

    return sanitize(state);
}

void SessionStore::clear()
{
    QFile::remove(storagePath());
}

} // namespace BlackCore
