// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>

namespace BlackCore {

//! Защита секретов: очистка строк перед записью в журнал.
/*!
 *  Пароли, токены и приватные ключи не должны попадать в диагностические
 *  журналы. Всё, что пишется в журнал (файл и stderr), проходит через
 *  Secrets::redact(); исходная строка нигде не сохраняется.
 */
class Secrets
{
public:
    //! Маска, которой заменяются секреты
    static QString mask();

    //! Есть ли в строке похожее на секрет
    static bool containsSecret(const QString &text);

    //! Заменить секреты маской, остальное оставить как есть
    static QString redact(const QString &text);

    //! Безопасная для журнала форма команды (обёртка над redact)
    static QString commandForLog(const QString &command);

    //! Список шаблонов (для документации и тестов)
    static QStringList patterns();
};

} // namespace BlackCore
