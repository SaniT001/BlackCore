// SPDX-License-Identifier: MIT
#include "PtyProcess.h"

#include "../core/AppInfo.h"
#include "../core/Logger.h"

#include <QFileInfo>
#include <QSize>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char **environ;

namespace BlackCore {
namespace {

constexpr qsizetype kMaxWriteBuffer = 8 * 1024 * 1024; // 8 MiB
//! Сколько байт вывода читать за одну активацию сокета (остальное — следующим тиком)
constexpr qint64 kReadChunkBytes = 32 * 1024;

void setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void setCloexec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

QByteArray execFailureMessage(const QString &program)
{
    return QStringLiteral("Не удалось запустить программу '%1': %2")
        .arg(program, QString::fromLocal8Bit(std::strerror(errno)))
        .toUtf8();
}

} // namespace

int PtyProcess::s_sigchldPipe[2] = {-1, -1};
bool PtyProcess::s_handlerInstalled = false;

namespace {
QList<PtyProcess *> *registry()
{
    static QList<PtyProcess *> instances;
    return &instances;
}
QSocketNotifier *g_sigchldNotifier = nullptr;
} // namespace

PtyProcess::PtyProcess(QObject *parent)
    : QObject(parent)
{
    registry()->append(this);
}

PtyProcess::~PtyProcess()
{
    registry()->removeAll(this);
    if (isRunning()) {
        ::kill(m_childPid, SIGHUP);
        ::kill(m_childPid, SIGCONT);
        // Дадим процессу до 500 мс на корректное завершение
        for (int i = 0; i < 50; ++i) {
            int status = 0;
            const pid_t r = ::waitpid(m_childPid, &status, WNOHANG);
            if (r == m_childPid || (r < 0 && errno == ECHILD))
                break;
            ::usleep(10 * 1000);
        }
        if (::waitpid(m_childPid, nullptr, WNOHANG) == 0)
            ::kill(m_childPid, SIGKILL);
    }
    closeMasterFd();
}

bool PtyProcess::start(const QString &program,
                       const QStringList &arguments,
                       const QString &workingDirectory,
                       const QString &termType,
                       QString *errorMessage)
{
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        Log::error(message);
        return false;
    };

    if (isRunning())
        return fail(tr("Процесс уже запущен"));
    if (program.isEmpty())
        return fail(tr("Не задана программа для запуска"));

    const QFileInfo programInfo(program);
    if (!programInfo.exists())
        return fail(tr("Оболочка не найдена: %1").arg(program));
    if (!programInfo.isExecutable())
        return fail(tr("Файл не является исполняемым: %1").arg(program));

    // --- создание псевдотерминала ---
    const int masterFd = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (masterFd < 0)
        return fail(tr("posix_openpt(): %1").arg(QString::fromLocal8Bit(std::strerror(errno))));

