// SPDX-License-Identifier: MIT
#include "TaskTracker.h"

#include <QTimer>

namespace BlackCore {

TaskTracker::TaskTracker(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &TaskTracker::tick);
    m_timer->start();
}

void TaskTracker::setQuietMs(int milliseconds)
{
    m_quietMs = qBound(100, milliseconds, 60000);
}

void TaskTracker::setMaxDurationMs(int milliseconds)
{
    m_maxDurationMs = qBound(1000, milliseconds, 24 * 3600 * 1000);
}

int TaskTracker::begin(const QString &command, int terminalId, const QString &tag, bool script)
{
    Task task;
    task.id = m_nextId++;
    task.command = command;
    task.tag = tag;
    task.terminalId = terminalId;
    task.started = QDateTime::currentDateTime();
    task.startedMs = task.started.toMSecsSinceEpoch();
    task.lastOutputMs = task.startedMs;
    task.script = script;
    m_tasks.append(task);
    return task.id;
}

TaskTracker::Task *TaskTracker::find(int id)
{
    for (Task &task : m_tasks) {
        if (task.id == id)
            return &task;
    }
    return nullptr;
}

void TaskTracker::noteOutput(int terminalId)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Task &task : m_tasks) {
        if (task.terminalId != terminalId)
            continue;
        task.lastOutputMs = now;
        task.sawOutput = true;
    }
}

void TaskTracker::complete(int id, int exitCode)
{
    Task *task = find(id);
    if (!task)
        return;

    const int durationMs = int(QDateTime::currentMSecsSinceEpoch() - task->startedMs);
    const QString command = task->command;
    const QString tag = task->tag;
    const bool script = task->script;
    m_tasks.removeAll(*task);
    emit finished(id, command, tag, qMax(0, durationMs), exitCode, script);
}

void TaskTracker::cancel(int id)
{
    Task *task = find(id);
    if (!task)
        return;
    m_tasks.removeAll(*task);
}

void TaskTracker::cancelAll()
{
    m_tasks.clear();
}

bool TaskTracker::isActive(int id) const
{
    for (const Task &task : m_tasks) {
        if (task.id == id)
            return true;
    }
    return false;
}

int TaskTracker::activeCount() const
{
    return m_tasks.size();
}

int TaskTracker::terminalId(int id) const
{
    for (const Task &task : m_tasks) {
        if (task.id == id)
            return task.terminalId;
    }
    return -1;
}

QString TaskTracker::command(int id) const
{
    for (const Task &task : m_tasks) {
        if (task.id == id)
            return task.command;
    }
    return QString();
}

bool TaskTracker::isScript(int id) const
{
    for (const Task &task : m_tasks) {
        if (task.id == id)
            return task.script;
    }
    return false;
}

void TaskTracker::tick()
{
    if (m_tasks.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<int> finishedIds;
    for (const Task &task : m_tasks) {
        const qint64 quiet = m_quietMs * (task.sawOutput ? 1 : 4);
        if (now - task.lastOutputMs >= quiet) {
            // Долгая команда может молчать: пока у оболочки есть дочерние
            // процессы, задача продолжается
            const bool busy = m_busyCheck && m_busyCheck(task.terminalId);
            if (!busy)
                finishedIds.append(task.id);
        }
        else if (now - task.startedMs >= m_maxDurationMs)
            finishedIds.append(task.id);
    }
    for (int id : finishedIds)
        complete(id, -1);
}

} // namespace BlackCore
