// SPDX-License-Identifier: MIT
#include "GitInfo.h"

#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>

namespace BlackCore {
namespace {

constexpr int kProcessTimeoutMs = 3500;

//! Путь к git: из PATH или стандартных мест
QString gitExecutable()
{
    static const QString cached = []() {
        const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("git"));
        if (!fromPath.isEmpty())
            return fromPath;
        for (const QString &candidate : {QStringLiteral("/usr/bin/git"),
                                        QStringLiteral("/usr/local/bin/git"),
                                        QStringLiteral("/bin/git")}) {
            if (QFileInfo(candidate).isExecutable())
                return candidate;
        }
        return QString();
    }();
    return cached;
}

} // namespace

QString GitStatus::shortLabel() const
{
    if (!valid)
        return QString();

    QString label = branch.isEmpty() ? QStringLiteral("git") : branch;
    if (isDetached())
        label = QStringLiteral("HEAD");
    if (ahead > 0 || behind > 0)
        label += QStringLiteral(" ↑%1↓%2").arg(ahead).arg(behind);
    if (changedFiles() > 0)
        label += QStringLiteral(" •%1").arg(changedFiles());
    return label;
}

bool GitService::isGitAvailable()
{
    return !gitExecutable().isEmpty();
}

QString GitService::gitVersion()
{
    if (!isGitAvailable())
        return QString();

    bool ok = false;
    const QString version = runGit(QDir::homePath(), {QStringLiteral("--version")}, &ok).trimmed();
    return ok ? version : QString();
}

QString GitService::runGit(const QString &workingDirectory, const QStringList &arguments, bool *ok)
{
    if (ok)
        *ok = false;
    const QString executable = gitExecutable();
    if (executable.isEmpty())
        return QString();

    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setWorkingDirectory(workingDirectory);
    // Никаких запросов пароля/ввода: только чтение
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(kProcessTimeoutMs))
        return QString();

    if (!process.waitForFinished(kProcessTimeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        return QString();
    }

    if (process.exitStatus() != QProcess::NormalExit)
        return QString();

    if (ok)
        *ok = process.exitCode() == 0;
    return QString::fromUtf8(process.readAllStandardOutput());
}

QString GitService::findRepositoryRoot(const QString &directory)
{
    if (directory.isEmpty())
        return QString();

    QDir current(directory);
    if (!current.exists())
        return QString();
    current = QDir(current.absolutePath());

    for (int depth = 0; depth < 64; ++depth) {
        const QFileInfo dotGit(current.absoluteFilePath(QStringLiteral(".git")));
        if (dotGit.exists())
            return current.absolutePath();

        if (!current.cdUp())
            break;
    }
    return QString();
}

GitStatus GitService::status(const QString &directory)
{
    GitStatus status;
    status.gitAvailable = isGitAvailable();

    const QString trimmedDirectory = directory.trimmed();
    if (trimmedDirectory.isEmpty()) {
        status.error = QStringLiteral("каталог не задан");
        return status;
    }

    const QString root = findRepositoryRoot(trimmedDirectory);
    if (root.isEmpty()) {
        status.error = QStringLiteral("это не Git-репозиторий");
        return status;
    }

    status.valid = true;
    status.repositoryRoot = root;
    status.gitDirectory = root + QStringLiteral("/.git");

    if (!status.gitAvailable) {
        // Резервный путь без git: ветка читается из .git/HEAD
        QFile head(status.gitDirectory + QStringLiteral("/HEAD"));
        if (head.open(QIODevice::ReadOnly)) {
            const QString content = QString::fromUtf8(head.readAll()).trimmed();
            if (content.startsWith(QStringLiteral("ref: refs/heads/")))
                status.branch = content.mid(QStringLiteral("ref: refs/heads/").size());
            else
                status.branch = QStringLiteral("HEAD");
        }
        status.error = QStringLiteral("git не установлен: показана только ветка");
        return status;
    }

    bool ok = false;
    const QString branch = runGit(root, {QStringLiteral("rev-parse"),
                                         QStringLiteral("--abbrev-ref"),
                                         QStringLiteral("HEAD")},
                                  &ok)
                               .trimmed();
    if (ok && !branch.isEmpty())
        status.branch = branch;

    const QString commit = runGit(root, {QStringLiteral("rev-parse"), QStringLiteral("--short"),
                                         QStringLiteral("HEAD")},
                                  &ok)
                               .trimmed();
    if (ok)
        status.lastCommit = commit;

    // git status --porcelain=v1 --branch: одна команда на всю картину
    const QString porcelain = runGit(root, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                                            QStringLiteral("--branch"),
                                            QStringLiteral("--untracked-files=normal")},
                                     &ok);
    if (!ok) {
        status.error = QStringLiteral("не удалось получить статус репозитория");
        return status;
    }

    const QStringList lines = porcelain.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression aheadBehind(
        QStringLiteral("^## .*?\\[ahead (\\d+)(?:, behind (\\d+))?\\]"));
    static const QRegularExpression behindOnly(
        QStringLiteral("^## .*?\\[behind (\\d+)\\]"));

    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("##"))) {
            auto match = aheadBehind.match(line);
            if (match.hasMatch()) {
                status.ahead = match.captured(1).toInt();
                status.behind = match.captured(2).toInt();
            } else {
                match = behindOnly.match(line);
                if (match.hasMatch())
                    status.behind = match.captured(1).toInt();
            }
            continue;
        }

        if (line.size() < 3)
            continue;

        const QChar indexState = line.at(0);
        const QChar workTreeState = line.at(1);

        if (indexState == QLatin1Char('?') && workTreeState == QLatin1Char('?')) {
            ++status.untracked;
            continue;
        }
        if (indexState == QLatin1Char('U') || workTreeState == QLatin1Char('U')
            || (indexState == QLatin1Char('A') && workTreeState == QLatin1Char('A'))
            || (indexState == QLatin1Char('D') && workTreeState == QLatin1Char('D'))) {
            ++status.conflicted;
            continue;
        }
        if (indexState != QLatin1Char(' ') && indexState != QLatin1Char('?'))
            ++status.staged;
        if (workTreeState != QLatin1Char(' ') && workTreeState != QLatin1Char('?'))
            ++status.modified;
    }

    if (status.branch.isEmpty()) {
        const QString symbolic = runGit(root, {QStringLiteral("symbolic-ref"),
                                               QStringLiteral("--short"),
                                               QStringLiteral("HEAD")},
                                        &ok)
                                     .trimmed();
        status.branch = ok && !symbolic.isEmpty() ? symbolic : QStringLiteral("HEAD");
    }

    return status;
}

QStringList GitService::quickCommands()
{
    return {QStringLiteral("git status"),
            QStringLiteral("git log --oneline --graph -20"),
            QStringLiteral("git diff"),
            QStringLiteral("git diff --staged"),
            QStringLiteral("git branch -vv"),
            QStringLiteral("git remote -v"),
            QStringLiteral("git fetch --all --prune"),
            QStringLiteral("git pull --ff-only"),
            QStringLiteral("git push")};
}

bool GitService::isAllowedCommand(const QString &command)
{
    return quickCommands().contains(command.trimmed());
}

} // namespace BlackCore
