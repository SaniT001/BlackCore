// SPDX-License-Identifier: MIT
#include "Logger.h"

#include "AppInfo.h"
#include "Secrets.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace BlackCore {
namespace Log {
namespace {

QFile *g_logFile = nullptr;
QString g_logPath;
QMutex g_mutex;
QtMessageHandler g_previousHandler = nullptr;
bool g_verbose = false;
std::function<void(const QString &)> g_errorNotifier;

constexpr qint64 kMaxLogSize = 2 * 1024 * 1024; // 2 MiB

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO ");
    case QtWarningMsg:
        return QStringLiteral("WARN ");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("?????");
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QMutexLocker locker(&g_mutex);

    // Пароли, токены и приватные ключи не попадают ни в файл, ни в stderr
    const QString safeMessage = Secrets::redact(message);
    const QString line = QStringLiteral("[%1] [%2] %3%4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  levelName(type),
                                  safeMessage,
                                  context.file ? QStringLiteral(" (%1:%2)")
                                                     .arg(QFileInfo(QString::fromUtf8(context.file)).fileName())
                                                     .arg(context.line)
                                               : QString());

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream stream(g_logFile);
        stream.setEncoding(QStringConverter::Utf8);
        stream << line << '\n';
        stream.flush();
    }

    // В stderr пишем всё, кроме отладочных сообщений без --verbose.
    if (type != QtDebugMsg || g_verbose)
        std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());

    const bool notify = (type == QtCriticalMsg || type == QtFatalMsg) && g_errorNotifier;
    if (notify) {
        // Копируем обработчик: он может быть заменён во время вызова
        const std::function<void(const QString &)> notifier = g_errorNotifier;
        locker.unlock();
        notifier(safeMessage);
    }

    if (type == QtFatalMsg) {
        std::fflush(stderr);
        abort();
    }
}

} // namespace

void install(bool verbose)
{
    g_verbose = verbose;

    // Каталог состояния (XDG_STATE_HOME, по умолчанию ~/.local/state)
    const QString stateDir = stateDirectory();

    if (QDir().mkpath(stateDir)) {
        const QString path = stateDir + QStringLiteral("/blackcore.log");
        const QFileInfo info(path);
        if (info.exists() && info.size() > kMaxLogSize) {
            QFile::remove(path + QStringLiteral(".1"));
            QFile::rename(path, path + QStringLiteral(".1"));
        }
        g_logFile = new QFile(path);
        if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            g_logPath = path;
        } else {
            delete g_logFile;
            g_logFile = nullptr;
        }
    }

    g_previousHandler = qInstallMessageHandler(messageHandler);
}

void shutdown()
{
    QMutexLocker locker(&g_mutex);
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;
    g_errorNotifier = nullptr;
    if (g_logFile) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

QString filePath()
{
    return g_logPath;
}

void setErrorNotifier(const std::function<void(const QString &)> &notifier)
{
    g_errorNotifier = notifier;
}

void info(const QString &message)
{
    qInfo().noquote() << message;
}

void warn(const QString &message)
{
    qWarning().noquote() << message;
}

void error(const QString &message)
{
    qCritical().noquote() << message;
}

} // namespace Log
} // namespace BlackCore
