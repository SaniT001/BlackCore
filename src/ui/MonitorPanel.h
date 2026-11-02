// SPDX-License-Identifier: MIT
#pragma once

#include "../core/Diagnostics.h"
#include "../core/SystemMonitor.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;

namespace BlackCore {

class HistoryGraph;

//! Панель монитора: показатели системы, процессы и диагностика.
/*!
 *  Все данные читаются из /proc и /sys с правами обычного пользователя.
 *  Панель ничего не изменяет в системе: кнопки только обновляют сведения или
 *  вставляют команду в приглашение терминала.
 */
class MonitorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorPanel(QWidget *parent = nullptr);

    //! Открыть раздел: 0 — обзор, 1 — процессы, 2 — диагностика
    void showSection(int index);
    int currentSection() const;

    //! Снять новый срез и обновить виджеты
    void refresh();

    //! Последняя сводка (для строки состояния и тестов)
    const SystemOverview &overview() const { return m_monitor.overview(); }

    //! Сколько точек истории накопилось
    int historyPoints() const { return m_monitor.cpuHistory().size(); }

    //! Обновить список процессов (по CPU или по памяти)
    void refreshProcesses(bool byMemory = false);
    QList<ProcessEntry> lastProcesses() const { return m_processes; }

    //! Выполнить диагностические проверки и показать результат
    void runDiagnostics();
    QList<DiagnosticCheck> lastChecks() const { return m_checks; }

    //! Обновить предпросмотр журнала и список отчётов о сбоях
    void refreshLog();
    void refreshCrashes();

    //! Период автообновления в миллисекундах
    void setRefreshInterval(int milliseconds);
    int refreshInterval() const;

signals:
    //! Вставить команду в приглашение активного терминала
    void commandRequested(const QString &command);
    //! Сообщение для строки состояния
    void statusMessage(const QString &text);
    //! Сводка обновилась (используется строкой состояния окна)
    void overviewUpdated();

private:
    QWidget *buildOverviewPage();
    QWidget *buildProcessPage();
    QWidget *buildDiagnosticsPage();

    void updateOverviewWidgets();
    void updateTemperatureTable();
    void fillChecksTable();
    void fillToolsTable();

    SystemMonitor m_monitor;
    SystemOverview m_overview;
    QList<ProcessEntry> m_processes;
    QList<DiagnosticCheck> m_checks;

    QTabWidget *m_tabs = nullptr;
    QTimer *m_timer = nullptr;

    // --- обзор ---
    HistoryGraph *m_graph = nullptr;
    QLabel *m_cpuValue = nullptr;
    QLabel *m_cpuDetail = nullptr;
    QProgressBar *m_cpuBar = nullptr;
    QLabel *m_memValue = nullptr;
    QLabel *m_memDetail = nullptr;
    QProgressBar *m_memBar = nullptr;
    QLabel *m_swapValue = nullptr;
    QProgressBar *m_swapBar = nullptr;
    QTableWidget *m_diskTable = nullptr;
    QLabel *m_netSummary = nullptr;
    QTableWidget *m_netTable = nullptr;
    QTableWidget *m_tempTable = nullptr;
    QLabel *m_tempEmpty = nullptr;
    QLabel *m_uptimeValue = nullptr;
    QLabel *m_kernelValue = nullptr;
    QLabel *m_distroValue = nullptr;
    QLabel *m_hostValue = nullptr;
    QLabel *m_connValue = nullptr;
    QLabel *m_loadValue = nullptr;

    // --- процессы ---
    QTableWidget *m_processTable = nullptr;
    QLabel *m_processSummary = nullptr;
    QPushButton *m_processCpuButton = nullptr;
    QPushButton *m_processMemoryButton = nullptr;

    // --- диагностика ---
    QTableWidget *m_checkTable = nullptr;
    QLabel *m_checkDetail = nullptr;
    QLabel *m_checkSummary = nullptr;
    QTableWidget *m_toolTable = nullptr;
    QLabel *m_logPathLabel = nullptr;
    QLabel *m_logHintLabel = nullptr;
    QPlainTextEdit *m_logPreview = nullptr;
    QListWidget *m_crashList = nullptr;
    QPlainTextEdit *m_crashDetail = nullptr;
    QLabel *m_crashSummary = nullptr;
};

} // namespace BlackCore
