// SPDX-License-Identifier: MIT
#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

namespace BlackCore {

//! Описание действия, которому можно назначить горячую клавишу.
struct ShortcutSpec
{
    QString id;                  //!< стабильный идентификатор («pane.splitVertical»)
    QString title;               //!< подпись для таблицы настроек
    QString group;               //!< группа («Вкладки», «Панели», …)
    QKeySequence defaultKey;     //!< клавиши по умолчанию (пусто — не заданы)
};

//! Пользовательские горячие клавиши: значения по умолчанию + переопределения.
/*!
 *  Переопределения хранятся в QSettings в разделе `shortcuts/overrides`
 *  (идентификатор → строка клавиш). Пустая строка означает «действие без клавиш».
 */
class ShortcutManager
{
public:
    //! Полный список действий с клавишами по умолчанию
    static const QList<ShortcutSpec> &catalog();

    //! Клавиши действия с учётом переопределений
    static QKeySequence key(const QString &id);

    //! Клавиши по умолчанию
    static QKeySequence defaultKey(const QString &id);

    //! Назначить клавиши (пустая последовательность — отключить)
    static void setKey(const QString &id, const QKeySequence &key);

    //! Вернуть клавиши по умолчанию для действия
    static void resetKey(const QString &id);

    //! Сбросить все переопределения
    static void resetAll();

    //! Есть ли переопределение для действия
    static bool hasOverride(const QString &id);

    //! Сколько действий переопределено
    static int overrideCount();

    //! Разбор строки вида «Ctrl+Shift+T»; пустая строка — отключено, nullptr — ошибка
    static bool parse(const QString &text, QKeySequence *key);

    //! Идентификатор действия, которое уже использует эти клавиши (или пусто)
    static QString conflict(const QString &keyText, const QString &exceptId = QString());

    //! Все конфликты в текущем наборе: «id1 ↔ id2»
    static QStringList conflicts();

private:
    static QList<ShortcutSpec> buildCatalog();
};

} // namespace BlackCore
