// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace BlackCore {
namespace Crash {

//! Каталог отчётов о сбоях: ~/.local/state/blackcore/crashes
QString reportsDir();

//! Файл-маркер запущенного экземпляра: ~/.local/state/blackcore/running.marker
QString markerPath();

//! Устанавливает обработчики SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS и ставит маркер запуска.
/*!
 *  Отчёт пишется в отдельный файл компактно (без Qt): версия, тип сигнала,
 *  время работы, последние строки журнала и стек вызовов. Пароли и ввод
 *  пользователя в отчёт не попадают.
 */
void install();

//! Штатное завершение: маркер удаляется
void markCleanExit();

//! Предыдущий запуск не завершился штатно (маркер остался и процесс уже не живёт)
bool previousRunCrashed();

//! Записать отчёт вручную (используется обработчиком сигнала и тестами)
QString writeReport(const QString &signalName, const QString &details = QString());

//! Список отчётов, новые первыми
QList<QString> reportFiles();

//! Последний отчёт (путь), если он есть
QString lastReportFile();

//! Содержимое отчёта (не больше maxBytes)
QString readReport(const QString &path, int maxBytes = 20000);

//! Удалить все отчёты (кнопка «Очистить» в панели диагностики)
bool clearReports();

} // namespace Crash
} // namespace BlackCore
