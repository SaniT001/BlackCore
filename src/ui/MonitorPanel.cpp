// SPDX-License-Identifier: MIT
#include "MonitorPanel.h"

#include "../core/CrashHandler.h"
#include "../core/Logger.h"
#include "../core/Theme.h"

#include <QCoreApplication>

#include <algorithm>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace BlackCore {

// ---------------------------------------------------------------------------
// График истории загрузки
// ---------------------------------------------------------------------------

//! Простой график истории: загрузка CPU и занятая память в процентах.
class HistoryGraph : public QWidget
{
public:
    explicit HistoryGraph(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(84);
        setMaximumHeight(110);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setObjectName(QStringLiteral("historyGraph"));
        setToolTip(tr("История загрузки CPU (золотая линия) и занятой памяти (серая линия)"));
    }

    void setSeries(const QVector<double> &cpu, const QVector<double> &memory)
    {
        m_cpu = cpu;
        m_memory = memory;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const BrandPalette colors = ThemeManager::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), colors.panel);

        const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(colors.active, 1));
        painter.drawRect(area);

        // Сетка: 25 / 50 / 75 %
        QPen grid(colors.active, 1, Qt::DashLine);
        painter.setPen(grid);
        for (int step = 1; step <= 3; ++step) {
            const qreal y = area.top() + area.height() * step / 4.0;
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        auto drawSeries = [&](const QVector<double> &values, const QColor &color, bool fill) {
            if (values.size() < 2)
                return;
            QPainterPath path;
            const qreal step = area.width() / qMax(1, m_points - 1);
            const qreal offset = area.width() - step * (values.size() - 1);
            for (int i = 0; i < values.size(); ++i) {
                const qreal x = area.left() + offset + step * i;
                const qreal y = area.bottom() - area.height() * qBound(0.0, values.at(i), 100.0) / 100.0;
                if (i == 0)
                    path.moveTo(x, y);
                else
                    path.lineTo(x, y);
            }
            if (fill) {
                QPainterPath closed = path;
                closed.lineTo(area.right(), area.bottom());
                closed.lineTo(area.left() + offset, area.bottom());
                closed.closeSubpath();
                QColor soft = color;
                soft.setAlpha(48);
                painter.fillPath(closed, soft);
            }
            painter.setPen(QPen(color, 1.6));
            painter.drawPath(path);
        };

        drawSeries(m_memory, colors.textSecondary, false);
        drawSeries(m_cpu, colors.gold, true);
    }

private:
    QVector<double> m_cpu;
    QVector<double> m_memory;
    int m_points = 120;
};

namespace {

QLabel *captionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", QStringLiteral("secondary"));
    label->setWordWrap(true);
    return label;
}

QLabel *valueLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("—"), parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() + 5);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

QLabel *mutedValueLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("—"), parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setProperty("role", QStringLiteral("secondary"));
    label->setWordWrap(true);
    return label;
}

QTableWidget *makeTable(const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setHighlightSections(false);
    table->setWordWrap(false);
    return table;
}

void setCell(QTableWidget *table, int row, int column, const QString &text, const QString &tip = QString())
{
    auto *item = new QTableWidgetItem(text);
    if (!tip.isEmpty())
        item->setToolTip(tip);
    table->setItem(row, column, item);
}

//! Колонки: одна растягивается, остальные — по содержимому (текст обрезается многоточием)
void stretchColumn(QTableWidget *table, int column)
{
    QHeaderView *header = table->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(column, QHeaderView::Stretch);
}

//! Компактная запись объёма для узких таблиц: без «,0»
QString compactBytes(qulonglong bytes)
{
    QString text = SystemMonitor::formatBytes(bytes);
    text.replace(QStringLiteral(",0 "), QStringLiteral(" "));
    return text;
}

//! Колонки фиксированной ширины, первая тянется (в узкой панели текст обрезается)
void setColumnWidths(QTableWidget *table, const QList<int> &widths)
{
    QHeaderView *header = table->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 0; column < widths.size() && column + 1 < table->columnCount(); ++column) {
        header->setSectionResizeMode(column + 1, QHeaderView::Fixed);
        table->setColumnWidth(column + 1, widths.at(column));
    }
}