    if (::grantpt(masterFd) != 0 || ::unlockpt(masterFd) != 0) {
        const QString message = tr("grantpt/unlockpt: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(masterFd);
        return fail(message);
    }

    const char *slaveNameRaw = ::ptsname(masterFd);
    if (!slaveNameRaw) {
        ::close(masterFd);
        return fail(tr("ptsname(): %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    }
    const QString slaveName = QString::fromLocal8Bit(slaveNameRaw);

    const int slaveFd = ::open(slaveNameRaw, O_RDWR | O_NOCTTY);
    if (slaveFd < 0) {
        const QString message = tr("Не удалось открыть %1: %2")
                                    .arg(slaveName, QString::fromLocal8Bit(std::strerror(errno)));
        ::close(masterFd);
        return fail(message);
    }

    struct winsize ws = {};
    ws.ws_row = static_cast<unsigned short>(m_windowSize.height());
    ws.ws_col = static_cast<unsigned short>(m_windowSize.width());
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ::ioctl(slaveFd, TIOCSWINSZ, &ws);

    // --- подготовка аргументов до fork (malloc после fork недопустим) ---
    QByteArray programPath = programInfo.absoluteFilePath().toLocal8Bit();
    QByteArray workingDir = workingDirectory.toLocal8Bit();
    QByteArray term = termType.toLocal8Bit();

    QList<QByteArray> argStorage = programInfo.absoluteFilePath().toLocal8Bit().split('\0');
    QByteArray argv0 = programInfo.fileName().toLocal8Bit();
    argStorage = {argv0};
    for (const QString &argument : arguments)
        argStorage.append(argument.toLocal8Bit());

    QList<char *> argv;
    argv.reserve(argStorage.size() + 1);
    for (QByteArray &argument : argStorage)
        argv.append(argument.data());
    argv.append(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        const QString message = tr("fork(): %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(slaveFd);
        ::close(masterFd);
        return fail(message);
    }

    if (pid == 0) {
        // -------- дочерний процесс --------
        ::close(masterFd);

        ::setsid();
        if (::ioctl(slaveFd, TIOCSCTTY, 0) != 0) {
            const QByteArray message = QByteArray("TIOCSCTTY failed: ") + std::strerror(errno) + '\n';
            ssize_t ignored = ::write(STDERR_FILENO, message.constData(), static_cast<size_t>(message.size()));
            (void)ignored;
        }

        if (::dup2(slaveFd, STDIN_FILENO) < 0 || ::dup2(slaveFd, STDOUT_FILENO) < 0
            || ::dup2(slaveFd, STDERR_FILENO) < 0) {
            _exit(126);
        }
        if (slaveFd > STDERR_FILENO)
            ::close(slaveFd);

        // Сброс обработчиков сигналов, унаследованных от родителя
        struct sigaction defaultAction = {};
        defaultAction.sa_handler = SIG_DFL;
        sigemptyset(&defaultAction.sa_mask);
        for (const int signalNumber : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGCHLD, SIGPIPE, SIGTSTP,
                                       SIGTTIN, SIGTTOU, SIGCONT, SIGWINCH, SIGUSR1, SIGUSR2}) {
            ::sigaction(signalNumber, &defaultAction, nullptr);
        }
        sigset_t emptyMask;
        sigemptyset(&emptyMask);
        ::sigprocmask(SIG_SETMASK, &emptyMask, nullptr);

        // Переменные окружения терминала
        ::setenv("TERM", term.constData(), 1);
        ::setenv("COLORTERM", "truecolor", 1);
        ::setenv("TERM_PROGRAM", "BlackCore", 1);
        ::setenv("TERM_PROGRAM_VERSION", kAppVersion, 1);
        ::unsetenv("COLUMNS");
        ::unsetenv("LINES");
        ::unsetenv("QT_QPA_PLATFORM");

        if (!workingDir.isEmpty())
            (void)::chdir(workingDir.constData());

        ::execve(programPath.constData(), argv.data(), environ);

        const QByteArray message = execFailureMessage(program);
        ssize_t ignored = ::write(STDERR_FILENO, message.constData(), static_cast<size_t>(message.size()));
        (void)ignored;
        _exit(127);
    }

    // -------- родительский процесс --------
    ::close(slaveFd);

    m_masterFd = masterFd;
    m_childPid = pid;
    m_ttyName = slaveName;
    m_exitCode = 0;
    m_exitSignal = 0;
    m_finishedEmitted = false;
    m_writeBuffer.clear();

    setNonBlocking(m_masterFd);
    setCloexec(m_masterFd);

    m_readNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &PtyProcess::onMasterReadable);

    m_writeNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &PtyProcess::onMasterWritable);

    // --- SIGCHLD через self-pipe (безопасно в обработчике сигнала) ---
    if (!s_handlerInstalled) {
        int fds[2] = {-1, -1};
        if (::pipe(fds) == 0) {
            s_sigchldPipe[0] = fds[0];
            s_sigchldPipe[1] = fds[1];
            setNonBlocking(s_sigchldPipe[0]);
            setNonBlocking(s_sigchldPipe[1]);
            setCloexec(s_sigchldPipe[0]);
            setCloexec(s_sigchldPipe[1]);

            struct sigaction action = {};
            action.sa_handler = &PtyProcess::sigchldHandler;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
            if (::sigaction(SIGCHLD, &action, nullptr) == 0)
                s_handlerInstalled = true;
        }
    }

    if (s_handlerInstalled && !g_sigchldNotifier) {
        g_sigchldNotifier = new QSocketNotifier(s_sigchldPipe[0], QSocketNotifier::Read);
        QObject::connect(g_sigchldNotifier, &QSocketNotifier::activated, g_sigchldNotifier, []() {
            char buffer[64];
            while (::read(s_sigchldPipe[0], buffer, sizeof(buffer)) > 0) { }
            const QList<PtyProcess *> instances = *registry();
            for (PtyProcess *process : instances)
                process->checkChildState();
        });
    }

    Log::info(tr("Терминал запущен: %1 (pid %2, tty %3, shell %4)")
                  .arg(termType)
                  .arg(pid)
                  .arg(slaveName, programInfo.absoluteFilePath()));
    return true;
}

void PtyProcess::closeMasterFd()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier = nullptr;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        m_writeNotifier->deleteLater();
        m_writeNotifier = nullptr;
    }
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    m_writeBuffer.clear();
}

