// SPDX-License-Identifier: MIT
#include "Diagnostics.h"

#include "AppInfo.h"
#include "CrashHandler.h"
#include "Logger.h"
#include "Settings.h"
#include "SystemMonitor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

#include <sys/statvfs.h>
#include <unistd.h>

namespace BlackCore {
namespace {

struct ToolSpec
{
    const char *name;
    const char *purpose;
    bool required;
};

const ToolSpec kTools[] = {
    {"bash", "оболочка по умолчанию для вкладок", true},
    {"git", "панель Git и стартовые команды проектов", false},
    {"ssh", "SSH-подключения из менеджера", false},
    {"php", "запуск PHP-проектов (php -S, artisan)", false},
    {"composer", "установка зависимостей PHP-проектов", false},
    {"python3", "запуск Python-проектов и venv", false},
    {"node", "запуск Node.js-проектов", false},
    {"npm", "установка зависимостей Node.js-проектов", false},
    {"top", "быстрый просмотр процессов в терминале", false},
    {"df", "проверка занятого места вручную", false},
    {"ps", "просмотр процессов вручную", false},
    {"sensors", "температуры через lm-sensors (если hwmon недоступен)", false},
};

bool isWritableDirectory(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    return info.exists() && info.isDir() && info.isWritable();
}

} // namespace

QString DiagnosticCheck::stateLabel(State state)
{
    switch (state) {
    case Ok:
        return QObject::tr("норма");
    case Warning:
        return QObject::tr("внимание");
    case Failed:
        return QObject::tr("ошибка");
    case Skipped:
        return QObject::tr("пропущено");
    }
    return QString();
}

QString DiagnosticCheck::stateText() const
{
    return stateLabel(state);
}

QStringList Diagnostics::knownTools()
{
    QStringList names;
    for (const ToolSpec &spec : kTools)
        names.append(QString::fromLatin1(spec.name));
    return names;
}

QString Diagnostics::toolVersion(const QString &path, int timeoutMs)
{
    if (path.isEmpty())
        return QString();

    QProcess process;
    process.setProgram(path);
    process.setArguments({QStringLiteral("--version")});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(timeoutMs))
        return QString();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(200);
        return QString();
    }
    const QString output = QString::fromUtf8(process.readAll()).trimmed();
    if (output.isEmpty())
        return QString();
    return output.split(QLatin1Char('\n')).first().trimmed().left(120);
}

QList<ToolInfo> Diagnostics::tools(bool withVersions)
{
    QList<ToolInfo> result;
    for (const ToolSpec &spec : kTools) {
        ToolInfo info;
        info.name = QString::fromLatin1(spec.name);
        info.purpose = QObject::tr(spec.purpose);
        info.required = spec.required;
        info.path = QStandardPaths::findExecutable(info.name);
        if (info.path.isEmpty() && info.name == QLatin1String("bash"))
            info.path = QStringLiteral("/bin/bash");
        if (withVersions && !info.path.isEmpty())
            info.version = toolVersion(info.path);
        result.append(info);
    }
    return result;
}

QList<DiagnosticCheck> Diagnostics::toolChecks()
{
    QList<DiagnosticCheck> checks;
    for (const ToolInfo &tool : tools(false)) {
        DiagnosticCheck check;
        check.id = QStringLiteral("tool/") + tool.name;
        check.title = QObject::tr("Инструмент %1").arg(tool.name);
        if (tool.available()) {
            check.state = DiagnosticCheck::Ok;
            check.detail = tool.path;
        } else if (tool.required) {
            check.state = DiagnosticCheck::Failed;
            check.detail = QObject::tr("не найден в PATH");
            check.hint = QObject::tr("Установите пакет, содержащий %1").arg(tool.name);
        } else {
            check.state = DiagnosticCheck::Warning;
            check.detail = QObject::tr("не найден — возможность недоступна до установки");
            check.hint = QObject::tr("BlackCore ничего не устанавливает сам: %1 ставится "
                                     "штатным менеджером пакетов, если это нужно")
                             .arg(tool.name);
        }
        checks.append(check);
    }
    return checks;
}

