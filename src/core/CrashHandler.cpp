// SPDX-License-Identifier: MIT
#include "CrashHandler.h"

#include "AppInfo.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <execinfo.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

namespace BlackCore {
namespace Crash {
namespace {

constexpr int kMaxFrames = 32;
constexpr qint64 kMaxReportBytes = 256 * 1024;

char g_reportDir[PATH_MAX] = {0};
char g_logPath[PATH_MAX] = {0};
volatile sig_atomic_t g_inHandler = 0;
time_t g_startTime = 0;

QString stateDir()
{
    return BlackCore::stateDirectory();
}

void writeCStr(int fd, const char *text)
{
    if (fd < 0 || !text)
        return;
    const ssize_t ignored = ::write(fd, text, std::strlen(text));
    (void)ignored;
}

void writeInt(int fd, long value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%ld", value);
    writeCStr(fd, buffer);
}

//! Тело обработчика: только write() и open(), без Qt и malloc
void handleSignal(int signalNumber)
{
    if (g_inHandler)
        ::_exit(128 + signalNumber);
    g_inHandler = 1;

    const char *name = "SIG?";
    switch (signalNumber) {
    case SIGSEGV: name = "SIGSEGV"; break;
    case SIGABRT: name = "SIGABRT"; break;
    case SIGFPE: name = "SIGFPE"; break;
    case SIGILL: name = "SIGILL"; break;
    case SIGBUS: name = "SIGBUS"; break;
    default: break;
    }

    char path[PATH_MAX + 64];
    char stamp[32];
    const time_t now = ::time(nullptr);
    struct tm tmNow;
    ::localtime_r(&now, &tmNow);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmNow);
    std::snprintf(path, sizeof(path), "%s/crash-%s-%d.txt", g_reportDir, stamp, static_cast<int>(::getpid()));

    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        writeCStr(fd, "BlackCore crash report\n");
        writeCStr(fd, "version: ");
        writeCStr(fd, kAppVersion);
        writeCStr(fd, "\nsignal: ");
        writeCStr(fd, name);
        writeCStr(fd, "\npid: ");
        writeInt(fd, static_cast<long>(::getpid()));
        writeCStr(fd, "\nwhen: ");
        writeCStr(fd, stamp);
        writeCStr(fd, "\nuptime_seconds: ");
        writeInt(fd, static_cast<long>(g_startTime > 0 ? now - g_startTime : 0));
        writeCStr(fd, "\n\nbacktrace:\n");
        void *frames[kMaxFrames];
        const int count = ::backtrace(frames, kMaxFrames);
        ::backtrace_symbols_fd(frames, count, fd);
        writeCStr(fd, "\nlast log lines: see ");
        writeCStr(fd, g_logPath);
        writeCStr(fd, "\n");
        ::close(fd);
    }

    // Возвращаем поведение по умолчанию, чтобы система получила корректный код
    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

void installOne(int signalNumber)
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_RESETHAND;
    ::sigaction(signalNumber, &action, nullptr);
}

} // namespace

QString reportsDir()
{
    return stateDir() + QStringLiteral("/crashes");
}

QString markerPath()
{
    return stateDir() + QStringLiteral("/running.marker");
}

void install()
{
    QDir().mkpath(reportsDir());

    const QString dir = reportsDir();
    const QByteArray dirBytes = QFile::encodeName(dir);
    std::snprintf(g_reportDir, sizeof(g_reportDir), "%s", dirBytes.constData());

    const QByteArray logBytes = QFile::encodeName(Log::filePath());
    std::snprintf(g_logPath, sizeof(g_logPath), "%s", logBytes.constData());

    g_startTime = ::time(nullptr);

    installOne(SIGSEGV);
    installOne(SIGABRT);
    installOne(SIGFPE);
    installOne(SIGILL);
    installOne(SIGBUS);

    // Маркер запуска: останется на диске, если процесс завершится аварийно
    const QString marker = markerPath();
    QFile file(marker);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&file);
        out << "pid=" << QCoreApplication::applicationPid() << '\n'
            << "started=" << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n'
            << "version=" << QString::fromLatin1(kAppVersion) << '\n';
        file.close();
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void markCleanExit()
{
    QFile::remove(markerPath());
}

bool previousRunCrashed()
{
    QFile marker(markerPath());
    if (!marker.exists())
        return false;
    if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    qint64 pid = 0;
    while (!marker.atEnd()) {
        const QString line = QString::fromUtf8(marker.readLine()).trimmed();
        if (line.startsWith(QLatin1String("pid=")))
            pid = line.mid(4).toLongLong();
    }
    marker.close();

    if (pid <= 0)
        return true; // маркер есть, но испорчен — считаем прошлый запуск аварийным
    if (pid == QCoreApplication::applicationPid())
        return false; // наш собственный маркер

    // Если процесс ещё жив — значит работает другой экземпляр, а не сбой
    if (::kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH)
        return false;
    return true;
}

QString writeReport(const QString &signalName, const QString &details)
{
    const QString dirPath = reportsDir();
    QDir().mkpath(dirPath);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path = QStringLiteral("%1/crash-%2-%3.txt").arg(dirPath, stamp).arg(QCoreApplication::applicationPid());
    for (int attempt = 1; QFileInfo::exists(path) && attempt < 100; ++attempt) {
        path = QStringLiteral("%1/crash-%2-%3-%4.txt")
                   .arg(dirPath, stamp)
                   .arg(QCoreApplication::applicationPid())
                   .arg(attempt);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return QString();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "BlackCore crash report\n";
    out << "version: " << QString::fromLatin1(kAppVersion) << '\n';
    out << "signal: " << (signalName.isEmpty() ? QStringLiteral("manual") : signalName) << '\n';
    out << "pid: " << QCoreApplication::applicationPid() << '\n';
    out << "when: " << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    out << "uptime_seconds: " << (g_startTime > 0 ? ::time(nullptr) - g_startTime : 0) << '\n';
    if (!details.isEmpty())
        out << "details: " << details << '\n';
    out << "log: " << Log::filePath() << '\n';
    file.flush();
    file.close();
    return path;
}

QList<QString> reportFiles()
{
    QDir dir(reportsDir());
    if (!dir.exists())
        return {};
    const QStringList files = dir.entryList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Name);
    QStringList absolute;
    for (const QString &file : files) {
        if (QFileInfo(dir.filePath(file)).size() > kMaxReportBytes)
            continue;
        absolute.append(dir.filePath(file));
    }
    // Имена содержат дату и время, поэтому обратный порядок — новые первыми
    std::reverse(absolute.begin(), absolute.end());
    return absolute;
}

QString lastReportFile()
{
    const QList<QString> files = reportFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString readReport(const QString &path, int maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QByteArray data = file.read(maxBytes);
    return QString::fromUtf8(data);
}

bool clearReports()
{
    const QList<QString> files = reportFiles();
    bool ok = true;
    for (const QString &file : files)
        ok = QFile::remove(file) && ok;
    return ok;
}

} // namespace Crash
} // namespace BlackCore