//! Подогнать высоту таблицы под число строк (чтобы не занимала пустое место)
void fitTableHeight(QTableWidget *table, int maxRows = 6)
{
    const int rows = qMin(table->rowCount(), maxRows);
    const int rowsHeight = rows * qMax(18, table->verticalHeader()->defaultSectionSize());
    const int height = table->horizontalHeader()->height() + rowsHeight + 8;
    table->setFixedHeight(qMax(48, height));
}

QWidget *wrapScroll(QWidget *content, QWidget *parent)
{
    auto *area = new QScrollArea(parent);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(content);
    return area;
}

QColor colorForState(DiagnosticCheck::State state)
{
    const BrandPalette colors = ThemeManager::colors();
    switch (state) {
    case DiagnosticCheck::Ok:
        return colors.success;
    case DiagnosticCheck::Warning:
        return colors.gold;
    case DiagnosticCheck::Failed:
        return colors.error;
    case DiagnosticCheck::Skipped:
        return colors.textSecondary;
    }
    return colors.textSecondary;
}

} // namespace

// ---------------------------------------------------------------------------
// Конструктор
// ---------------------------------------------------------------------------

MonitorPanel::MonitorPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("monitorPanel"));
    setMinimumWidth(320);
    setMaximumWidth(520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    // Узкая панель: подписи вкладок обрезаются, а не растягивают панель
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->addTab(buildOverviewPage(), tr("Обзор"));
    m_tabs->addTab(buildProcessPage(), tr("Процессы"));
    m_tabs->addTab(buildDiagnosticsPage(), tr("Проверки"));
    layout->addWidget(m_tabs, 1);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1)
            refreshProcesses(false);
        else if (index == 2)
            runDiagnostics();
    });

    // Первый срез задаёт базу для расчёта процентов загрузки
    m_monitor.sample();
    refresh();

    m_timer = new QTimer(this);
    m_timer->setInterval(1500);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (!isVisible())
            return;
        refresh();
        if (m_tabs->currentIndex() == 1)
            refreshProcesses(false);
    });
    m_timer->start();
}

void MonitorPanel::setRefreshInterval(int milliseconds)
{
    if (m_timer)
        m_timer->setInterval(qBound(500, milliseconds, 10000));
}

int MonitorPanel::refreshInterval() const
{
    return m_timer ? m_timer->interval() : 0;
}

void MonitorPanel::showSection(int index)
{
    if (!m_tabs)
        return;
    m_tabs->setCurrentIndex(qBound(0, index, m_tabs->count() - 1));
}

int MonitorPanel::currentSection() const
{
    return m_tabs ? m_tabs->currentIndex() : 0;
}

// ---------------------------------------------------------------------------
// Обзор
// ---------------------------------------------------------------------------

