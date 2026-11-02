// SPDX-License-Identifier: MIT
#include "SystemMonitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QSysInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>

#include <pwd.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace BlackCore {
namespace {

constexpr double kHotCelsius = 80.0; //!< выше этого датчик помечается как «горячий»
constexpr int kMaxMounts = 24;
constexpr int kMaxTemperatures = 12;
constexpr qint64 kMinElapsedMs = 100;

QStringList splitFields(const QString &line)
{
    return line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

qulonglong toNumber(const QString &text, bool *ok = nullptr)
{
    return text.toULongLong(ok);
}

//! Разбор чисел вида «15,6» и «15.6»
QString oneDecimal(double value)
{
    QString text = QString::number(value, 'f', 1);
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return text;
}

QString userNameForUid(uint uid)
{
    struct passwd pwd;
    struct passwd *result = nullptr;
    char buffer[2048];
    if (::getpwuid_r(uid, &pwd, buffer, sizeof(buffer), &result) == 0 && result)
        return QString::fromLocal8Bit(result->pw_name);
    return QString::number(uid);
}

} // namespace

// ---------------------------------------------------------------------------
// Форматирование
// ---------------------------------------------------------------------------

QString SystemMonitor::formatBytes(qulonglong bytes)
{
    constexpr qulonglong ki = 1024ULL;
    constexpr qulonglong mi = ki * 1024ULL;
    constexpr qulonglong gi = mi * 1024ULL;
    constexpr qulonglong ti = gi * 1024ULL;

    if (bytes >= ti)
        return QStringLiteral("%1 ТиБ").arg(oneDecimal(double(bytes) / double(ti)));
    if (bytes >= gi)
        return QStringLiteral("%1 ГиБ").arg(oneDecimal(double(bytes) / double(gi)));
    if (bytes >= mi)
        return QStringLiteral("%1 МиБ").arg(oneDecimal(double(bytes) / double(mi)));
    if (bytes >= ki)
        return QStringLiteral("%1 КиБ").arg(oneDecimal(double(bytes) / double(ki)));
    return QStringLiteral("%1 Б").arg(bytes);
}

QString SystemMonitor::formatRate(qulonglong bytesPerSecond)
{
    if (bytesPerSecond == 0)
        return QStringLiteral("0 Б/с");
    return QStringLiteral("%1/с").arg(formatBytes(bytesPerSecond));
}

QString SystemMonitor::formatDuration(qint64 seconds)
{
    if (seconds < 0)
        seconds = 0;
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;

    if (days > 0)
        return QStringLiteral("%1 сут %2 ч").arg(days).arg(hours);
    if (hours > 0)
        return QStringLiteral("%1 ч %2 мин").arg(hours).arg(minutes);
    if (minutes > 0)
        return QStringLiteral("%1 мин %2 с").arg(minutes).arg(secs);
    return QStringLiteral("%1 с").arg(secs);
}

QString SystemMonitor::formatPercent(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("—");
    return QStringLiteral("%1 %").arg(oneDecimal(value));
}

bool SystemMonitor::isInterestingFs(const QString &fsType)
{
    static const QStringList types = {
        QStringLiteral("ext2"),    QStringLiteral("ext3"),    QStringLiteral("ext4"),
        QStringLiteral("btrfs"),   QStringLiteral("xfs"),     QStringLiteral("f2fs"),
        QStringLiteral("vfat"),    QStringLiteral("exfat"),   QStringLiteral("ntfs"),
        QStringLiteral("ntfs3"),   QStringLiteral("zfs"),     QStringLiteral("jfs"),
        QStringLiteral("reiserfs"), QStringLiteral("overlay"), QStringLiteral("bcachefs"),
    };
    return types.contains(fsType);
}

// ---------------------------------------------------------------------------
// Мелкие структуры
// ---------------------------------------------------------------------------

qulonglong CpuSample::total() const
{
    return user + nice + system + idle + iowait + irq + softirq + steal;
}

qulonglong CpuSample::idleTotal() const
{
    return idle + iowait;
}

QString CpuUsage::perCoreText() const
{
    if (cores.isEmpty())
        return QString();
    QStringList parts;
    for (int i = 0; i < cores.size(); ++i) {
        parts.append(QStringLiteral("ЦП%1 %2 %").arg(i).arg(oneDecimal(cores.at(i))));
    }
    return parts.join(QStringLiteral(" · "));
}

qulonglong MemoryInfo::usedKb() const
{
    if (totalKb <= availableKb)
        return 0;
    return totalKb - availableKb;
}

qulonglong MemoryInfo::swapUsedKb() const
{
    if (swapTotalKb <= swapFreeKb)
        return 0;
    return swapTotalKb - swapFreeKb;
}

double MemoryInfo::usedPercent() const
{
    if (totalKb == 0)
        return 0.0;
    return 100.0 * double(usedKb()) / double(totalKb);
}

double MemoryInfo::swapUsedPercent() const
{
    if (swapTotalKb == 0)
        return 0.0;
    return 100.0 * double(swapUsedKb()) / double(swapTotalKb);
}

double DiskUsage::usedPercent() const
{
    if (total == 0)
        return 0.0;
    return 100.0 * double(used) / double(total);
}

QString ProcessEntry::rssText() const
{
    return SystemMonitor::formatBytes(rssKb * 1024ULL);
}

QString SystemOverview::uptimeText() const
{
    return SystemMonitor::formatDuration(uptimeSeconds);
}

QString SystemOverview::loadText() const
{
    return QStringLiteral("%1 / %2 / %3")
        .arg(oneDecimal(load1), oneDecimal(load5), oneDecimal(load15));
}

qulonglong SystemOverview::networkRxRate() const
{
    qulonglong sum = 0;
    for (const NetworkInterface &iface : networks)
        sum += iface.rxRate;
    return sum;
}

qulonglong SystemOverview::networkTxRate() const
{
    qulonglong sum = 0;
    for (const NetworkInterface &iface : networks)
        sum += iface.txRate;
    return sum;
}

// ---------------------------------------------------------------------------
// Разбор источников
// ---------------------------------------------------------------------------

QList<CpuSample> SystemMonitor::parseStat(const QString &text)
{
    QList<CpuSample> result;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.startsWith(QStringLiteral("cpu")))
            break; // строки cpu* идут в начале /proc/stat
        const QStringList parts = splitFields(line);
        if (parts.size() < 5)
            continue;
        CpuSample sample;
        sample.name = parts.at(0);
        sample.user = toNumber(parts.value(1));
        sample.nice = toNumber(parts.value(2));
        sample.system = toNumber(parts.value(3));
        sample.idle = toNumber(parts.value(4));
        sample.iowait = toNumber(parts.value(5));
        sample.irq = toNumber(parts.value(6));
        sample.softirq = toNumber(parts.value(7));
        sample.steal = toNumber(parts.value(8));
        result.append(sample);
    }
    return result;
}

