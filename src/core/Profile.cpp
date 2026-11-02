// SPDX-License-Identifier: MIT
#include "Profile.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSettings>

#include <algorithm>

namespace BlackCore {
namespace {
constexpr const char *kProfilesKey = "profiles/list";
} // namespace

const QString &ProfileManager::defaultId()
{
    static const QString id = QStringLiteral("default");
    return id;
}

QJsonObject TerminalProfile::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("shell"), shell);
    object.insert(QStringLiteral("fontFamily"), fontFamily);
    object.insert(QStringLiteral("fontSize"), fontSize);
    object.insert(QStringLiteral("cursorStyle"), static_cast<int>(cursorStyle));
    object.insert(QStringLiteral("scrollback"), scrollback);
    return object;
}

TerminalProfile TerminalProfile::fromJson(const QJsonObject &object)
{
    TerminalProfile profile;
    profile.id = object.value(QStringLiteral("id")).toString();
    profile.name = object.value(QStringLiteral("name")).toString();
    profile.shell = object.value(QStringLiteral("shell")).toString();
    profile.fontFamily = object.value(QStringLiteral("fontFamily")).toString();
    profile.fontSize = qBound(0, object.value(QStringLiteral("fontSize")).toInt(0),
                              Settings::kMaxFontSize);
    profile.cursorStyle = static_cast<CursorStyle>(
        qBound(0, object.value(QStringLiteral("cursorStyle")).toInt(0), 3));
    profile.scrollback = qBound(0, object.value(QStringLiteral("scrollback")).toInt(0),
                                Settings::kMaxScrollback);
    return profile;
}

QList<TerminalProfile> ProfileManager::profiles()
{
    QList<TerminalProfile> result;

    TerminalProfile builtin;
    builtin.id = defaultId();
    builtin.name = QCoreApplication::translate("ProfileManager", "По умолчанию");
    result.append(builtin);

    QSettings store;
    const QJsonArray array = QJsonArray::fromVariantList(
        store.value(QLatin1String(kProfilesKey)).toList());

    for (const QJsonValue &value : array) {
        const TerminalProfile profile = TerminalProfile::fromJson(value.toObject());
        if (!profile.isValid() || profile.id == defaultId())
            continue;
        result.append(profile);
    }
    return result;
}

TerminalProfile ProfileManager::profile(const QString &id)
{
    const QList<TerminalProfile> all = profiles();
    for (const TerminalProfile &profile : all) {
        if (profile.id == id)
            return profile;
    }
    return all.first();
}

void ProfileManager::saveProfile(const TerminalProfile &profile)
{
    if (!profile.isValid() || profile.id == defaultId())
        return;

    QList<TerminalProfile> userProfiles;
    const QList<TerminalProfile> all = profiles();
    for (int i = 1; i < all.size(); ++i) {
        if (all.at(i).id == profile.id)
            userProfiles.append(profile);
        else
            userProfiles.append(all.at(i));
    }
    if (!std::any_of(userProfiles.begin(), userProfiles.end(),
                     [&profile](const TerminalProfile &item) { return item.id == profile.id; })) {
        userProfiles.append(profile);
    }

    QVariantList storage;
    for (const TerminalProfile &item : userProfiles)
        storage.append(item.toJson().toVariantMap());

    QSettings store;
    store.setValue(QLatin1String(kProfilesKey), storage);
    store.sync();
}

void ProfileManager::removeProfile(const QString &id)
{
    if (id == defaultId())
        return;

    QVariantList storage;
    const QList<TerminalProfile> all = profiles();
    for (int i = 1; i < all.size(); ++i) {
        if (all.at(i).id != id)
            storage.append(all.at(i).toJson().toVariantMap());
    }

    QSettings store;
    store.setValue(QLatin1String(kProfilesKey), storage);
    store.sync();
}

QString ProfileManager::normalizeId(const QString &id)
{
    if (id.isEmpty())
        return defaultId();
    for (const TerminalProfile &profile : profiles()) {
        if (profile.id == id)
            return id;
    }
    return defaultId();
}

} // namespace BlackCore
