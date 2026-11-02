// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace BlackCore {

//! Состояние Git-репозитория (только чтение, без изменения файлов).
struct GitStatus
{
    bool valid = false;         //!< удалось определить репозиторий
    bool gitAvailable = false;  //!< в системе есть исполняемый git
    QString repositoryRoot;     //!< корень рабочего дерева
    QString gitDirectory;       //!< каталог .git
    QString branch;             //!< текущая ветка («HEAD» при detached)
    QString lastCommit;         //!< короткий хэш последнего коммита
    int ahead = 0;              //!< коммитов впереди upstream
    int behind = 0;             //!< коммитов позади upstream
    int modified = 0;           //!< изменённых файлов
    int staged = 0;             //!< изменений в индексе
    int untracked = 0;          //!< неотслеживаемых файлов
    int conflicted = 0;         //!< конфликтов слияния
    QString error;              //!< причина, по которой статус не получен

    int changedFiles() const { return modified + staged + untracked + conflicted; }
    bool isClean() const { return valid && changedFiles() == 0; }
    bool isDetached() const { return valid && branch == QStringLiteral("HEAD"); }

    //! Краткая подпись для строки состояния: «main •3»
    QString shortLabel() const;
};

//! Определение репозитория и чтение статуса через системный git (только чтение).
/*!
 *  BlackCore не изменяет репозиторий: используются исключительно команды чтения
 *  (rev-parse, status, log, symbolic-ref). Если git не установлен, репозиторий
 *  всё равно определяется по каталогу .git, а ветка читается из .git/HEAD.
 */
class GitService
{
public:
    //! Есть ли исполняемый git в системе
    static bool isGitAvailable();
    static QString gitVersion();

    //! Найти корень репозитория, поднимаясь от каталога вверх
    static QString findRepositoryRoot(const QString &directory);

    //! Полный статус для каталога (внутри репозитория или его корня)
    static GitStatus status(const QString &directory);

    //! Разрешённые к запуску команды быстрого доступа (только чтение и безопасные)
    static QStringList quickCommands();

    //! Проверка, что команда входит в белый список
    static bool isAllowedCommand(const QString &command);

private:
    static QString runGit(const QString &workingDirectory, const QStringList &arguments,
                          bool *ok = nullptr);
};

} // namespace BlackCore