CpuUsage SystemMonitor::usageBetween(const QList<CpuSample> &previous, const QList<CpuSample> &current)
{
    CpuUsage usage;
    if (current.isEmpty() || previous.isEmpty())
        return usage;

    auto percent = [](const CpuSample &before, const CpuSample &after) {
        const qulonglong totalDelta = after.total() - before.total();
        const qulonglong idleDelta = after.idleTotal() - before.idleTotal();
        if (totalDelta == 0 || totalDelta > 10 * 1000 * 1000 * 1000ULL)
            return -1.0;
        const double busy = double(totalDelta) - double(idleDelta);
        return std::clamp(100.0 * busy / double(totalDelta), 0.0, 100.0);
    };

    bool any = false;
    const double totalPercent = percent(previous.first(), current.first());
    if (totalPercent >= 0.0) {
        usage.total = totalPercent;
        any = true;
    }
    for (int i = 1; i < current.size() && i < previous.size(); ++i) {
        const double value = percent(previous.at(i), current.at(i));
        if (value >= 0.0) {
            usage.cores.append(value);
            any = true;
        }
    }
    usage.valid = any;
    return usage;
}

MemoryInfo SystemMonitor::parseMemInfo(const QString &text)
{
    MemoryInfo info;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QStringList parts = splitFields(line.mid(colon + 1));
        if (parts.isEmpty())
            continue;
        bool ok = false;
        const qulonglong value = toNumber(parts.first(), &ok);
        if (!ok)
            continue;

        if (key == QLatin1String("MemTotal"))
            info.totalKb = value;
        else if (key == QLatin1String("MemAvailable"))
            info.availableKb = value;
        else if (key == QLatin1String("MemFree"))
            info.freeKb = value;
        else if (key == QLatin1String("Buffers"))
            info.buffersKb = value;
        else if (key == QLatin1String("Cached"))
            info.cachedKb = value;
        else if (key == QLatin1String("SwapTotal"))
            info.swapTotalKb = value;
        else if (key == QLatin1String("SwapFree"))
            info.swapFreeKb = value;
    }
    // MemAvailable появился в 3.14; на всякий случай оцениваем объём как в free(1)
    if (info.availableKb == 0 && info.totalKb > 0)
        info.availableKb = info.freeKb + info.buffersKb + info.cachedKb;
    info.valid = info.totalKb > 0;
    return info;
}