QWidget *MonitorPanel::buildOverviewPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 4);
    layout->setSpacing(8);

    m_graph = new HistoryGraph(page);
    layout->addWidget(m_graph);

    // --- процессор ---
    auto *cpuGroup = new QGroupBox(tr("Процессор"), page);
    auto *cpuLayout = new QVBoxLayout(cpuGroup);
    m_cpuValue = valueLabel(cpuGroup);
    m_cpuBar = new QProgressBar(cpuGroup);
    m_cpuBar->setRange(0, 100);
    m_cpuBar->setTextVisible(false);
    m_cpuDetail = captionLabel(QString(), cpuGroup);
    cpuLayout->addWidget(m_cpuValue);
    cpuLayout->addWidget(m_cpuBar);
    cpuLayout->addWidget(m_cpuDetail);
    layout->addWidget(cpuGroup);

    // --- память ---
    auto *memGroup = new QGroupBox(tr("Память и подкачка"), page);
    auto *memLayout = new QVBoxLayout(memGroup);
    m_memValue = valueLabel(memGroup);
    m_memBar = new QProgressBar(memGroup);
    m_memBar->setRange(0, 100);
    m_memBar->setTextVisible(false);
    m_memDetail = captionLabel(QString(), memGroup);
    m_swapValue = mutedValueLabel(memGroup);
    m_swapBar = new QProgressBar(memGroup);
    m_swapBar->setRange(0, 100);
    m_swapBar->setTextVisible(false);
    memLayout->addWidget(m_memValue);
    memLayout->addWidget(m_memBar);
    memLayout->addWidget(m_memDetail);
    memLayout->addWidget(m_swapValue);
    memLayout->addWidget(m_swapBar);
    layout->addWidget(memGroup);

    // --- диски ---
    auto *diskGroup = new QGroupBox(tr("Диски"), page);
    auto *diskLayout = new QVBoxLayout(diskGroup);
    m_diskTable = makeTable({tr("Точка"), tr("Занято"), tr("Свободно"), tr("%")}, diskGroup);
    m_diskTable->setObjectName(QStringLiteral("diskTable"));
    m_diskTable->setMinimumHeight(96);
    setColumnWidths(m_diskTable, {84, 84, 46});
    diskLayout->addWidget(m_diskTable);
    layout->addWidget(diskGroup);

    // --- сеть ---
    auto *netGroup = new QGroupBox(tr("Сеть"), page);
    auto *netLayout = new QVBoxLayout(netGroup);
    m_netSummary = captionLabel(QString(), netGroup);
    m_netTable = makeTable({tr("Интерфейс"), tr("Приём"), tr("Передача")}, netGroup);
    m_netTable->setObjectName(QStringLiteral("netTable"));
    m_netTable->setMinimumHeight(84);
    setColumnWidths(m_netTable, {78, 78});
    netLayout->addWidget(m_netSummary);
    netLayout->addWidget(m_netTable);
    layout->addWidget(netGroup);

    // --- температуры ---
    auto *tempGroup = new QGroupBox(tr("Температуры"), page);
    auto *tempLayout = new QVBoxLayout(tempGroup);
    m_tempTable = makeTable({tr("Датчик"), tr("°C")}, tempGroup);
    m_tempTable->setObjectName(QStringLiteral("tempTable"));
    m_tempTable->setMinimumHeight(72);
    setColumnWidths(m_tempTable, {62});
    m_tempEmpty = captionLabel(QString(), tempGroup);
    tempLayout->addWidget(m_tempTable);
    tempLayout->addWidget(m_tempEmpty);
    layout->addWidget(tempGroup);

    // --- система ---
    auto *systemGroup = new QGroupBox(tr("Система"), page);
    auto *systemLayout = new QVBoxLayout(systemGroup);
    m_uptimeValue = mutedValueLabel(systemGroup);
    m_uptimeValue->setObjectName(QStringLiteral("uptimeValue"));
    m_kernelValue = mutedValueLabel(systemGroup);
    m_distroValue = mutedValueLabel(systemGroup);
    m_hostValue = mutedValueLabel(systemGroup);
    m_loadValue = mutedValueLabel(systemGroup);
    m_connValue = mutedValueLabel(systemGroup);
    systemLayout->addWidget(m_uptimeValue);
    systemLayout->addWidget(m_distroValue);
    systemLayout->addWidget(m_kernelValue);
    systemLayout->addWidget(m_hostValue);
    systemLayout->addWidget(m_loadValue);
    systemLayout->addWidget(m_connValue);
    layout->addWidget(systemGroup);

    auto *buttons = new QHBoxLayout;
    auto *psButton = new QPushButton(tr("Процессы в терминале"), page);
    psButton->setToolTip(tr("Вставить «ps aux --sort=-%cpu | head -20» в приглашение терминала"));
    connect(psButton, &QPushButton::clicked, this, [this]() {
        emit commandRequested(QStringLiteral("ps aux --sort=-%cpu | head -20"));
    });
    auto *dfButton = new QPushButton(tr("df -h"), page);
    dfButton->setToolTip(tr("Вставить «df -h» в приглашение терминала"));
    connect(dfButton, &QPushButton::clicked, this, [this]() {
        emit commandRequested(QStringLiteral("df -h"));
    });
    buttons->addWidget(psButton);
    buttons->addWidget(dfButton);
    layout->addLayout(buttons);
    layout->addStretch(1);

    return wrapScroll(page, this);
}