QList<DiagnosticCheck> Diagnostics::runChecks()
{
    QList<DiagnosticCheck> checks;

    auto add = [&checks](const QString &id, const QString &title, DiagnosticCheck::State state,
                         const QString &detail, const QString &hint = QString()) {
        DiagnosticCheck check;
        check.id = id;
        check.title = title;
        check.state = state;
        check.detail = detail;
        check.hint = hint;
        checks.append(check);
    };

    // --- сама программа ---
    add(QStringLiteral("app/version"),
        QObject::tr("Версия BlackCore"),
        DiagnosticCheck::Ok,
        BlackCore::buildInfo());
    add(QStringLiteral("app/platform"),
        QObject::tr("Графическая платформа"),
        qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
            ? DiagnosticCheck::Warning
            : DiagnosticCheck::Ok,
        QStringLiteral("Qt-платформа: %1, DISPLAY=%2, WAYLAND_DISPLAY=%3")
            .arg(QGuiApplication::platformName(),
                 qEnvironmentVariableIsEmpty("DISPLAY") ? QObject::tr("нет") : qEnvironmentVariable("DISPLAY"),
                 qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") ? QObject::tr("нет")
                                                                : qEnvironmentVariable("WAYLAND_DISPLAY")));
    // --- файлы и каталоги ---
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/BlackCore");
    add(QStringLiteral("io/config"),
        QObject::tr("Каталог настроек доступен для записи"),
        isWritableDirectory(configDir) ? DiagnosticCheck::Ok : DiagnosticCheck::Warning,
        configDir);
    add(QStringLiteral("io/state"),
        QObject::tr("Каталог состояния и журнала"),
        isWritableDirectory(QFileInfo(Log::filePath()).absolutePath() + QStringLiteral("/blackcore"))
                || isWritableDirectory(QFileInfo(Log::filePath()).absolutePath())
            ? DiagnosticCheck::Ok
            : DiagnosticCheck::Warning,
        Log::filePath().isEmpty() ? QObject::tr("журнал в файл недоступен")
                                  : QFileInfo(Log::filePath()).absolutePath());

    // --- /proc и /sys ---
    const bool procOk = QFileInfo::exists(QStringLiteral("/proc/stat"))
        && QFileInfo::exists(QStringLiteral("/proc/meminfo"));
    add(QStringLiteral("sys/proc"),
        QObject::tr("Системная информация /proc"),
        procOk ? DiagnosticCheck::Ok : DiagnosticCheck::Failed,
        procOk ? QObject::tr("/proc/stat и /proc/meminfo доступны на чтение")
               : QObject::tr("/proc недоступен — показатели системы не собираются"),
        procOk ? QString() : QObject::tr("Проверьте, что /proc смонтирован и доступен пользователю"));

    const bool sysOk = QFileInfo::exists(QStringLiteral("/sys/class/net"))
        && QFileInfo::exists(QStringLiteral("/sys/class/hwmon"));
    add(QStringLiteral("sys/sysfs"),
        QObject::tr("Датчики и сеть /sys"),
        sysOk ? DiagnosticCheck::Ok
              : (QFileInfo::exists(QStringLiteral("/sys/class/net")) ? DiagnosticCheck::Warning
                                                                     : DiagnosticCheck::Failed),
        sysOk ? QObject::tr("/sys/class/net и /sys/class/hwmon доступны")
              : QObject::tr("/sys/class/hwmon отсутствует — температуры могут быть недоступны"),
        sysOk ? QString() : QObject::tr("Температуры в виртуалках часто недоступны — это нормально"));

    // --- место под журнал ---
    struct statvfs vfs;
    if (::statvfs(QDir::homePath().toLocal8Bit().constData(), &vfs) == 0) {
        const qulonglong freeBytes = qulonglong(vfs.f_bavail) * qulonglong(vfs.f_frsize);
        add(QStringLiteral("io/disk"),
            QObject::tr("Свободное место в домашнем каталоге"),
            freeBytes > 512ULL * 1024 * 1024 ? DiagnosticCheck::Ok : DiagnosticCheck::Warning,
            QStringLiteral("%1 свободно в %2")
                .arg(SystemMonitor::formatBytes(freeBytes), QDir::homePath()),
            freeBytes > 512ULL * 1024 * 1024 ? QString()
                                             : QObject::tr("Освободите место: настройки и журнал "
                                                           "пишутся в домашний каталог"));
    }

    // --- журнал ---
    const QString logPath = Log::filePath();
    if (logPath.isEmpty()) {
        add(QStringLiteral("log/file"), QObject::tr("Файл журнала"), DiagnosticCheck::Warning,
            QObject::tr("журнал пишется только в stderr"));
    } else {
        const QFileInfo info(logPath);
        add(QStringLiteral("log/file"), QObject::tr("Файл журнала"),
            info.exists() ? DiagnosticCheck::Ok : DiagnosticCheck::Warning,
            info.exists() ? QStringLiteral("%1 · %2").arg(logPath, SystemMonitor::formatBytes(qulonglong(info.size())))
                          : QObject::tr("файл ещё не создан: %1").arg(logPath));
    }

    // --- сбои ---
    const QList<QString> reports = Crash::reportFiles();
    if (Crash::previousRunCrashed()) {
        add(QStringLiteral("crash/marker"), QObject::tr("Предыдущий запуск"),
            DiagnosticCheck::Warning,
            QObject::tr("приложение завершилось не штатно (остался маркер запуска)"),
            QObject::tr("Откройте отчёты о сбоях и журнал: причина обычно в последних строках"));
    } else {
        add(QStringLiteral("crash/marker"), QObject::tr("Предыдущий запуск"), DiagnosticCheck::Ok,
            QObject::tr("завершён штатно"));
    }
    add(QStringLiteral("crash/reports"), QObject::tr("Отчёты о сбоях"),
        reports.isEmpty() ? DiagnosticCheck::Ok : DiagnosticCheck::Warning,
        reports.isEmpty() ? QObject::tr("отчётов нет")
                          : QObject::tr("отчётов: %1, последний — %2")
                                .arg(reports.size())
                                .arg(QFileInfo(reports.first()).fileName()));

    // --- локаль ---
    const QString localeName = QLocale::system().name();
    const bool utf8 = QString::fromLatin1(qgetenv("LC_ALL")).contains(QLatin1String("UTF-8"))
        || QString::fromLatin1(qgetenv("LANG")).contains(QLatin1String("UTF-8"))
        || localeName.endsWith(QLatin1String("UTF-8"), Qt::CaseInsensitive);
    add(QStringLiteral("locale/utf8"), QObject::tr("Локаль UTF-8"),
        utf8 ? DiagnosticCheck::Ok : DiagnosticCheck::Warning,
        QStringLiteral("LANG=%1, LC_ALL=%2")
            .arg(qEnvironmentVariableIsEmpty("LANG") ? QObject::tr("нет") : qEnvironmentVariable("LANG"),
                 qEnvironmentVariableIsEmpty("LC_ALL") ? QObject::tr("нет") : qEnvironmentVariable("LC_ALL")),
        utf8 ? QString() : QObject::tr("Для кириллицы в терминале нужна локаль UTF-8"));

    // --- настройки ---
    const Settings &settings = Settings::instance();
    QStringList knownShells = {QStringLiteral("/bin/bash"), QStringLiteral("/bin/zsh"),
                               QStringLiteral("/usr/bin/fish"), QStringLiteral("/bin/sh")};
    const QString shell = settings.shell();
    add(QStringLiteral("settings/shell"), QObject::tr("Оболочка терминала"),
        shell.isEmpty() || QFileInfo::exists(shell) || QStandardPaths::findExecutable(shell).size() > 0
            ? DiagnosticCheck::Ok
            : DiagnosticCheck::Warning,
        shell.isEmpty() ? QObject::tr("по умолчанию (SHELL из окружения)") : shell,
        shell.isEmpty() || QFileInfo::exists(shell) ? QString()
                                                    : QObject::tr("Укажите существующую оболочку в настройках"));

    const QString sshDir = QDir::homePath() + QStringLiteral("/.ssh");
    add(QStringLiteral("ssh/dir"), QObject::tr("Каталог ключей ~/.ssh"),
        QFileInfo::exists(sshDir) ? DiagnosticCheck::Ok : DiagnosticCheck::Skipped,
        QFileInfo::exists(sshDir) ? sshDir : QObject::tr("каталог отсутствует — SSH-ключи не найдены"));
    if (QFileInfo::exists(sshDir)) {
        const QFileInfo dirInfo(sshDir);
        const bool privateOk = !(dirInfo.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther));
        add(QStringLiteral("ssh/permissions"), QObject::tr("Права на ~/.ssh"),
            privateOk ? DiagnosticCheck::Ok : DiagnosticCheck::Warning,
            privateOk ? QObject::tr("доступ только владельцу")
                      : QObject::tr("каталог читают другие пользователи"),
            privateOk ? QString() : QObject::tr("Выполните: chmod 700 ~/.ssh"));
    }

    checks.append(toolChecks());
    return checks;
}