qint64 SystemMonitor::parseUptime(const QString &text)
{
    const QStringList parts = splitFields(text);
    if (parts.isEmpty())
        return 0;
    bool ok = false;
    const double seconds = parts.first().toDouble(&ok);
    if (!ok)
        return 0;
    return static_cast<qint64>(seconds);
}

QVector<double> SystemMonitor::parseLoadAverage(const QString &text)
{
    QVector<double> values;
    const QStringList parts = splitFields(text);
    for (int i = 0; i < 3 && i < parts.size(); ++i)
        values.append(parts.at(i).toDouble());
    while (values.size() < 3)
        values.append(0.0);
    return values;
}

QList<DiskUsage> SystemMonitor::parseMounts(const QString &text)
{
    QList<DiskUsage> result;
    QStringList seenMounts;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = splitFields(line);
        if (parts.size() < 4)
            continue;
        const QString device = parts.at(0);
        const QString mountPoint = parts.at(1);
        const QString fsType = parts.at(2);
        const QString options = parts.at(3);

        if (!isInterestingFs(fsType))
            continue;
        if (mountPoint.startsWith(QStringLiteral("/snap")))
            continue;
        // Точка монтирования уникальна: разные точки — разные строки,
        // повторное появление той же пары (bind-монтирование) не дублируем
        if (seenMounts.contains(mountPoint))
            continue;

        DiskUsage disk;
        disk.device = device;
        disk.mountPoint = mountPoint;
        disk.fsType = fsType;
        disk.readOnly = options.split(QLatin1Char(',')).contains(QStringLiteral("ro"));
        seenMounts.append(mountPoint);
        result.append(disk);
        if (result.size() >= kMaxMounts)
            break;
    }
    return result;
}

QList<NetworkInterface> SystemMonitor::parseNetDev(const QString &text)
{
    QList<NetworkInterface> result;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        if (line.trimmed().startsWith(QLatin1String("Inter-")) || line.trimmed().startsWith(QLatin1String("face")))
            continue;
        NetworkInterface iface;
        iface.name = line.left(colon).trimmed();
        const QStringList parts = splitFields(line.mid(colon + 1));
        if (parts.size() < 9)
            continue;
        iface.rxBytes = toNumber(parts.at(0));
        iface.txBytes = toNumber(parts.at(8));
        result.append(iface);
    }
    return result;
}

ConnectionCounts SystemMonitor::parseConnections(const QString &tcp, const QString &tcp6,
                                                 const QString &udp, const QString &udp6)
{
    ConnectionCounts counts;

    auto scan = [](const QString &text, ConnectionCounts &target, bool isUdp) {
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("sl")))
                continue;
            const QStringList parts = splitFields(trimmed);
            if (parts.size() < 4)
                continue;
            bool ok = false;
            const int state = parts.at(3).toInt(&ok, 16);
            if (!ok)
                continue;
            if (isUdp) {
                ++target.udp;
                continue;
            }
            switch (state) {
            case 0x01: ++target.established; break;
            case 0x0A: ++target.listen; break;
            case 0x06: ++target.timeWait; break;
            default: ++target.otherTcp; break;
            }
        }
    };

    scan(tcp, counts, false);
    scan(tcp6, counts, false);
    scan(udp, counts, true);
    scan(udp6, counts, true);
    return counts;
}

QStringList SystemMonitor::parseOsRelease(const QString &text)
{
    QString name;
    QString version;
    QString id;
    QString pretty;

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);

        if (key == QLatin1String("NAME"))
            name = value;
        else if (key == QLatin1String("VERSION_ID"))
            version = value;
        else if (key == QLatin1String("ID"))
            id = value;
        else if (key == QLatin1String("PRETTY_NAME"))
            pretty = value;
    }

    return {name, version, id, pretty};
}

