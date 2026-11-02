// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

#include <functional>

namespace BlackCore {
namespace Log {

//! Устанавливает обработчик сообщений Qt: stderr + файл в ~/.local/state/blackcore/blackcore.log
void install(bool verbose);
void shutdown();

//! Полный путь к текущему лог-файлу (пустая строка, если логирование в файл недоступно)
QString filePath();

//! Обработчик серьёзных сообщений (ошибки) — используется уведомлениями приложения.
/*!
 *  Вызывается для qCritical/qFatal уже с очищенным от секретов текстом.
 *  Обработчик не должен сам писать в журнал, чтобы не зациклиться.
 */
void setErrorNotifier(const std::function<void(const QString &)> &notifier);

//! Удобные обёртки (используют категории qCInfo и т.п. через qInfo/qWarning)
void info(const QString &message);
void warn(const QString &message);
void error(const QString &message);

} // namespace Log
} // namespace BlackCore