void MonitorPanel::updateOverviewWidgets()
{
    const SystemOverview &overview = m_overview;

    // --- CPU ---
    if (overview.cpu.valid) {
        m_cpuValue->setText(SystemMonitor::formatPercent(overview.cpu.total));
        m_cpuBar->setValue(int(overview.cpu.total + 0.5));
    } else {
        m_cpuValue->setText(tr("измеряется…"));
        m_cpuBar->setValue(0);
    }
    const int cores = overview.cpuCount > 0 ? overview.cpuCount : 1;
    m_cpuDetail->setText(tr("Ядер: %1 · средняя нагрузка: %2").arg(cores).arg(overview.loadText()));

    // --- память ---
    const MemoryInfo &memory = overview.memory;
    if (memory.valid) {
        m_memValue->setText(SystemMonitor::formatPercent(memory.usedPercent()));
        m_memBar->setValue(int(memory.usedPercent() + 0.5));
        m_memDetail->setText(tr("Занято %1 из %2 · доступно %3")
                                 .arg(SystemMonitor::formatBytes(memory.usedKb() * 1024ULL),
                                      SystemMonitor::formatBytes(memory.totalKb * 1024ULL),
                                      SystemMonitor::formatBytes(memory.availableKb * 1024ULL)));
    } else {
        m_memValue->setText(tr("нет данных"));
        m_memBar->setValue(0);
        m_memDetail->clear();
    }

    if (memory.swapTotalKb > 0) {
        m_swapValue->setText(tr("Подкачка: %1 / %2 · %3")
                                 .arg(SystemMonitor::formatBytes(memory.swapUsedKb() * 1024ULL),
                                      SystemMonitor::formatBytes(memory.swapTotalKb * 1024ULL),
                                      SystemMonitor::formatPercent(memory.swapUsedPercent())));
        m_swapBar->setValue(int(memory.swapUsedPercent() + 0.5));
        m_swapBar->setVisible(true);
    } else {
        m_swapValue->setText(tr("Подкачка не используется (swap не подключён)"));
        m_swapBar->setVisible(false);
    }

    // --- диски ---
    m_diskTable->setRowCount(overview.disks.size());
    for (int row = 0; row < overview.disks.size(); ++row) {
        const DiskUsage &disk = overview.disks.at(row);
        setCell(m_diskTable, row, 0, disk.mountPoint,
                tr("%1 · %2%3").arg(disk.device, disk.fsType,
                                    disk.readOnly ? tr(" · только чтение") : QString()));
        setCell(m_diskTable, row, 1, compactBytes(disk.used),
                tr("Всего %1 · доступно %2").arg(SystemMonitor::formatBytes(disk.total),
                                                SystemMonitor::formatBytes(disk.available)));
        setCell(m_diskTable, row, 2, compactBytes(disk.available));
        setCell(m_diskTable, row, 3, QStringLiteral("%1 %").arg(qRound(disk.usedPercent())));
        if (disk.usedPercent() >= 90.0)
            m_diskTable->item(row, 3)->setForeground(ThemeManager::colors().error);
    }
    fitTableHeight(m_diskTable, 8);

    // --- сеть ---
    m_netSummary->setText(tr("Приём: %1 · передача: %2")
                              .arg(SystemMonitor::formatRate(overview.networkRxRate()),
                                   SystemMonitor::formatRate(overview.networkTxRate())));
    QList<NetworkInterface> networks = overview.networks;
    std::sort(networks.begin(), networks.end(), [](const NetworkInterface &a, const NetworkInterface &b) {
        return (a.rxRate + a.txRate) > (b.rxRate + b.txRate);
    });
    m_netTable->setRowCount(networks.size());
    for (int row = 0; row < networks.size(); ++row) {
        const NetworkInterface &iface = networks.at(row);
        setCell(m_netTable, row, 0, iface.name,
                tr("%1 · всего принято %2, передано %3")
                    .arg(iface.up ? tr("поднят") : tr("не поднят"),
                         SystemMonitor::formatBytes(iface.rxBytes),
                         SystemMonitor::formatBytes(iface.txBytes)));
        setCell(m_netTable, row, 1, SystemMonitor::formatRate(iface.rxRate));
        setCell(m_netTable, row, 2, SystemMonitor::formatRate(iface.txRate));
    }
    fitTableHeight(m_netTable, 6);

    updateTemperatureTable();

    // --- система ---
    m_uptimeValue->setText(tr("Время работы: %1").arg(overview.uptimeText()));
    m_distroValue->setText(tr("Дистрибутив: %1").arg(overview.distro.isEmpty() ? tr("не определён") : overview.distro));
    m_kernelValue->setText(tr("Ядро: %1").arg(overview.kernel.isEmpty() ? tr("неизвестно") : overview.kernel));
    m_hostValue->setText(tr("Компьютер: %1").arg(overview.hostName.isEmpty() ? tr("—") : overview.hostName));
    m_loadValue->setText(tr("Нагрузка (1 / 5 / 15 мин): %1").arg(overview.loadText()));
    const ConnectionCounts &connections = overview.connections;
    m_connValue->setText(tr("Соединения: TCP установлено %1 · слушает %2 · UDP %3")
                             .arg(connections.established)
                             .arg(connections.listen)
                             .arg(connections.udp));

    m_graph->setSeries(m_monitor.cpuHistory(), m_monitor.memoryHistory());
}

