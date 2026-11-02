// SPDX-License-Identifier: MIT
#pragma once

#include "../core/Settings.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace BlackCore {

//! Профиль терминала: набор настроек сессии, независимый от глобальных.
/*!
 *  Профиль применяется к панели при её создании (оболочка, шрифт, курсор,
 *  глубина прокрутки). Глобальные настройки внешнего вида при этом не меняются.
 */
struct TerminalProfile
{
    QString id;
    QString name;
    QString shell;                          //!< пусто — как в настройках
    QString fontFamily;                     //!< пусто — как в настройках
    int fontSize = 0;                       //!< 0 — как в настройках
    CursorStyle cursorStyle = CursorStyle::FollowApplication;
    int scrollback = 0;                     //!< 0 — как в настройках

    QJsonObject toJson() const;
    static TerminalProfile fromJson(const QJsonObject &object);

    bool isValid() const { return !id.isEmpty(); }
};

//! Хранилище профилей (встроенный «По умолчанию» + пользовательские).
class ProfileManager
{
public:
    static const QString &defaultId();

    //! Все профили: сначала встроенный, затем пользовательские
    static QList<TerminalProfile> profiles();

    static TerminalProfile profile(const QString &id);

    //! Сохранить (добавить или обновить) пользовательский профиль
    static void saveProfile(const TerminalProfile &profile);
    static void removeProfile(const QString &id);

    //! Идентификатор актуального профиля (если профиль удалён — «по умолчанию»)
    static QString normalizeId(const QString &id);
};

} // namespace BlackCore
