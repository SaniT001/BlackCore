// SPDX-License-Identifier: MIT
// BlackCore — терминал для Linux Mint
#include "core/AppInfo.h"
#include "core/CrashHandler.h"
#include "core/Logger.h"
#include "core/ConfigIO.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>

#include <QSocketNotifier>

#include <csignal>
#include <cstdio>
#include <exception>

#include <fcntl.h>
#include <unistd.h>

namespace {

int g_signalPipe[2] = {-1, -1};

void unixSignalHandler(int signalNumber)
{
    const int savedErrno = errno;
    const char byte = static_cast<char>(signalNumber);
    const ssize_t ignored = ::write(g_signalPipe[1], &byte, 1);
    (void)ignored;
    errno = savedErrno;
}

//! Корректное завершение по SIGINT/SIGTERM/SIGHUP (кнопка «Стоп» в системе)
void installSignalBridge(QCoreApplication *app)
{
    if (::pipe(g_signalPipe) != 0)
        return;

    for (const int fd : g_signalPipe) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        const int fdFlags = ::fcntl(fd, F_GETFD, 0);
        ::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC);
    }

    auto *notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, app);
    QObject::connect(notifier, &QSocketNotifier::activated, app, [app]() {
        char buffer[16];
        const ssize_t count = ::read(g_signalPipe[0], buffer, sizeof(buffer));
        if (count > 0)
            BlackCore::Log::info(QStringLiteral("Получен сигнал %1 — завершение работы")
                                     .arg(static_cast<int>(buffer[0])));
        app->quit();
    });

    struct sigaction action = {};
    action.sa_handler = &unixSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int signalNumber : {SIGINT, SIGTERM, SIGHUP}) {
        ::sigaction(signalNumber, &action, nullptr);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Графическая сессия проверяется до создания QApplication, чтобы выдать
    // понятное сообщение вместо аварийного завершения Qt.
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        std::fprintf(stderr,
                     "BlackCore: не найдена графическая сессия (DISPLAY/WAYLAND_DISPLAY не заданы).\n"
                     "Запустите приложение из сеанса Cinnamon или проверьте переменные окружения.\n");
        return 1;
    }

    // HiDPI: явный масштаб интерфейса должен быть известен до создания QApplication
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        const qreal scale = BlackCore::Settings::storedUiScale();
        if (!qFuzzyCompare(scale, 1.0))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));
    }

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QLatin1String(BlackCore::kOrgName));
    QCoreApplication::setOrganizationDomain(QLatin1String(BlackCore::kOrgDomain));
    QCoreApplication::setApplicationName(QLatin1String(BlackCore::kAppName));
    QCoreApplication::setApplicationVersion(QLatin1String(BlackCore::kAppVersion));
    QApplication::setApplicationDisplayName(QLatin1String(BlackCore::kAppName));
    QApplication::setDesktopFileName(QLatin1String(BlackCore::kAppId));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/blackcore-256.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("BlackCore — терминал для Linux Mint."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QStringLiteral("Подробный вывод отладочных сообщений."));
    QCommandLineOption commandOption(
        {QStringLiteral("e"), QStringLiteral("execute"), QStringLiteral("command")},
        QStringLiteral("Выполнить команду вместо интерактивной оболочки."),
        QStringLiteral("команда"));
    QCommandLineOption geometryOption(
        QStringLiteral("geometry"),
        QStringLiteral("Начальный размер окна, например 1280x800."),
        QStringLiteral("ширинаxвысота"));
    parser.addOption(geometryOption);

    QCommandLineOption resetOption(
        QStringLiteral("reset-settings"),
        QStringLiteral("Сбросить настройки к значениям по умолчанию (перед сбросом "
                       "создаётся резервная копия)."));
    parser.addOption(resetOption);

    QCommandLineOption shellOption(
        QStringLiteral("shell"),
        QStringLiteral("Оболочка для запуска (по умолчанию из настроек или $SHELL)."),
        QStringLiteral("путь"));

    parser.addOption(verboseOption);
    parser.addOption(commandOption);
    parser.addOption(shellOption);
    parser.process(application);

    BlackCore::Log::install(parser.isSet(verboseOption));
    BlackCore::Crash::install();
    BlackCore::Log::info(BlackCore::buildInfo());
    BlackCore::Log::info(QStringLiteral("Журнал: %1").arg(BlackCore::Log::filePath()));

    // Системная палитра фиксируется до того, как мы применим свою тему,
    // иначе «системная» тема строилась бы по нашим же цветам.
    BlackCore::ThemeManager::captureSystemPalette();

    installSignalBridge(&application);

    BlackCore::Settings &settings = BlackCore::Settings::instance();
    settings.load();

    // Сброс настроек запускается до применения темы: резервная копия создаётся
    // всегда, поэтому сброс обратим
    if (parser.isSet(resetOption)) {
        QString backupPath;
        QString resetError;
        if (BlackCore::ConfigIO::resetToDefaults(&backupPath, &resetError)) {
            BlackCore::Log::info(QStringLiteral("Настройки сброшены; резервная копия: %1")
                                     .arg(backupPath));
            settings.load();
        } else {
            BlackCore::Log::warn(QStringLiteral("Не удалось сбросить настройки: %1").arg(resetError));
        }
    }

    // Фирменный стиль: палитра + таблица стилей для всех виджетов
    const BlackCore::Theme theme = BlackCore::ThemeManager::theme(settings.themeId());
    BlackCore::ThemeManager::apply(theme);
    BlackCore::Log::info(QStringLiteral("Тема оформления: %1 (%2)").arg(theme.name, theme.id));
    if (parser.isSet(shellOption)) {
        const QString shell = parser.value(shellOption);
        if (QFileInfo::exists(shell))
            settings.setShell(shell);
        else
            BlackCore::Log::warn(QStringLiteral("Указанная оболочка не найдена: %1").arg(shell));
    }

    int exitCode = 0;
    try {
        BlackCore::MainWindow window;
        if (parser.isSet(commandOption))
            window.setInitialCommand(parser.value(commandOption));
        if (parser.isSet(geometryOption)) {
            const QStringList parts = parser.value(geometryOption).split(QLatin1Char('x'));
            if (parts.size() == 2) {
                const int width = parts.at(0).toInt();
                const int height = parts.at(1).toInt();
                if (width > 320 && height > 240)
                    window.resize(width, height);
            }
        }
        window.show();
        window.startSession();
        window.setWindowOpacity(settings.windowOpacity() / 100.0);
        if (parser.positionalArguments().contains(QStringLiteral("--fullscreen")))
            window.showFullScreen();
        exitCode = application.exec();
    } catch (const std::exception &exception) {
        BlackCore::Log::error(QStringLiteral("Неперехваченное исключение: %1")
                                  .arg(QString::fromLocal8Bit(exception.what())));
        exitCode = 2;
    } catch (...) {
        BlackCore::Log::error(QStringLiteral("Неперехваченное исключение неизвестного типа"));
        exitCode = 2;
    }

    BlackCore::Crash::markCleanExit();
    BlackCore::Log::info(QStringLiteral("BlackCore завершён с кодом %1").arg(exitCode));
    BlackCore::Log::shutdown();
    return exitCode;
}