void MonitorPanel::updateTemperatureTable()
{
    const QList<TemperatureReading> readings = m_overview.temperatures;
    m_tempTable->setRowCount(readings.size());
    for (int row = 0; row < readings.size(); ++row) {
        const TemperatureReading &reading = readings.at(row);
        setCell(m_tempTable, row, 0,
                QStringLiteral("%1 · %2").arg(reading.chip, reading.label), reading.source);
        setCell(m_tempTable, row, 1, QStringLiteral("%1 °C")
                                          .arg(QString::number(reading.celsius, 'f', 1)
                                                   .replace(QLatin1Char('.'), QLatin1Char(','))));
        if (reading.critical)
            m_tempTable->item(row, 1)->setForeground(ThemeManager::colors().error);
    }
    fitTableHeight(m_tempTable, 6);
    m_tempTable->setVisible(!readings.isEmpty());
    m_tempEmpty->setVisible(readings.isEmpty());
    if (readings.isEmpty())
        m_tempEmpty->setText(tr("Датчики температур не найдены: их нет в /sys/class/hwmon "
                                "и /sys/class/thermal (обычно так бывает в виртуалках). "
                                "Для lm-sensors: sudo apt install lm-sensors"));
}

// ---------------------------------------------------------------------------
// Процессы
// ---------------------------------------------------------------------------

QWidget *MonitorPanel::buildProcessPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 4);
    layout->setSpacing(8);

    auto *buttons = new QHBoxLayout;
    m_processCpuButton = new QPushButton(tr("По CPU"), page);
    m_processMemoryButton = new QPushButton(tr("По памяти"), page);
    auto *refreshButton = new QPushButton(tr("Обновить"), page);
    buttons->addWidget(m_processCpuButton);
    buttons->addWidget(m_processMemoryButton);
    buttons->addWidget(refreshButton);
    layout->addLayout(buttons);

    m_processSummary = captionLabel(QString(), page);
    layout->addWidget(m_processSummary);

    m_processTable = makeTable({tr("PID"), tr("Процесс"), tr("Пользователь"), tr("CPU"), tr("Память")}, page);
    m_processTable->setObjectName(QStringLiteral("processTable"));
    stretchColumn(m_processTable, 1);
    layout->addWidget(m_processTable, 1);

    auto *hint = captionLabel(tr("Данные читаются из /proc с правами обычного пользователя: "
                                 "процессы других пользователей видны без подробностей, "
                                 "но их потребление показывается."),
                              page);
    layout->addWidget(hint);

    auto *terminalButton = new QPushButton(tr("Вставить top в терминал"), page);
    connect(terminalButton, &QPushButton::clicked, this, [this]() {
        emit commandRequested(QStringLiteral("top"));
    });
    layout->addWidget(terminalButton);

    connect(m_processCpuButton, &QPushButton::clicked, this, [this]() { refreshProcesses(false); });
    connect(m_processMemoryButton, &QPushButton::clicked, this, [this]() { refreshProcesses(true); });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        // Второй опрос подряд даёт ненулевые проценты CPU
        m_monitor.topProcesses(10, false);
        refreshProcesses(false);
    });

    return page;
}