QString Diagnostics::summarize(const QList<DiagnosticCheck> &checks)
{
    int ok = 0;
    int warnings = 0;
    int failed = 0;
    for (const DiagnosticCheck &check : checks) {
        switch (check.state) {
        case DiagnosticCheck::Ok: ++ok; break;
        case DiagnosticCheck::Warning: ++warnings; break;
        case DiagnosticCheck::Failed: ++failed; break;
        case DiagnosticCheck::Skipped: break;
        }
    }
    return QObject::tr("Проверок: %1 · норма: %2 · внимание: %3 · ошибок: %4")
        .arg(checks.size())
        .arg(ok)
        .arg(warnings)
        .arg(failed);
}

QString Diagnostics::logPath()
{
    return Log::filePath();
}

qint64 Diagnostics::logSize()
{
    const QString path = Log::filePath();
    if (path.isEmpty())
        return 0;
    const QFileInfo info(path);
    return info.exists() ? info.size() : 0;
}

QString Diagnostics::tailLog(int maxLines)
{
    const QString path = Log::filePath();
    if (path.isEmpty())
        return QString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    // Журнал ограничен 2 МиБ, поэтому читаем целиком и оставляем хвост
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    QStringList limited;
    const int start = qMax(0, lines.size() - maxLines);
    for (int i = start; i < lines.size(); ++i)
        limited.append(lines.at(i));
    while (!limited.isEmpty() && limited.last().isEmpty())
        limited.removeLast();
    return limited.join(QLatin1Char('\n'));
}

} // namespace BlackCore