ProcessEntry SystemMonitor::parseProcessStat(int pid, const QString &statText, const QString &statusText,
                                             qulonglong previousTicks, qint64 elapsedMs, long clockTicks)
{
    ProcessEntry entry;
    entry.pid = pid;

    const int open = statText.indexOf(QLatin1Char('('));
    const int close = statText.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open)
        return entry;
    entry.name = statText.mid(open + 1, close - open - 1);

    const QStringList rest = splitFields(statText.mid(close + 1));
    // rest[0] — поле 3 (state), значит поле N — это rest[N-3]
    if (rest.size() > 21) {
        const qulonglong utime = toNumber(rest.at(11));
        const qulonglong stime = toNumber(rest.at(12));
        const qulonglong rssPages = toNumber(rest.at(21));
        const long pageSize = ::sysconf(_SC_PAGESIZE);
        entry.rssKb = rssPages * qulonglong(pageSize > 0 ? pageSize : 4096) / 1024ULL;

        const qulonglong ticks = utime + stime;
        if (previousTicks > 0 && elapsedMs > 0 && clockTicks > 0 && ticks >= previousTicks) {
            const double seconds = double(elapsedMs) / 1000.0;
            entry.cpuPercent = 100.0 * double(ticks - previousTicks) / (seconds * double(clockTicks));
        }
    }

    if (!statusText.isEmpty()) {
        const QStringList lines = statusText.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("Uid:"))) {
                const QStringList parts = splitFields(line.mid(4));
                if (!parts.isEmpty())
                    entry.user = QString::number(parts.first().toUInt());
                break;
            }
        }
    }
    return entry;
}

// ---------------------------------------------------------------------------
// SystemMonitor
// ---------------------------------------------------------------------------

SystemMonitor::SystemMonitor()
    : m_procRoot(QStringLiteral("/proc"))
    , m_sysRoot(QStringLiteral("/sys"))
{
}

void SystemMonitor::setRoots(const QString &procRoot, const QString &sysRoot)
{
    m_procRoot = procRoot;
    m_sysRoot = sysRoot;
    clearHistory();
    m_cpuPrevious.clear();
    m_netPrevious.clear();
    m_processTicks.clear();
    m_processSampleMs = 0;
    m_overview = SystemOverview();
}

void SystemMonitor::setHistorySize(int points)
{
    m_historySize = std::clamp(points, 5, 1000);
    while (m_cpuHistory.size() > m_historySize)
        m_cpuHistory.removeFirst();
    while (m_memoryHistory.size() > m_historySize)
        m_memoryHistory.removeFirst();
}

void SystemMonitor::clearHistory()
{
    m_cpuHistory.clear();
    m_memoryHistory.clear();
}

