// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QSize>
#include <QStringList>

#include <sys/types.h>

class QSocketNotifier;

namespace BlackCore {

//! Дочерний процесс, запущенный на псевдотерминале (PTY).
/*!
 *  Реализация: posix_openpt/grantpt/unlockpt + fork + setsid + TIOCSCTTY.
 *  Ввод-вывод — через QSocketNotifier в GUI-потоке (без блокирующих чтений).
 *  Завершение дочернего процесса отслеживается через SIGCHLD и self-pipe,
 *  что безопасно с точки зрения сигналов (без вызовов Qt в обработчике).
 */
class PtyProcess : public QObject
{
    Q_OBJECT

public:
    explicit PtyProcess(QObject *parent = nullptr);
    ~PtyProcess() override;

    //! Запуск программы. При ошибке возвращает false и заполняет errorMessage.
    bool start(const QString &program,
               const QStringList &arguments,
               const QString &workingDirectory,
               const QString &termType,
               QString *errorMessage);

    bool isRunning() const { return m_childPid > 0; }
    pid_t pid() const { return m_childPid; }

    //! Терминальный файл (например, /dev/pts/3) — полезно для диагностики
    QString ttyName() const { return m_ttyName; }

    //! Отправить данные в PTY. Возвращает число реально записанных байт (с учётом буфера).
    qint64 writeData(const QByteArray &data);

    //! Установить размер окна (строки/столбцы). Ядро само пошлёт SIGWINCH.
    void setWindowSize(int rows, int cols);
    QSize windowSize() const { return m_windowSize; }

    //! Аккуратно завершить процесс: SIGHUP (как у обычного терминала), затем SIGKILL по таймауту.
    void terminate();

    //! Немедленно завершить процесс сигналом
    void sendSignal(int signalNumber);

    //! Последний код возврата/сигнал (валидно после finished())
    int exitCode() const { return m_exitCode; }
    int exitSignal() const { return m_exitSignal; }
    QString exitDescription() const;

signals:
    void dataReceived(const QByteArray &data);
    void finished(int exitCode, int exitSignal);
    void errorOccurred(const QString &message);

private slots:
    void onMasterReadable();
    void onMasterWritable();

private:
    void closeMasterFd();
    void checkChildState();
    void flushWriteBuffer();
    void setWriteNotification(bool enabled);
    static void sigchldHandler(int signalNumber);

    int m_masterFd = -1;
    pid_t m_childPid = -1;
    QString m_ttyName;
    QSize m_windowSize{24, 80};

    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QSocketNotifier *m_sigchldNotifier = nullptr;

    QByteArray m_writeBuffer;

    int m_exitCode = 0;
    int m_exitSignal = 0;
    bool m_finishedEmitted = false;

    static int s_sigchldPipe[2];
    static bool s_handlerInstalled;
};

} // namespace BlackCore
