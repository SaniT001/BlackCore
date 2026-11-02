// SPDX-License-Identifier: MIT
#pragma once

#include "PaneLayout.h"

#include <QList>
#include <QString>

namespace BlackCore {

//! Состояние одной вкладки для сохранения.
struct TabState
{
    QString title;                    //!< видимое имя вкладки
    QString customTitle;              //!< имя, заданное пользователем (не перетирается программой)
    QSharedPointer<LayoutNode> layout;
    int activePane = 0;
};

//! Состояние всех вкладок окна.
struct SessionState
{
    QList<TabState> tabs;
    int currentTab = 0;
    bool isValid() const { return !tabs.isEmpty(); }
};

//! Сохранение и восстановление сессий между запусками.
/*!
 *  В файл попадают только имена, каталоги, профили и геометрия раскладки.
 *  Команды, набранные в терминале, не сохраняются вообще — поэтому при
 *  восстановлении невозможно «повторить» потенциально опасную команду.
 */
class SessionStore
{
public:
    static QString storagePath();

    static void save(const SessionState &state);
    static SessionState load();

    static void clear();

    //! Включено ли восстановление при запуске
    static bool restoreEnabled();
    static void setRestoreEnabled(bool enabled);

    //! Проверка рабочих каталогов: несуществующие заменяются на домашний
    static QString sanitizeDirectory(const QString &directory);

    //! Убрать пустые/битые панели и вкладки
    static SessionState sanitize(const SessionState &state);
};

} // namespace BlackCore