QString SystemMonitor::readText(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

SystemOverview SystemMonitor::sample()
{
    SystemOverview overview;
    overview.sampledAt = QDateTime::currentDateTime();

    // --- CPU ---
    const QList<CpuSample> stat = parseStat(readText(m_procRoot + QStringLiteral("/stat")));
    overview.cpuCount = qMax(0, stat.size() - 1);
    if (!m_cpuPrevious.isEmpty()) {
        overview.cpu = usageBetween(m_cpuPrevious, stat);
    }
    if (!stat.isEmpty())
        m_cpuPrevious = stat;

    // --- память ---
    overview.memory = parseMemInfo(readText(m_procRoot + QStringLiteral("/meminfo")));

    // --- время работы и нагрузка ---
    overview.uptimeSeconds = parseUptime(readText(m_procRoot + QStringLiteral("/uptime")));
    const QVector<double> load = parseLoadAverage(readText(m_procRoot + QStringLiteral("/loadavg")));
    overview.load1 = load.value(0);
    overview.load5 = load.value(1);
    overview.load15 = load.value(2);

    // --- ядро и дистрибутив ---
    overview.kernel = readText(m_procRoot + QStringLiteral("/sys/kernel/osrelease")).trimmed();
    if (overview.kernel.isEmpty())
        overview.kernel = QSysInfo::kernelVersion();
    overview.hostName = QSysInfo::machineHostName();

    QString osRelease = readText(QStringLiteral("/etc/os-release"));
    if (osRelease.isEmpty())
        osRelease = readText(QStringLiteral("/usr/lib/os-release"));
    const QStringList distro = parseOsRelease(osRelease);
    overview.distroId = distro.value(2);
    overview.distro = distro.value(3);
    if (overview.distro.isEmpty()) {
        overview.distro = distro.value(0);
        if (overview.distro.isEmpty()) {
            overview.distro = QSysInfo::prettyProductName();
            overview.distroId = QSysInfo::productType();
        }
    }

    // --- подсистемы ---
    readDisks(overview);
    readNetworks(overview);
    readTemperatures(overview);
    readConnections(overview);

    overview.valid = true;

    // --- история ---
    if (overview.cpu.valid) {
        m_cpuHistory.append(overview.cpu.total);
        while (m_cpuHistory.size() > m_historySize)
            m_cpuHistory.removeFirst();
    }
    if (overview.memory.valid) {
        m_memoryHistory.append(overview.memory.usedPercent());
        while (m_memoryHistory.size() > m_historySize)
            m_memoryHistory.removeFirst();
    }

    m_overview = overview;
    m_lastSampleMs = QDateTime::currentMSecsSinceEpoch();
    return overview;
}

void SystemMonitor::readDisks(SystemOverview &overview) const
{
    QList<DiskUsage> disks = parseMounts(readText(m_procRoot + QStringLiteral("/mounts")));
    for (int i = disks.size() - 1; i >= 0; --i) {
        DiskUsage &disk = disks[i];
        struct statvfs info;
        const QByteArray path = QFile::encodeName(disk.mountPoint);
        if (::statvfs(path.constData(), &info) != 0) {
            disks.removeAt(i);
            continue;
        }
        const qulonglong blockSize = info.f_frsize > 0 ? info.f_frsize : info.f_bsize;
        disk.total = qulonglong(info.f_blocks) * blockSize;
        disk.available = qulonglong(info.f_bavail) * blockSize;
        disk.used = qulonglong(info.f_blocks - info.f_bfree) * blockSize;
    }
    std::sort(disks.begin(), disks.end(), [](const DiskUsage &a, const DiskUsage &b) {
        return a.mountPoint < b.mountPoint;
    });
    overview.disks = disks;
}

void SystemMonitor::readNetworks(SystemOverview &overview)
{
    QList<NetworkInterface> networks = parseNetDev(readText(m_procRoot + QStringLiteral("/net/dev")));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = m_lastSampleMs > 0 ? qMax(kMinElapsedMs, now - m_lastSampleMs) : 0;

    for (NetworkInterface &iface : networks) {
        // Интерфейсы без трафика и без состояния (lo исключением не является) тоже показываем
        iface.up = readText(m_sysRoot + QStringLiteral("/class/net/%1/operstate").arg(iface.name))
                       .trimmed()
                   == QLatin1String("up");
        const auto previous = m_netPrevious.value(iface.name);
        if (elapsed > 0 && previous.first > 0 && iface.rxBytes >= previous.first) {
            iface.rxRate = qulonglong(double(iface.rxBytes - previous.first) * 1000.0 / double(elapsed));
            iface.txRate = qulonglong(double(iface.txBytes - previous.second) * 1000.0 / double(elapsed));
        }
        m_netPrevious.insert(iface.name, {iface.rxBytes, iface.txBytes});
    }
    overview.networks = networks;
}

void SystemMonitor::readTemperatures(SystemOverview &overview) const
{
    QList<TemperatureReading> readings;
    const QDir hwmon(m_sysRoot + QStringLiteral("/class/hwmon"));
    const QStringList chips = hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &chipDir : chips) {
        const QString chipPath = hwmon.filePath(chipDir);
        const QString chipName = readText(chipPath + QStringLiteral("/name")).trimmed();
        const QDir dir(chipPath);
        const QStringList files = dir.entryList({QStringLiteral("temp*_input")}, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            bool ok = false;
            const double milli = readText(dir.filePath(file)).trimmed().toDouble(&ok);
            if (!ok)
                continue;
            TemperatureReading reading;
            reading.chip = chipName.isEmpty() ? chipDir : chipName;
            const QString base = file.left(file.size() - QStringLiteral("_input").size());
            reading.label = readText(dir.filePath(base + QStringLiteral("_label"))).trimmed();
            if (reading.label.isEmpty())
                reading.label = base;
            reading.celsius = milli / 1000.0;
            reading.source = dir.filePath(file);
            reading.critical = reading.celsius >= kHotCelsius;
            readings.append(reading);
            if (readings.size() >= kMaxTemperatures)
                break;
        }
        if (readings.size() >= kMaxTemperatures)
            break;
    }

    // Дополнительно: thermal zones (работает без hwmon, например в виртуалках)
    if (readings.isEmpty()) {
        const QDir thermal(m_sysRoot + QStringLiteral("/class/thermal"));
        const QStringList zones = thermal.entryList({QStringLiteral("thermal_zone*")}, QDir::Dirs, QDir::Name);
        for (const QString &zone : zones) {
            bool ok = false;
            const double milli = readText(thermal.filePath(zone + QStringLiteral("/temp"))).trimmed().toDouble(&ok);
            if (!ok)
                continue;
            TemperatureReading reading;
            reading.chip = readText(thermal.filePath(zone + QStringLiteral("/type"))).trimmed();
            reading.label = zone;
            reading.celsius = milli / 1000.0;
            reading.source = thermal.filePath(zone + QStringLiteral("/temp"));
            reading.critical = reading.celsius >= kHotCelsius;
            readings.append(reading);
        }
    }

    overview.temperatures = readings;
}