void MonitorPanel::refreshProcesses(bool byMemory)
{
    m_processes = m_monitor.topProcesses(12, byMemory);
    m_processTable->setRowCount(m_processes.size());
    for (int row = 0; row < m_processes.size(); ++row) {
        const ProcessEntry &process = m_processes.at(row);
        setCell(m_processTable, row, 0, QString::number(process.pid));
        setCell(m_processTable, row, 1, process.name);
        setCell(m_processTable, row, 2, process.user);
        setCell(m_processTable, row, 3, SystemMonitor::formatPercent(process.cpuPercent));
        setCell(m_processTable, row, 4, process.rssText());
    }

    m_processSummary->setText(tr("%1 самых ресурсоёмких из %2 процессов · сортировка: %3")
                                  .arg(m_processes.size())
                                  .arg(m_monitor.processCount())
                                  .arg(byMemory ? tr("по памяти") : tr("по CPU")));
}

// ---------------------------------------------------------------------------
// Диагностика
// ---------------------------------------------------------------------------

QWidget *MonitorPanel::buildDiagnosticsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 4);
    layout->setSpacing(8);

    // --- проверки ---
    auto *checkGroup = new QGroupBox(tr("Диагностические проверки"), page);
    auto *checkLayout = new QVBoxLayout(checkGroup);
    m_checkSummary = captionLabel(QString(), checkGroup);
    m_checkTable = makeTable({tr("Проверка"), tr("Состояние")}, checkGroup);
    m_checkTable->setObjectName(QStringLiteral("checkTable"));
    m_checkTable->setMinimumHeight(150);
    stretchColumn(m_checkTable, 0);
    m_checkTable->setColumnWidth(1, 96);
    m_checkDetail = captionLabel(QString(), checkGroup);
    m_checkDetail->setObjectName(QStringLiteral("checkDetail"));
    auto *checkButtons = new QHBoxLayout;
    auto *runButton = new QPushButton(tr("Выполнить проверки"), checkGroup);
    auto *copyButton = new QPushButton(tr("Отчёт в терминал"), checkGroup);
    copyButton->setToolTip(tr("Вставить сводку проверок в приглашение терминала"));
    checkButtons->addWidget(runButton);
    checkButtons->addWidget(copyButton);
    checkLayout->addWidget(m_checkSummary);
    checkLayout->addWidget(m_checkTable);
    checkLayout->addWidget(m_checkDetail);
    checkLayout->addLayout(checkButtons);
    layout->addWidget(checkGroup);

    // --- журнал ---
    auto *logGroup = new QGroupBox(tr("Журнал BlackCore"), page);
    auto *logLayout = new QVBoxLayout(logGroup);
    m_logPathLabel = captionLabel(QString(), logGroup);
    m_logPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_logPathLabel->setObjectName(QStringLiteral("logPathLabel"));
    m_logPreview = new QPlainTextEdit(logGroup);
    m_logPreview->setReadOnly(true);
    m_logPreview->setObjectName(QStringLiteral("logPreview"));
    m_logPreview->setMinimumHeight(120);
    m_logPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(mono.pointSizeF() - 1);
    m_logPreview->setFont(mono);
    m_logHintLabel = captionLabel(tr("Показаны последние строки. Полный журнал — кнопка «Открыть»."), logGroup);
    auto *logButtons = new QHBoxLayout;
    auto *logRefreshButton = new QPushButton(tr("Обновить"), logGroup);
    auto *logOpenButton = new QPushButton(tr("Открыть"), logGroup);
    auto *logTailButton = new QPushButton(tr("tail в терминале"), logGroup);
    logButtons->addWidget(logRefreshButton);
    logButtons->addWidget(logOpenButton);
    logButtons->addWidget(logTailButton);
    logLayout->addWidget(m_logPathLabel);
    logLayout->addWidget(m_logHintLabel);
    logLayout->addWidget(m_logPreview);
    logLayout->addLayout(logButtons);
    layout->addWidget(logGroup);

    // --- сбои ---
    auto *crashGroup = new QGroupBox(tr("Сбои приложения"), page);
    auto *crashLayout = new QVBoxLayout(crashGroup);
    m_crashSummary = captionLabel(QString(), crashGroup);
    m_crashList = new QListWidget(crashGroup);
    m_crashList->setObjectName(QStringLiteral("crashList"));
    m_crashList->setMaximumHeight(90);
    m_crashDetail = new QPlainTextEdit(crashGroup);
    m_crashDetail->setReadOnly(true);
    m_crashDetail->setObjectName(QStringLiteral("crashDetail"));
    m_crashDetail->setMinimumHeight(90);
    m_crashDetail->setFont(mono);
    auto *crashButtons = new QHBoxLayout;
    auto *crashClearButton = new QPushButton(tr("Очистить отчёты"), crashGroup);
    auto *crashDirButton = new QPushButton(tr("Каталог отчётов"), crashGroup);
    crashButtons->addWidget(crashClearButton);
    crashButtons->addWidget(crashDirButton);
    crashLayout->addWidget(m_crashSummary);
    crashLayout->addWidget(m_crashList);
    crashLayout->addWidget(m_crashDetail);
    crashLayout->addLayout(crashButtons);
    layout->addWidget(crashGroup);

    // --- инструменты ---
    auto *toolGroup = new QGroupBox(tr("Системные инструменты"), page);
    auto *toolLayout = new QVBoxLayout(toolGroup);
    m_toolTable = makeTable({tr("Инструмент"), tr("Наличие"), tr("Версия и путь")}, toolGroup);
    m_toolTable->setObjectName(QStringLiteral("toolTable"));
    m_toolTable->setMinimumHeight(150);
    stretchColumn(m_toolTable, 2);
    toolLayout->addWidget(m_toolTable);
    toolLayout->addWidget(captionLabel(tr("BlackCore ничего не устанавливает автоматически: "
                                          "отсутствующий инструмент просто отмечен здесь, "
                                          "установите его штатным менеджером пакетов, если нужно."),
                                       toolGroup));
    layout->addWidget(toolGroup);
    layout->addStretch(1);

    connect(m_checkTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                if (row < 0 || row >= m_checks.size()) {
                    m_checkDetail->clear();
                    return;
                }
                const DiagnosticCheck &check = m_checks.at(row);
                m_checkDetail->setText(check.hint.isEmpty()
                                           ? check.detail
                                           : QStringLiteral("%1 — %2").arg(check.detail, check.hint));
            });
    connect(runButton, &QPushButton::clicked, this, [this]() { runDiagnostics(); });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        QStringList lines;
        lines.append(tr("# Диагностика BlackCore"));
        for (const DiagnosticCheck &check : std::as_const(m_checks)) {
            lines.append(QStringLiteral("# [%1] %2 — %3")
                             .arg(check.stateText(), check.title, check.detail));
        }
        emit commandRequested(QStringLiteral("# сводка диагностики скопирована ниже\n")
                              + QStringList(lines.mid(0, 40)).join(QLatin1Char('\n')));
    });
    connect(logRefreshButton, &QPushButton::clicked, this, [this]() { refreshLog(); });
    connect(logOpenButton, &QPushButton::clicked, this, [this]() {
        const QString path = Diagnostics::logPath();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            emit statusMessage(tr("Файл журнала ещё не создан"));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(logTailButton, &QPushButton::clicked, this, [this]() {
        const QString path = Diagnostics::logPath();
        if (path.isEmpty()) {
            emit statusMessage(tr("Журнал пишется только в stderr"));
            return;
        }
        emit commandRequested(QStringLiteral("tail -n 100 %1").arg(path));
    });
    connect(m_crashList, &QListWidget::currentRowChanged, this, [this](int row) {
        const QList<QString> files = Crash::reportFiles();
        if (row < 0 || row >= files.size())
            return;
        m_crashDetail->setPlainText(Crash::readReport(files.at(row)));
    });
    connect(crashClearButton, &QPushButton::clicked, this, [this]() {
        if (Crash::clearReports()) {
            emit statusMessage(tr("Отчёты о сбоях удалены"));
            refreshCrashes();
        }
    });
    connect(crashDirButton, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Crash::reportsDir()));
    });

    return wrapScroll(page, this);
}

