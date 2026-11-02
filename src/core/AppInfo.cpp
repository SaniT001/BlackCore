// SPDX-License-Identifier: MIT
#include "AppInfo.h"

#include <QDir>
#include <QStringList>
#include <vterm.h>

namespace BlackCore {

QString versionString()
{
    return QStringLiteral("v%1").arg(QLatin1String(kAppVersion));
}

QString fullName()
{
    return QStringLiteral("%1 %2").arg(QLatin1String(kAppName), versionString());
}

QString stateDirectory()
{
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty() || !QDir(base).isAbsolute())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    const QString path = base + QStringLiteral("/blackcore");
    QDir().mkpath(path);
    return path;
}

QString buildInfo()
{
    const QStringList parts{
        fullName(),
        QStringLiteral("Qt %1").arg(QLatin1String(qVersion())),
        QStringLiteral("libvterm %1.%2.%3")
            .arg(VTERM_VERSION_MAJOR)
            .arg(VTERM_VERSION_MINOR)
            .arg(VTERM_VERSION_PATCH),
#ifdef NDEBUG
        QStringLiteral("release"),
#else
        QStringLiteral("debug"),
#endif
    };
    return parts.join(QStringLiteral(" | "));
}

} // namespace BlackCore
