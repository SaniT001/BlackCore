// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <functional>
#include <QList>
#include <QObject>
#include <QString>

class QTimer;

namespace BlackCore {

//! Отслеживание выполнения команд, запущенных из BlackCore.
/*!
 *  Оболочка не сообщает о завершении команды, поэтому признаком завершения
 *  служит тишина в выводе терминала после запуска: как только вывод
 *  прекратился на quietMs, задача считается завершённой. Если вывода не было
 *  вовсе, задача закрывается по тому же таймауту с множителем. Это эвристика,
 *  и она используется только для уведомлений и истории, а не для управления
 *  процессами.
 */
class TaskTracker : public QObject
{
    Q_OBJECT

public:
    explicit TaskTracker(QObject *parent = nullptr);

    //! Тишина в выводе, после которой задача считается завершённой (по умолчанию 800 мс)
    void setQuietMs(int milliseconds);
    int quietMs() const { return m_quietMs; }

    //! Верхняя граница ожидания (по умолчанию 30 минут)
    void setMaxDurationMs(int milliseconds);
    int maxDurationMs() const { return m_maxDurationMs; }

    //! Проверка «панель занята» (у оболочки есть дочерние процессы).
    /*! Позволяет не закрывать задачу, пока команда действительно выполняется:
     *  тишина в выводе у долгой команды — это не завершение. */
    void setBusyCheck(std::function<bool(int terminalId)> check) { m_busyCheck = std::move(check); }

    //! Начать задачу; terminalId — идентификатор панели (для noteOutput), tag — метка (id истории)
    int begin(const QString &command, int terminalId = -1, const QString &tag = QString(),
              bool script = false);

    //! Отметить вывод в панели: продлевает ожидание всех задач этой панели
    void noteOutput(int terminalId);

    //! Завершить задачу досрочно (например, процесс оболочки закончился)
    void complete(int id, int exitCode = -1);

    void cancel(int id);
    void cancelAll();

    bool isActive(int id) const;
    int activeCount() const;
    int terminalId(int id) const;
    QString command(int id) const;
    bool isScript(int id) const;

    //! Проверить таймауты (вызывается таймером; в тестах можно вызвать вручную)
    void tick();

signals:
    //! Задача завершена; exitCode = -1, если код возврата неизвестен
    void finished(int id, const QString &command, const QString &tag, int durationMs,
                  int exitCode, bool script);

private:
    struct Task
    {
        int id = 0;
        QString command;
        QString tag;
        int terminalId = -1;
        QDateTime started;
        qint64 startedMs = 0;
        qint64 lastOutputMs = 0;
        bool sawOutput = false;
        bool script = false;

        bool operator==(const Task &other) const { return id == other.id; }
    };

    Task *find(int id);

    QList<Task> m_tasks;
    int m_nextId = 1;
    int m_quietMs = 800;
    int m_maxDurationMs = 30 * 60 * 1000;
    QTimer *m_timer = nullptr;
    std::function<bool(int)> m_busyCheck;
};

} // namespace BlackCore