void MonitorPanel::runDiagnostics()
{
    m_checks = Diagnostics::runChecks();
    fillChecksTable();
    fillToolsTable();
    refreshLog();
    refreshCrashes();
    m_checkSummary->setText(Diagnostics::summarize(m_checks));
    if (!m_checks.isEmpty()) {
        m_checkTable->setCurrentCell(0, 0);
        m_checkDetail->setText(m_checks.first().detail);
    }
    emit statusMessage(Diagnostics::summarize(m_checks));
}

void MonitorPanel::fillChecksTable()
{
    m_checkTable->setRowCount(m_checks.size());
    for (int row = 0; row < m_checks.size(); ++row) {
        const DiagnosticCheck &check = m_checks.at(row);
        const QString detail = check.hint.isEmpty()
            ? check.detail
            : QStringLiteral("%1 — %2").arg(check.detail, check.hint);
        setCell(m_checkTable, row, 0, check.title, detail);
        setCell(m_checkTable, row, 1, check.stateText(), detail);
        const QColor color = colorForState(check.state);
        m_checkTable->item(row, 0)->setForeground(ThemeManager::colors().textPrimary);
        m_checkTable->item(row, 1)->setForeground(color);
    }
    fitTableHeight(m_checkTable, 24);
}

void MonitorPanel::fillToolsTable()
{
    const QList<ToolInfo> tools = Diagnostics::tools(false);
    stretchColumn(m_toolTable, 2);
    m_toolTable->setColumnWidth(0, 110);
    m_toolTable->setRowCount(tools.size());
    for (int row = 0; row < tools.size(); ++row) {
        const ToolInfo &tool = tools.at(row);
        setCell(m_toolTable, row, 0, tool.name, tool.purpose);
        setCell(m_toolTable, row, 1, tool.available() ? tr("есть") : (tool.required ? tr("нет") : tr("не нужен")));
        setCell(m_toolTable, row, 2, tool.available() ? tool.path : tr("не найден в PATH"));
        const QColor color = tool.available() ? ThemeManager::colors().success
                             : tool.required ? ThemeManager::colors().error
                                             : ThemeManager::colors().textSecondary;
        m_toolTable->item(row, 1)->setForeground(color);
    }
    fitTableHeight(m_toolTable, 16);
}

