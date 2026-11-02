// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace BlackCore {

//! Внешний инструмент, который использует или может использовать BlackCore
struct ToolInfo
{
    QString name;      //!< имя исполняемого файла (git, ssh, php …)
    QString purpose;   //!< зачем нужен
    QString path;      //!< найденный путь (пусто, если не найден)
    QString version;   //!< первая строка вывода --version (если удалось получить)
    bool required = false;
    bool available() const { return !path.isEmpty(); }
};

//! Результат одной диагностической проверки
struct DiagnosticCheck
{
    enum State { Ok, Warning, Failed, Skipped };

    QString id;
    QString title;
    QString detail;
    QString hint;   //!< что сделать, если проверка не прошла
    State state = Ok;

    bool passed() const { return state == Ok || state == Skipped; }
    QString stateText() const;
    static QString stateLabel(State state);
};

//! Проверки, журнал и сведения о сбоях.
/*!
 *  Все проверки выполняются с правами обычного пользователя и ничего не меняют:
 *  читаются /proc, /sys, конфигурация и журнал, ищутся исполняемые файлы в PATH.
 */
class Diagnostics
{
public:
    //! Список инструментов, которые проверяются (без установки!)
    static QStringList knownTools();

    //! Наличие инструментов; withVersions=false — быстро, только поиск в PATH
    static QList<ToolInfo> tools(bool withVersions = false);

    //! Полный набор диагностических проверок
    static QList<DiagnosticCheck> runChecks();

    //! Только проверки инструментов (для быстрого обновления)
    static QList<DiagnosticCheck> toolChecks();

    //! Последние строки журнала BlackCore
    static QString tailLog(int maxLines = 200);

    //! Путь к журналу и его размер в байтах
    static QString logPath();
    static qint64 logSize();

    //! Короткая сводка: сколько проверок прошло, сколько предупреждений и ошибок
    static QString summarize(const QList<DiagnosticCheck> &checks);

    //! Текст версии инструмента (первая строка вывода), пустая строка при неудаче
    static QString toolVersion(const QString &path, int timeoutMs = 1200);
};

} // namespace BlackCore
