// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace BlackCore {

//! Накопительные счётчики времени CPU из /proc/stat (в тиках)
struct CpuSample
{
    QString name;                 //!< «cpu» — суммарно, «cpu0» … — по ядрам
    qulonglong user = 0;
    qulonglong nice = 0;
    qulonglong system = 0;
    qulonglong idle = 0;
    qulonglong iowait = 0;
    qulonglong irq = 0;
    qulonglong softirq = 0;
    qulonglong steal = 0;

    qulonglong total() const;
    qulonglong idleTotal() const;
};

//! Загрузка CPU в процентах: суммарно и по ядрам
struct CpuUsage
{
    double total = 0.0;
    QVector<double> cores;
    bool valid = false;

    QString perCoreText() const;
};

//! Память и подкачка из /proc/meminfo (значения в КиБ)
struct MemoryInfo
{
    qulonglong totalKb = 0;
    qulonglong availableKb = 0;
    qulonglong freeKb = 0;
    qulonglong buffersKb = 0;
    qulonglong cachedKb = 0;
    qulonglong swapTotalKb = 0;
    qulonglong swapFreeKb = 0;
    bool valid = false;

    qulonglong usedKb() const;
    qulonglong swapUsedKb() const;
    double usedPercent() const;
    double swapUsedPercent() const;
};

//! Занятое место на точке монтирования
struct DiskUsage
{
    QString device;
    QString mountPoint;
    QString fsType;
    qulonglong total = 0;      //!< всего байт
    qulonglong used = 0;       //!< занято байт
    qulonglong available = 0;  //!< доступно обычному пользователю

    double usedPercent() const;
    bool readOnly = false;
};

//! Показание температурного датчика (/sys/class/hwmon, /sys/class/thermal)
struct TemperatureReading
{
    QString chip;      //!< имя чипа: coretemp, k10temp, nvme, acpitz …
    QString label;     //!< подпись датчика: Package id 0, Core 0, Composite …
    double celsius = 0.0;
    QString source;    //!< путь к файлу показания

    bool critical = false; //!< выше порога внимания
};

//! Счётчики сетевого интерфейса
struct NetworkInterface
{
    QString name;
    qulonglong rxBytes = 0;
    qulonglong txBytes = 0;
    qulonglong rxRate = 0; //!< байт/с с прошлого опроса
    qulonglong txRate = 0;
    bool up = false;
};

//! Состояние сетевых подключений по /proc/net/{tcp,tcp6,udp,udp6}
struct ConnectionCounts
{
    int established = 0;
    int listen = 0;
    int timeWait = 0;
    int otherTcp = 0;
    int udp = 0;

    int tcpTotal() const { return established + listen + timeWait + otherTcp; }
    int total() const { return tcpTotal() + udp; }
};

//! Строка списка процессов
struct ProcessEntry
{
    int pid = 0;
    QString name;
    QString user;
    double cpuPercent = 0.0;
    qulonglong rssKb = 0;

    QString rssText() const;
};

//! Сводка о системе для панели монитора
struct SystemOverview
{
    QString kernel;         //!< версия ядра
    QString distro;         //!< название дистрибутива («Linux Mint 22.3»)
    QString distroId;       //!< идентификатор («linuxmint»)
    QString hostName;
    qint64 uptimeSeconds = 0;
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;
    int cpuCount = 0;
    CpuUsage cpu;
    MemoryInfo memory;
    QList<DiskUsage> disks;
    QList<TemperatureReading> temperatures;
    QList<NetworkInterface> networks;
    ConnectionCounts connections;
    QDateTime sampledAt;
    bool valid = false;

    QString uptimeText() const;
    QString loadText() const;
    qulonglong networkRxRate() const;
    qulonglong networkTxRate() const;
};

//! Сбор системной информации из /proc и /sys.
/*!
 *  Всё чтение выполняется с правами обычного пользователя: используются только
 *  /proc, /sys и statvfs. Никакие команды не запускаются, root не требуется.
 *  Разбор вынесен в статические функции, чтобы его можно было проверить на
 *  подготовленных строках в тестах.
 */
class SystemMonitor
{
public:
    SystemMonitor();

    //! Каталоги-источники (по умолчанию /proc и /sys); подменяются в тестах
    void setRoots(const QString &procRoot, const QString &sysRoot);
    QString procRoot() const { return m_procRoot; }
    QString sysRoot() const { return m_sysRoot; }

    //! Сколько точек истории держать (по умолчанию 120)
    void setHistorySize(int points);
    int historySize() const { return m_historySize; }

    const QVector<double> &cpuHistory() const { return m_cpuHistory; }
    const QVector<double> &memoryHistory() const { return m_memoryHistory; }
    void clearHistory();

    //! Снять полный срез: CPU, память, диски, сеть, температуры, подключения
    SystemOverview sample();

    //! Последний срез без повторного чтения
    const SystemOverview &overview() const { return m_overview; }

    //! Список самых ресурсоёмких процессов (данные из /proc, без root)
    QList<ProcessEntry> topProcesses(int count = 10, bool byMemory = false);
    int processCount() const;

    // --- разбор источников (статический, используется в тестах) ---
    static QList<CpuSample> parseStat(const QString &text);
    static CpuUsage usageBetween(const QList<CpuSample> &previous, const QList<CpuSample> &current);
    static MemoryInfo parseMemInfo(const QString &text);
    static qint64 parseUptime(const QString &text);
    static QVector<double> parseLoadAverage(const QString &text);
    static QList<DiskUsage> parseMounts(const QString &text);
    static QList<NetworkInterface> parseNetDev(const QString &text);
    static ConnectionCounts parseConnections(const QString &tcp, const QString &tcp6,
                                             const QString &udp, const QString &udp6);
    static QStringList parseOsRelease(const QString &text);
    static ProcessEntry parseProcessStat(int pid, const QString &statText, const QString &statusText,
                                         qulonglong previousTicks, qint64 elapsedMs,
                                         long clockTicks);

    // --- форматирование ---
    static QString formatBytes(qulonglong bytes);
    static QString formatRate(qulonglong bytesPerSecond);
    static QString formatDuration(qint64 seconds);
    static QString formatPercent(double value);

    //! Признак файловой системы, которую нужно показывать в занятом месте
    static bool isInterestingFs(const QString &fsType);

private:
    void readTemperatures(SystemOverview &overview) const;
    void readDisks(SystemOverview &overview) const;
    void readNetworks(SystemOverview &overview);
    void readConnections(SystemOverview &overview) const;

    QString readText(const QString &path) const;

    QString m_procRoot;
    QString m_sysRoot;
    int m_historySize = 120;
    QVector<double> m_cpuHistory;
    QVector<double> m_memoryHistory;

    QList<CpuSample> m_cpuPrevious;
    QHash<QString, QPair<qulonglong, qulonglong>> m_netPrevious;
    qint64 m_lastSampleMs = 0;

    SystemOverview m_overview;

    // Процессы: тики CPU с прошлого опроса
    QHash<int, qulonglong> m_processTicks;
    qint64 m_processSampleMs = 0;
};

} // namespace BlackCore