void MonitorPanel::refreshLog()
{
    const QString path = Diagnostics::logPath();
    if (path.isEmpty()) {
        m_logPathLabel->setText(tr("Журнал в файл недоступен"));
        m_logPreview->setPlainText(tr("Запустите BlackCore из терминала: сообщения будут видны в stderr."));
        return;
    }
    m_logPathLabel->setText(tr("%1 · %2").arg(path, SystemMonitor::formatBytes(qulonglong(Diagnostics::logSize()))));
    const QString tail = Diagnostics::tailLog(200);
    m_logPreview->setPlainText(tail.isEmpty() ? tr("Журнал пуст.") : tail);
    auto *scroll = m_logPreview->verticalScrollBar();
    scroll->setValue(scroll->maximum());
}

void MonitorPanel::refreshCrashes()
{
    const QList<QString> files = Crash::reportFiles();
    m_crashList->clear();
    for (const QString &file : files) {
        const QFileInfo info(file);
        m_crashList->addItem(tr("%1 · %2")
                                 .arg(info.fileName(),
                                      info.lastModified().toString(QStringLiteral("dd.MM.yyyy HH:mm"))));
    }
    if (files.isEmpty()) {
        m_crashSummary->setText(Crash::previousRunCrashed()
                                    ? tr("Отчётов нет, но предыдущий запуск завершился не штатно")
                                    : tr("Сбоев не зарегистрировано"));
        m_crashDetail->clear();
    } else {
        m_crashSummary->setText(tr("Отчётов: %1 · последний: %2")
                                    .arg(files.size())
                                    .arg(QFileInfo(files.first()).fileName()));
        m_crashList->setCurrentRow(0);
        m_crashDetail->setPlainText(Crash::readReport(files.first()));
    }
}

// ---------------------------------------------------------------------------
// Общее обновление
// ---------------------------------------------------------------------------

void MonitorPanel::refresh()
{
    m_overview = m_monitor.sample();
    updateOverviewWidgets();
    emit overviewUpdated();
}

} // namespace BlackCore