void SystemMonitor::readConnections(SystemOverview &overview) const
{
    const QString netRoot = m_procRoot + QStringLiteral("/net/");
    overview.connections = parseConnections(readText(netRoot + QStringLiteral("tcp")),
                                            readText(netRoot + QStringLiteral("tcp6")),
                                            readText(netRoot + QStringLiteral("udp")),
                                            readText(netRoot + QStringLiteral("udp6")));
}

int SystemMonitor::processCount() const
{
    const QDir proc(m_procRoot);
    int count = 0;
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        if (entry.at(0).isDigit())
            ++count;
    }
    return count;
}

QList<ProcessEntry> SystemMonitor::topProcesses(int count, bool byMemory)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = m_processSampleMs > 0 ? now - m_processSampleMs : 0;
    const long clockTicks = ::sysconf(_SC_CLK_TCK);
    QHash<int, qulonglong> ticksNow;

    QList<ProcessEntry> entries;
    const QDir proc(m_procRoot);
    const QStringList dirs = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dirName : dirs) {
        bool ok = false;
        const int pid = dirName.toInt(&ok);
        if (!ok)
            continue;
        const QString statPath = proc.filePath(dirName + QStringLiteral("/stat"));
        QFile statFile(statPath);
        if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString statText = QString::fromUtf8(statFile.readAll());
        statFile.close();

        const int open = statText.indexOf(QLatin1Char('('));
        const int close = statText.lastIndexOf(QLatin1Char(')'));
        if (open < 0 || close <= open)
            continue;
        const QStringList rest = splitFields(statText.mid(close + 1));
        if (rest.size() <= 21)
            continue;
        const qulonglong ticks = toNumber(rest.at(11)) + toNumber(rest.at(12));
        ticksNow.insert(pid, ticks);

        if (elapsed <= 0 && m_processTicks.isEmpty())
            continue; // первый опрос задаёт базу для процентов

        QFile statusFile(proc.filePath(dirName + QStringLiteral("/status")));
        QString statusText;
        if (statusFile.open(QIODevice::ReadOnly | QIODevice::Text))
            statusText = QString::fromUtf8(statusFile.readAll());

        ProcessEntry entry = parseProcessStat(pid, statText, statusText, m_processTicks.value(pid), elapsed, clockTicks);
        if (!statusText.isEmpty()) {
            const QStringList lines = statusText.split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (line.startsWith(QLatin1String("Uid:"))) {
                    const QStringList parts = splitFields(line.mid(4));
                    if (!parts.isEmpty())
                        entry.user = userNameForUid(parts.first().toUInt());
                    break;
                }
            }
        }
        if (entry.name.isEmpty())
            entry.name = dirName;
        entries.append(entry);
    }

    m_processTicks = ticksNow;
    m_processSampleMs = now;

    std::sort(entries.begin(), entries.end(), [byMemory](const ProcessEntry &a, const ProcessEntry &b) {
        if (byMemory)
            return a.rssKb > b.rssKb;
        if (a.cpuPercent == b.cpuPercent)
            return a.rssKb > b.rssKb;
        return a.cpuPercent > b.cpuPercent;
    });
    if (entries.size() > count)
        entries = entries.mid(0, count);
    return entries;
}

} // namespace BlackCore