void PtyProcess::onMasterReadable()
{
    if (m_masterFd < 0)
        return;

    // За одну активацию читаем не больше kReadChunkBytes: при большом выводе
    // (например, seq 1 100000) иначе весь поток обрабатывался бы в одном
    // вызове обработчика и интерфейс «замирал». Остаток дочитывается
    // отложенным вызовом, и цикл событий успевает обработать отрисовку и ввод.
    char buffer[65536];
    qint64 total = 0;
    bool more = false;
    for (;;) {
        const ssize_t bytesRead = ::read(m_masterFd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            emit dataReceived(QByteArray(buffer, static_cast<int>(bytesRead)));
            total += bytesRead;
            if (total >= kReadChunkBytes) {
                more = true;
                break;
            }
            continue;
        }
        if (bytesRead == 0) {
            closeMasterFd();
            break;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        if (errno == EIO) {
            // Slave-устройство закрыто (процесс завершился)
            closeMasterFd();
            break;
        }
        Log::warn(tr("Чтение из PTY: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        closeMasterFd();
        break;
    }

    if (more) {
        // Возвращаемся в цикл событий и продолжаем чтение следующим тиком
        QTimer::singleShot(0, this, &PtyProcess::onMasterReadable);
    }
}

void PtyProcess::onMasterWritable()
{
    flushWriteBuffer();
}

void PtyProcess::setWriteNotification(bool enabled)
{
    if (m_writeNotifier && m_writeNotifier->isEnabled() != enabled)
        m_writeNotifier->setEnabled(enabled);
}

qint64 PtyProcess::writeData(const QByteArray &data)
{
    if (m_masterFd < 0 || !isRunning()) {
        Log::warn(tr("Попытка записи в закрытый PTY (%1 байт)").arg(data.size()));
        return -1;
    }
    if (m_writeBuffer.size() + data.size() > kMaxWriteBuffer) {
        Log::warn(tr("Буфер ввода переполнен (%1 байт) — данные отброшены").arg(m_writeBuffer.size()));
        return -1;
    }
    m_writeBuffer.append(data);
    flushWriteBuffer();
    return data.size();
}

void PtyProcess::flushWriteBuffer()
{
    while (!m_writeBuffer.isEmpty() && m_masterFd >= 0) {
        const ssize_t written = ::write(m_masterFd, m_writeBuffer.constData(),
                                        static_cast<size_t>(m_writeBuffer.size()));
        if (written > 0) {
            m_writeBuffer.remove(0, static_cast<int>(written));
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            setWriteNotification(true);
            return;
        }
        if (written < 0 && errno == EIO) {
            m_writeBuffer.clear();
            return;
        }
        Log::warn(tr("Запись в PTY: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        m_writeBuffer.clear();
        return;
    }
    setWriteNotification(false);
}

void PtyProcess::setWindowSize(int rows, int cols)
{
    rows = qMax(1, rows);
    cols = qMax(1, cols);
    if (m_windowSize.height() == rows && m_windowSize.width() == cols)
        return;

    m_windowSize = QSize(cols, rows);
    if (m_masterFd >= 0) {
        struct winsize ws = {};
        ws.ws_row = static_cast<unsigned short>(rows);
        ws.ws_col = static_cast<unsigned short>(cols);
        if (::ioctl(m_masterFd, TIOCSWINSZ, &ws) != 0)
            Log::warn(tr("TIOCSWINSZ: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    }
}

void PtyProcess::sendSignal(int signalNumber)
{
    if (isRunning())
        ::kill(m_childPid, signalNumber);
}

void PtyProcess::terminate()
{
    if (!isRunning())
        return;

    Log::info(tr("Завершение процесса %1 (SIGHUP)").arg(m_childPid));
    ::kill(m_childPid, SIGHUP);
    ::kill(m_childPid, SIGCONT);

    const pid_t pidToKill = m_childPid;
    QTimer::singleShot(2000, this, [this, pidToKill]() {
        if (isRunning() && m_childPid == pidToKill) {
            Log::warn(tr("Процесс %1 не завершился по SIGHUP, отправлен SIGKILL").arg(pidToKill));
            ::kill(pidToKill, SIGKILL);
        }
    });
}

void PtyProcess::sigchldHandler(int signalNumber)
{
    (void)signalNumber;
    const int savedErrno = errno;
    const char byte = 1;
    const ssize_t ignored = ::write(s_sigchldPipe[1], &byte, 1);
    (void)ignored;
    errno = savedErrno;
}

void PtyProcess::checkChildState()
{
    if (m_childPid <= 0)
        return;

    int status = 0;
    const pid_t result = ::waitpid(m_childPid, &status, WNOHANG);
    if (result != m_childPid)
        return;

    const pid_t finishedPid = m_childPid;
    m_childPid = -1;
    m_exitCode = 0;
    m_exitSignal = 0;
    if (WIFEXITED(status))
        m_exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        m_exitSignal = WTERMSIG(status);

    // Считываем остатки вывода, затем закрываем PTY
    onMasterReadable();
    closeMasterFd();

    Log::info(tr("Процесс %1 завершён: %2").arg(finishedPid).arg(exitDescription()));

    if (!m_finishedEmitted) {
        m_finishedEmitted = true;
        emit finished(m_exitCode, m_exitSignal);
    }
}

QString PtyProcess::exitDescription() const
{
    if (m_exitSignal != 0) {
        const char *name = ::strsignal(m_exitSignal);
        return tr("завершён сигналом %1 (%2)")
            .arg(m_exitSignal)
            .arg(name ? QString::fromLocal8Bit(name) : QStringLiteral("?"));
    }
    if (m_exitCode == 0)
        return tr("код возврата 0");
    return tr("код возврата %1").arg(m_exitCode);
}

} // namespace BlackCore
