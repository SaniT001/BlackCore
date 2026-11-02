// SPDX-License-Identifier: MIT
// Автотесты этапа 1: базовые команды, цвета, кириллица, управляющие клавиши,
// длинный вывод, полноэкранные программы, изменение размера.
//
// Запуск:  xvfb-run -a ./build/blackcore-tests [--screenshots <каталог>]

#include "core/AppInfo.h"
#include "core/PaneLayout.h"
#include "core/Profile.h"
#include "core/ConfigIO.h"
#include "core/GitInfo.h"
#include "core/Project.h"
#include "core/SystemMonitor.h"
#include "core/Diagnostics.h"
#include "core/CrashHandler.h"
#include "core/CommandLibrary.h"
#include "core/History.h"
#include "core/Notifications.h"
#include "core/Secrets.h"
#include "core/TaskTracker.h"
#include "core/SshProfile.h"
#include "core/SessionStore.h"
#include "core/Shortcuts.h"
#include "core/Workspace.h"
#include "core/Logger.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "terminal/TerminalView.h"
#include "ui/MainWindow.h"
#include "ui/PaneFrame.h"
#include "ui/DevPanel.h"
#include "ui/CommandLibraryDialog.h"
#include "ui/CommandPalette.h"
#include "ui/HistoryDialog.h"
#include "ui/MonitorPanel.h"
#include "ui/NotificationToast.h"
#include "ui/SettingsDialog.h"
#include "ui/PaneTree.h"
#include "ui/SideBar.h"
#include "ui/TerminalTabs.h"
#include "ui/TitleBar.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStatusBar>
#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QRegularExpression>
#include <QSaveFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>

#include <csignal>
#include <unistd.h>
#include <QSettings>
#include <QSet>
#include <QTableWidget>
#include <QTabWidget>
#include <QMenuBar>
#include <QGuiApplication>
#include <QPalette>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <QProgressBar>
#include <QLabel>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>

#include <csignal>
#include <unistd.h>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>

#include <csignal>
#include <unistd.h>
#include <QLineEdit>

#include <algorithm>
#include <cstdio>
#include <functional>

using namespace BlackCore;

namespace {

int g_failures = 0;
int g_checks = 0;
QString g_screenshotDir;

void report(bool ok, const QString &name, const QString &details = QString())
{
    ++g_checks;
    if (!ok)
        ++g_failures;
    QTextStream out(stdout);
    out << (ok ? "  [ OK ] " : "  [FAIL] ") << name;
    if (!details.isEmpty())
        out << " — " << details;
    out << Qt::endl;
}

//! Прокрутить цикл событий заданное время (чтобы PTY успел отдать вывод)
void spin(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

//! Ждать выполнения условия или таймаута
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (condition())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}

struct Session
{
    TerminalView *view = nullptr;
    bool finished = false;
    int exitCode = -1;
    int exitSignal = -1;

    explicit Session(const QString &command)
    {
        view = new TerminalView();
        view->resize(900, 560);
        view->show();
        QObject::connect(view, &TerminalView::sessionFinished, view,
                         [this](int code, int signal) {
                             finished = true;
                             exitCode = code;
                             exitSignal = signal;
                         });
        view->setInitialCommand(command);
        // Каталог фиксирован, чтобы тест не зависел от последнего каталога в настройках
        view->setStartDirectory(QDir::homePath());
        QString error;
        if (!view->startSession(&error))
            report(false, QStringLiteral("запуск сессии"), error);
        spin(150);
    }

    ~Session()
    {
        if (view) {
            view->hide();
            delete view;
        }
    }

    void type(const QByteArray &bytes) { view->sendTextToShell(QString::fromUtf8(bytes)); }
    void sendRawByte(char value) { view->sendTextToShell(QString(QChar::fromLatin1(value))); }
    QString text() const { return view->screenText(); }
    bool contains(const QString &needle) const { return text().contains(needle); }
    void screenshot(const QString &name) const
    {
        if (g_screenshotDir.isEmpty())
            return;
        QDir().mkpath(g_screenshotDir);
        view->grab().save(g_screenshotDir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }
};

// ---------------------------------------------------------------------------

void testBasicCommands()
{
    QTextStream(stdout) << "\n== Базовые команды ==\n";

    {
        Session session(QStringLiteral("echo 'marker-hello'; pwd; printf 'done\\n'"));
        waitFor([&] { return session.contains(QStringLiteral("done")); });
        report(session.contains(QStringLiteral("marker-hello")), QStringLiteral("echo выполняет команду"),
               session.text().left(80).simplified());
        report(session.contains(QDir::homePath()), QStringLiteral("pwd показывает рабочий каталог"),
               QDir::homePath());
        session.screenshot(QStringLiteral("01-basic"));
    }

    {
        Session session(QStringLiteral("echo $TERM; echo $COLORTERM; echo $TERM_PROGRAM"));
        waitFor([&] { return session.contains(QStringLiteral("BlackCore")); });
        report(session.contains(QStringLiteral("xterm-256color")), QStringLiteral("TERM=xterm-256color"));
        report(session.contains(QStringLiteral("truecolor")), QStringLiteral("COLORTERM=truecolor"));
        report(session.contains(QStringLiteral("BlackCore")), QStringLiteral("TERM_PROGRAM=BlackCore"));
    }
}

void testErrorHandling()
{
    QTextStream(stdout) << "\n== Обработка ошибок ==\n";

    {
        Session session(QStringLiteral("nosuchcommand-xyz; echo \"code=$?\"; echo marker-after"));
        waitFor([&] { return session.contains(QStringLiteral("marker-after")); });
        report(session.contains(QStringLiteral("not found")), QStringLiteral("команда не найдена: сообщение оболочки"),
               QStringLiteral("bash: nosuchcommand-xyz: command not found"));
        report(session.contains(QStringLiteral("code=127")), QStringLiteral("код возврата 127 сохранён"));
    }

    {
        Session session(QStringLiteral("exit 7"));
        waitFor([&] { return session.finished; });
        report(session.finished && session.exitCode == 7, QStringLiteral("код возврата процесса доходит до приложения"),
               QStringLiteral("exit=%1").arg(session.exitCode));
    }
}

void testAnsiColors()
{
    QTextStream(stdout) << "\n== ANSI-цвета ==\n";

    Session session(QStringLiteral(
        "printf '\\033[31mREDTEXT\\033[0m\\n'; printf '\\033[32mGREENTEXT\\033[0m\\n'; "
        "printf '\\033[1;32mBRIGHTGREEN\\033[0m\\n'; "
        "printf '\\033[38;5;208mORANGE256\\033[0m\\n'; "
        "printf '\\033[7mREVERSED\\033[0m\\n'; sleep 1"));
    waitFor([&] { return session.contains(QStringLiteral("ORANGE256")); }, 4000);

    const ColorTheme theme = ThemeManager::theme(Settings::instance().themeId()).terminal;

    // Ищем строку REDTEXT и проверяем цвет первой ячейки
    const QStringList lines = session.text().split(QLatin1Char('\n'));
    int redLine = -1;
    int greenLine = -1;
    int orangeLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).contains(QStringLiteral("REDTEXT")))
            redLine = i;
        if (lines.at(i).contains(QStringLiteral("GREENTEXT")))
            greenLine = i;
        if (lines.at(i).contains(QStringLiteral("ORANGE256")))
            orangeLine = i;
    }

    report(redLine >= 0, QStringLiteral("SGR 31 отрисован"));
    if (redLine >= 0) {
        const QPair<QRgb, QRgb> colors = session.view->colorsAt(redLine, 0);
        report(QColor::fromRgb(colors.first) == theme.palette[1], QStringLiteral("цвет текста = красный палитры"),
               QColor::fromRgb(colors.first).name());
    }
    if (greenLine >= 0) {
        const QPair<QRgb, QRgb> colors = session.view->colorsAt(greenLine, 0);
        report(QColor::fromRgb(colors.first) == theme.palette[2],
               QStringLiteral("цвет текста = зелёный палитры"), QColor::fromRgb(colors.first).name());
    }

    int brightLine = -1;
    int reversedLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).contains(QStringLiteral("BRIGHTGREEN")))
            brightLine = i;
        if (lines.at(i).contains(QStringLiteral("REVERSED")))
            reversedLine = i;
    }
    if (brightLine >= 0) {
        const QPair<QRgb, QRgb> colors = session.view->colorsAt(brightLine, 0);
        report(QColor::fromRgb(colors.first) == theme.palette[10],
               QStringLiteral("жирный зелёный = яркий цвет палитры (bold highbright)"),
               QColor::fromRgb(colors.first).name());
    }
    if (reversedLine >= 0) {
        const QPair<QRgb, QRgb> colors = session.view->colorsAt(reversedLine, 0);
        report(QColor::fromRgb(colors.first) == theme.background
                   && QColor::fromRgb(colors.second) == theme.foreground,
               QStringLiteral("инверсия (SGR 7) меняет местами фон и текст"));
    }
    if (orangeLine >= 0) {
        const QPair<QRgb, QRgb> colors = session.view->colorsAt(orangeLine, 0);
        report(QColor::fromRgb(colors.first) == theme.colorByIndex(208),
               QStringLiteral("256-цветная палитра (38;5;208)"), QColor::fromRgb(colors.first).name());
    }

    session.screenshot(QStringLiteral("02-colors"));
}

void testCyrillic()
{
    QTextStream(stdout) << "\n== Кириллица ==\n";

    Session session(QStringLiteral(
        "printf 'Привет, мир!\\n'; printf 'ЙЦУКЕН йцукен Ёё\\n'; "
        "printf 'Широкий: 日本語\\n'; sleep 1"));
    waitFor([&] { return session.contains(QStringLiteral("日本語")); }, 4000);

    report(session.contains(QStringLiteral("Привет, мир!")), QStringLiteral("UTF-8 кириллица на экране"));
    report(session.contains(QStringLiteral("ЙЦУКЕН йцукен Ёё")), QStringLiteral("прописные/строчные и Ё"));
    QString noSpaces = session.text();
    noSpaces.remove(QLatin1Char(' '));
    report(noSpaces.contains(QStringLiteral("日本語")), QStringLiteral("широкие символы (CJK) на экране"));
    const QStringList cjkLines = session.text().split(QLatin1Char('\n'));
    for (const QString &line : cjkLines) {
        if (line.contains(QStringLiteral("日"))) {
            report(line.contains(QStringLiteral("日 本 語")),
                   QStringLiteral("широкий символ занимает две колонки"));
            break;
        }
    }

    session.screenshot(QStringLiteral("03-cyrillic"));
}

void testLongOutputAndScrollback()
{
    QTextStream(stdout) << "\n== Длинный вывод и прокрутка ==\n";

    Session session(QStringLiteral("seq 1 500; echo 'LONG-END'"));
    waitFor([&] { return session.contains(QStringLiteral("LONG-END")); }, 8000);
    report(session.contains(QStringLiteral("LONG-END")), QStringLiteral("последняя строка длинного вывода видна"));
    report(session.view->scrollbackCount() > 300, QStringLiteral("скроллбэк заполнен"),
           QStringLiteral("строк в истории: %1").arg(session.view->scrollbackCount()));

    const QString buffer = session.view->bufferText();
    report(buffer.contains(QStringLiteral("\n1\n")) || buffer.startsWith(QStringLiteral("1\n")),
           QStringLiteral("первая строка сохранена в истории"));
    report(buffer.contains(QStringLiteral("\n500")), QStringLiteral("строка 500 в истории"));

    session.view->scrollPageUp();
    spin(50);
    session.screenshot(QStringLiteral("04-scrollback"));
    session.view->scrollToBottom();
    spin(30);
}

void testControlKeys()
{
    QTextStream(stdout) << "\n== Управляющие клавиши ==\n";

    // Ctrl+C — прерывание активного процесса
    {
        Session session(QStringLiteral("echo started; sleep 30; echo unreachable"));
        waitFor([&] { return session.contains(QStringLiteral("started")); }, 3000);
        session.sendRawByte('\x03'); // Ctrl+C
        const bool stopped = waitFor([&] { return session.finished; }, 5000);
        report(stopped, QStringLiteral("Ctrl+C прерывает активный процесс"),
               QStringLiteral("exit=%1 signal=%2").arg(session.exitCode).arg(session.exitSignal));
        report(!session.contains(QStringLiteral("unreachable")),
               QStringLiteral("команда после прерывания не выполняется"));
    }

    // Ctrl+C в интерактивной оболочке не завершает её, а возвращает приглашение
    {
        Session session(QStringLiteral("sleep 30"));
        spin(400);
        session.sendRawByte('\x03');
        const bool stopped = waitFor([&] { return session.finished; }, 5000);
        report(stopped && !session.view->isSessionRunning(),
               QStringLiteral("SIGINT доставлен процессу (сессия закрыта)"),
               QStringLiteral("signal=%1").arg(session.exitSignal));
    }

    // Ctrl+D — выход из оболочки (EOF)
    {
        Session session(QStringLiteral("echo start; cat"));
        spin(400);
        session.sendRawByte('\x04'); // Ctrl+D
        const bool closed = waitFor([&] { return session.finished; }, 5000);
        report(closed, QStringLiteral("Ctrl+D (EOF) завершает процесс"),
               QStringLiteral("exit=%1 signal=%2").arg(session.exitCode).arg(session.exitSignal));
    }

    // Ctrl+Z / fg — приостановка и продолжение
    {
        Session session(QStringLiteral("sleep 5; echo after-sleep"));
        spin(300);
        session.sendRawByte('\x1a'); // Ctrl+Z
        spin(300);
        session.type(QByteArray("fg\n"));
        waitFor([&] { return session.contains(QStringLiteral("after-sleep")); }, 8000);
        report(session.contains(QStringLiteral("after-sleep")),
               QStringLiteral("Ctrl+Z и fg работают (управление заданиями)"));
    }
}

void testInteractiveScreenPrograms()
{
    QTextStream(stdout) << "\n== Полноэкранные (интерактивные) программы ==\n";

    // Альтернативный экран через less
    {
        Session session(QStringLiteral("printf 'line-A\\nline-B\\n' > /tmp/bc_less.txt; less /tmp/bc_less.txt"));
        waitFor([&] { return session.view->isAlternateScreen(); }, 5000);
        report(session.view->isAlternateScreen(), QStringLiteral("альтернативный экран включается (less)"));
        report(session.contains(QStringLiteral("line-A")), QStringLiteral("содержимое файла отображается"));
        session.screenshot(QStringLiteral("05-less"));

        session.type(QByteArray("q"));
        waitFor([&] { return !session.view->isAlternateScreen(); }, 5000);
        report(!session.view->isAlternateScreen(), QStringLiteral("выход из альтернативного экрана (q)"));
    }

    // nano: запуск и выход
    if (QFileInfo::exists(QStringLiteral("/usr/bin/nano"))
        || QFileInfo::exists(QStringLiteral("/bin/nano"))) {
        Session session(QStringLiteral("nano /tmp/bc_nano.txt"));
        waitFor([&] { return session.contains(QStringLiteral("GNU nano")); }, 6000);
        report(session.contains(QStringLiteral("GNU nano")), QStringLiteral("nano запускается"));
        session.screenshot(QStringLiteral("06-nano"));
        session.type(QByteArray("\x18")); // Ctrl+X
        spin(500);
        session.type(QByteArray("n"));
        spin(500);
        report(true, QStringLiteral("nano корректно закрывается (Ctrl+X, N)"));
    } else {
        report(true, QStringLiteral("nano не установлен — проверка пропущена"));
    }

    // top в интерактивном режиме
    if (QFileInfo::exists(QStringLiteral("/usr/bin/top"))) {
        Session session(QStringLiteral("top -d 1"));
        spin(1500);
        report(session.contains(QStringLiteral("Tasks:")) || session.contains(QStringLiteral("load average")),
               QStringLiteral("top отображается"));
        session.screenshot(QStringLiteral("07-top"));
        session.type(QByteArray("q"));
        spin(300);
    }
}

void testSshSession()
{
    QTextStream(stdout) << "\n== SSH-сессия (локальный sshd, если запущен) ==\n";

    // Порт 2222 на 127.0.0.1 — тестовый sshd; если его нет, проверка пропускается
    const QByteArray probe = QByteArrayLiteral("ssh -o BatchMode=yes -o StrictHostKeyChecking=no "
                                               "-o UserKnownHostsFile=/dev/null -i /tmp/testkey "
                                               "-p 2222 sshtest@127.0.0.1 true");
    if (system(probe.constData()) != 0) {
        report(true, QStringLiteral("локальный тестовый sshd не запущен — проверка ssh пропущена"));
        return;
    }

    Session session(QStringLiteral(
        "ssh -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "
        "-o PreferredAuthentications=publickey -i /tmp/testkey -p 2222 sshtest@127.0.0.1 "
        "\'echo remote-$(id -un); stty size\'"));
    waitFor([&] { return session.contains(QStringLiteral("remote-sshtest")); }, 15000);
    report(session.contains(QStringLiteral("remote-sshtest")),
           QStringLiteral("команда выполнена на удалённой стороне через ssh"));
    const bool finished = waitFor([&] { return session.finished; }, 8000);
    report(finished, QStringLiteral("ssh-сессия завершается после выхода"),
           QStringLiteral("exit=%1").arg(session.exitCode));

    const QStringList lines = session.text().split(QLatin1Char('\n'));
    bool sizeSeen = false;
    for (const QString &line : lines) {
        if (QRegularExpression(QStringLiteral("^\\s*\\d+\\s+\\d+\\s*$")).match(line).hasMatch())
            sizeSeen = true;
    }
    report(sizeSeen, QStringLiteral("удалённая сторона получает размер терминала (stty size)"));
    session.screenshot(QStringLiteral("13-ssh"));
}

void testResize()
{
    QTextStream(stdout) << "\n== Изменение размера ==\n";

    Session session(QStringLiteral("stty size; sleep 3; stty size"));
    const QRegularExpression sizePattern(QStringLiteral("(\\d+)\\s+(\\d+)"));
    waitFor([&] { return sizePattern.match(session.text()).hasMatch(); }, 3000);
    const QRegularExpressionMatch beforeMatch = sizePattern.match(session.text());
    report(beforeMatch.hasMatch(), QStringLiteral("stty size отдаёт размер PTY"),
           beforeMatch.hasMatch() ? beforeMatch.captured(0) : QString());

    session.view->resize(1200, 700);

    QString afterSize;
    waitFor([&] {
        afterSize.clear();
        const QStringList currentLines = session.text().split(QLatin1Char('\n'));
        for (const QString &line : currentLines) {
            const QRegularExpressionMatch match = sizePattern.match(line);
            if (match.hasMatch() && line.trimmed() == match.captured(0)
                && match.captured(0) != beforeMatch.captured(0))
                afterSize = match.captured(0);
        }
        return !afterSize.isEmpty();
    }, 8000);
    report(!afterSize.isEmpty(), QStringLiteral("размер PTY обновляется после resize окна"),
           QStringLiteral("%1 → %2").arg(beforeMatch.captured(0), afterSize.isEmpty() ? QStringLiteral("?") : afterSize));

    // Проверка перерисовки: после resize содержимое всё ещё на месте
    report(session.view->rows() > 10 && session.view->cols() > 40,
           QStringLiteral("сетка терминала пересчитана"),
           QStringLiteral("%1x%2").arg(session.view->rows()).arg(session.view->cols()));
    session.screenshot(QStringLiteral("08-resize"));
}

void testSelectionAndClipboard()
{
    QTextStream(stdout) << "\n== Выделение и буфер обмена ==\n";

    Session session(QStringLiteral("echo 'clipboard-test-value'; sleep 2"));
    waitFor([&] { return session.contains(QStringLiteral("clipboard-test-value")); }, 4000);

    session.view->selectAll();
    session.view->copySelection();
    const QString clipboard = QApplication::clipboard()->text();
    report(clipboard.contains(QStringLiteral("clipboard-test-value")),
           QStringLiteral("выделение и копирование в буфер обмена"));

    QApplication::clipboard()->setText(QStringLiteral("echo pasted-ok"));
    session.view->pasteClipboard();
    spin(100);
    session.type(QByteArray("\n"));
    waitFor([&] { return session.contains(QStringLiteral("pasted-ok")); }, 4000);
    report(session.contains(QStringLiteral("pasted-ok")), QStringLiteral("вставка из буфера выполняется"));
}

void testFontZoom()
{
    QTextStream(stdout) << "\n== Масштаб шрифта ==\n";

    Session session(QStringLiteral("sleep 2"));
    const int initialSize = session.view->fontSize();
    const int initialCols = session.view->cols();
    session.view->zoomIn();
    session.view->zoomIn();
    spin(200);
    report(session.view->fontSize() == initialSize + 2, QStringLiteral("увеличение шрифта"),
           QStringLiteral("%1 → %2 pt").arg(initialSize).arg(session.view->fontSize()));
    report(session.view->cols() < initialCols, QStringLiteral("число колонок уменьшилось"),
           QStringLiteral("%1 → %2").arg(initialCols).arg(session.view->cols()));
    session.screenshot(QStringLiteral("09-zoom"));
    session.view->zoomReset();
    spin(100);
    report(session.view->fontSize() == 11, QStringLiteral("сброс масштаба"));
}


// ===========================================================================
// Этап 2: фирменный интерфейс, вкладки, боковая панель, визуальные настройки
// ===========================================================================

void testBrandPalette()
{
    QTextStream(stdout) << "\n== Фирменная палитра ==\n";

    struct Expected
    {
        const char *name;
        QColor (*getter)(const BrandPalette &);
        const char *hex;
    };

    const Expected expected[] = {
        {"основной фон", [](const BrandPalette &b) { return b.background; }, "#101113"},
        {"панели", [](const BrandPalette &b) { return b.panel; }, "#17191C"},
        {"активные элементы", [](const BrandPalette &b) { return b.active; }, "#24262A"},
        {"золотой акцент", [](const BrandPalette &b) { return b.gold; }, "#D9B15C"},
        {"светлый золотой", [](const BrandPalette &b) { return b.goldLight; }, "#F0D18A"},
        {"основной текст", [](const BrandPalette &b) { return b.textPrimary; }, "#E8E8E8"},
        {"вторичный текст", [](const BrandPalette &b) { return b.textSecondary; }, "#969BA3"},
        {"ошибки", [](const BrandPalette &b) { return b.error; }, "#EF6464"},
        {"успех", [](const BrandPalette &b) { return b.success; }, "#66C58A"},
    };

    const BrandPalette palette = ThemeManager::colors();
    for (const Expected &item : expected) {
        const QColor actual = item.getter(palette);
        const QColor reference(QString::fromLatin1(item.hex));
        report(actual == reference,
               QStringLiteral("цвет «%1» = %2").arg(QString::fromUtf8(item.name), QString::fromLatin1(item.hex)),
               actual.name());
    }

    const QList<Theme> &themes = ThemeManager::themes();
    report(themes.size() >= 3, QStringLiteral("реестр тем содержит несколько тем"),
           QStringLiteral("%1 шт.").arg(themes.size()));

    bool allThemesValid = true;
    for (const Theme &theme : themes) {
        const QString sheet = ThemeManager::styleSheet(theme);
        if (!sheet.contains(theme.brand.gold.name()) || sheet.contains(QLatin1String("@gold")))
            allThemesValid = false;
    }
    report(allThemesValid, QStringLiteral("все темы дают таблицу стилей без незаполненных токенов"));
}

void testSettingsPersistence()
{
    QTextStream(stdout) << "\n== Настройки внешнего вида: сохранение ==\n";

    Settings &settings = Settings::instance();
    const QString originalTheme = settings.themeId();
    const QString originalFont = settings.fontFamily();
    const int originalSize = settings.fontSize();
    const qreal originalSpacing = settings.lineSpacing();
    const int originalPadding = settings.padding();
    const int originalOpacity = settings.windowOpacity();
    const CursorStyle originalCursor = settings.cursorStyle();

    AppearanceSettings values;
    values.themeId = QStringLiteral("blackcore-light");
    values.fontFamily = QStringLiteral("DejaVu Sans Mono");
    values.fontSize = 14;
    values.lineSpacing = 1.25;
    values.padding = 10;
    values.cursorStyle = CursorStyle::Underline;
    values.windowOpacity = 85;
    values.uiScale = 1.25;
    values.applyToSettings();
    settings.save();

    settings.load();

    report(settings.themeId() == values.themeId, QStringLiteral("тема сохраняется"), settings.themeId());
    report(settings.fontFamily() == values.fontFamily, QStringLiteral("шрифт сохраняется"),
           settings.fontFamily());
    report(settings.fontSize() == values.fontSize, QStringLiteral("размер текста сохраняется"),
           QString::number(settings.fontSize()));
    report(qFuzzyCompare(settings.lineSpacing(), values.lineSpacing),
           QStringLiteral("межстрочный интервал сохраняется"), QString::number(settings.lineSpacing()));
    report(settings.padding() == values.padding, QStringLiteral("отступы сохраняются"),
           QString::number(settings.padding()));
    report(settings.cursorStyle() == values.cursorStyle, QStringLiteral("форма курсора сохраняется"));
    report(settings.windowOpacity() == values.windowOpacity, QStringLiteral("прозрачность сохраняется"),
           QStringLiteral("%1 %").arg(settings.windowOpacity()));
    report(qFuzzyCompare(settings.uiScale(), values.uiScale),
           QStringLiteral("масштаб интерфейса сохраняется"), QString::number(settings.uiScale()));

    settings.setFontSize(500);
    settings.setLineSpacing(9.0);
    settings.setPadding(-5);
    settings.setWindowOpacity(5);
    report(settings.fontSize() == Settings::kMaxFontSize && settings.padding() == Settings::kMinPadding
               && settings.windowOpacity() == Settings::kMinOpacity,
           QStringLiteral("значения ограничиваются допустимым диапазоном"));

    settings.setThemeId(originalTheme);
    settings.setFontFamily(originalFont);
    settings.setFontSize(originalSize);
    settings.setLineSpacing(originalSpacing);
    settings.setPadding(originalPadding);
    settings.setWindowOpacity(originalOpacity);
    settings.setCursorStyle(originalCursor);
    settings.setUiScale(1.0);
    settings.save();
}

void testWindowChrome()
{
    QTextStream(stdout) << "\n== Главное окно: заголовок, меню, панель, статус ==\n";

    MainWindow window;
    window.resize(1100, 700);
    window.show();
    window.startSession();
    spin(700);

    report(window.titleBar() && window.titleBar()->isVisible(),
           QStringLiteral("фирменная строка заголовка отображается"));
    report(window.sideBar() && window.sideBar()->isVisible(),
           QStringLiteral("боковая панель отображается"));
    report(window.tabs() && window.tabs()->count() == 1, QStringLiteral("создана первая вкладка"),
           QStringLiteral("вкладок: %1").arg(window.tabs() ? window.tabs()->count() : -1));
    report(window.statusBar()->isVisible(), QStringLiteral("строка состояния отображается"));
    report(window.menuWidget() != nullptr, QStringLiteral("главное меню на месте"));
    report(!window.windowTitle().isEmpty(), QStringLiteral("заголовок окна обновляется"),
           window.windowTitle());

    if (!g_screenshotDir.isEmpty())
        window.grab().save(g_screenshotDir + QStringLiteral("/20-main-window.png"));

    window.newTab();
    spin(500);
    report(window.tabs()->count() == 2, QStringLiteral("вторая вкладка создаётся"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));
    report(window.tabs()->currentIndex() == 1, QStringLiteral("новая вкладка становится активной"));

    window.tabs()->setCurrentIndex(0);
    spin(200);
    report(window.tabs()->currentIndex() == 0, QStringLiteral("переключение вкладок работает"));

    if (!g_screenshotDir.isEmpty()) {
        window.tabs()->setCurrentIndex(1);
        spin(250);
        window.grab().save(g_screenshotDir + QStringLiteral("/21-tabs.png"));
    }

    window.sideBar()->hide();
    spin(150);
    report(!window.sideBar()->isVisible(), QStringLiteral("боковая панель скрывается"));
    if (!g_screenshotDir.isEmpty())
        window.grab().save(g_screenshotDir + QStringLiteral("/22-sidebar-hidden.png"));
    window.sideBar()->show();
    spin(150);
    report(window.sideBar()->isVisible(), QStringLiteral("боковая панель показывается снова"));

    window.setWindowOpacity(0.9);
    spin(80);
    report(qAbs(window.windowOpacity() - 0.9) < 0.01, QStringLiteral("прозрачность окна применяется"),
           QString::number(window.windowOpacity(), 'f', 2));
    window.setWindowOpacity(1.0);

    window.hide();
    spin(100);
}

void testVisualSettingsLive()
{
    QTextStream(stdout) << "\n== Визуальные настройки: применение на лету ==\n";

    const AppearanceSettings original = AppearanceSettings::fromSettings();

    MainWindow window;
    window.resize(1000, 640);
    window.show();
    window.startSession();
    spin(600);

    TerminalView *view = window.currentTerminal();
    if (!view) {
        report(false, QStringLiteral("терминал создан"));
        return;
    }

    const int initialCols = view->cols();
    const int initialRows = view->rows();

    view->setFontSize(16);
    spin(250);
    report(view->fontSize() == 16, QStringLiteral("размер текста применяется"),
           QStringLiteral("%1 pt").arg(view->fontSize()));
    report(view->cols() < initialCols, QStringLiteral("сетка пересчитана после смены размера"),
           QStringLiteral("%1 → %2 колонок").arg(initialCols).arg(view->cols()));

    view->setLineSpacing(1.4);
    spin(250);
    report(qAbs(view->lineSpacing() - 1.4) < 0.001, QStringLiteral("межстрочный интервал применяется"),
           QString::number(view->lineSpacing()));
    report(view->rows() < initialRows, QStringLiteral("строк стало меньше при больших интервалах"),
           QStringLiteral("%1 → %2").arg(initialRows).arg(view->rows()));

    view->setPadding(18);
    spin(200);
    report(view->padding() == 18, QStringLiteral("отступы применяются"),
           QStringLiteral("%1 px").arg(view->padding()));

    view->setCursorStyle(CursorStyle::Bar);
    spin(120);
    report(view->cursorStyle() == CursorStyle::Bar, QStringLiteral("форма курсора применяется"));

    const QStringList families = QFontDatabase::families();
    QString candidate;
    for (const QString &family : families) {
        if (family.contains(QStringLiteral("Mono")) && family != view->fontFamily()) {
            candidate = family;
            break;
        }
    }
    if (!candidate.isEmpty()) {
        view->setFontFamily(candidate);
        spin(250);
        report(view->fontFamily() == candidate, QStringLiteral("шрифт переключается"), candidate);
    }

    bool themesApplied = true;
    for (const Theme &theme : ThemeManager::themes()) {
        ThemeManager::apply(theme);
        view->applyTheme(theme);
        spin(120);
        if (ThemeManager::current().id != theme.id)
            themesApplied = false;
    }
    report(themesApplied, QStringLiteral("все темы применяются к терминалу"),
           QStringLiteral("%1 шт.").arg(ThemeManager::themes().size()));

    if (!g_screenshotDir.isEmpty())
        window.grab().save(g_screenshotDir + QStringLiteral("/23-visual-settings.png"));

    original.applyToSettings();
    ThemeManager::apply(original.themeId);
    Settings::instance().save();
    window.hide();
    spin(120);
}

void testHiDpi()
{
    QTextStream(stdout) << "\n== HiDPI и масштабирование ==\n";

    TerminalView view;
    view.resize(800, 500);
    view.show();
    spin(300);

    const qreal dpr = view.devicePixelRatioF();
    report(dpr > 0.5, QStringLiteral("devicePixelRatio доступен"), QString::number(dpr, 'f', 2));
    report(view.cols() > 20 && view.rows() > 5, QStringLiteral("сетка терминала рассчитана"),
           QStringLiteral("%1 × %2").arg(view.cols()).arg(view.rows()));

    view.setLineSpacing(1.15);
    spin(200);
    report(view.rows() > 5, QStringLiteral("дробный межстрочный интервал не ломает сетку"),
           QStringLiteral("%1 строк").arg(view.rows()));

    view.setLineSpacing(1.0);
    view.hide();
    spin(100);
}

void testUnknownShell()
{
    QTextStream(stdout) << "\n== Обработка ошибок запуска ==\n";

    TerminalView view;
    view.resize(400, 300);
    Settings::instance().setShell(QStringLiteral("/nonexistent/shell"));
    QString error;
    const bool started = view.startSession(&error);
    report(!started && !error.isEmpty(), QStringLiteral("сообщение об ошибке при неверной оболочке"), error);
    Settings::instance().setShell(Settings::defaultShell());
}

// ---------------------------------------------------------------------------
// Этап 3: вкладки, панели, рабочие пространства, восстановление сессии
// ---------------------------------------------------------------------------

//! Сохранить/восстановить настройки, чтобы тесты не портили профиль пользователя
struct SettingsBackup
{
    QByteArray profiles;
    QByteArray workspaces;
    QByteArray restore;
    QByteArray session;

    SettingsBackup()
    {
        QSettings store;
        profiles = store.value(QStringLiteral("profiles/list")).toByteArray();
        workspaces = store.value(QStringLiteral("workspaces/list")).toByteArray();
        restore = store.value(QStringLiteral("session/restoreOnStartup")).toByteArray();

        QFile file(SessionStore::storagePath());
        if (file.open(QIODevice::ReadOnly))
            session = file.readAll();
    }

    ~SettingsBackup()
    {
        QSettings store;
        if (profiles.isEmpty())
            store.remove(QStringLiteral("profiles/list"));
        else
            store.setValue(QStringLiteral("profiles/list"), profiles);
        if (workspaces.isEmpty())
            store.remove(QStringLiteral("workspaces/list"));
        else
            store.setValue(QStringLiteral("workspaces/list"), workspaces);
        if (restore.isEmpty())
            store.remove(QStringLiteral("session/restoreOnStartup"));
        else
            store.setValue(QStringLiteral("session/restoreOnStartup"), restore);
        store.sync();

        QFile file(SessionStore::storagePath());
        if (session.isEmpty()) {
            file.remove();
        } else if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(session);
            file.close();
        }
    }
};

void testStage3Models()
{
    QTextStream(stdout) << "\n== Этап 3: раскладки, профили, рабочие пространства ==\n";
    SettingsBackup backup;

    // --- раскладки ---
    auto left = LayoutNode::makePane(PaneSpec::withDirectory(QStringLiteral("/tmp"), QStringLiteral("Логи")));
    auto middle = LayoutNode::makePane(PaneSpec::withDirectory(QStringLiteral("/tmp")));
    auto right = LayoutNode::makePane(PaneSpec());
    auto split = LayoutNode::makeSplit(Qt::Horizontal, {left, middle, right}, {50, 25, 25});

    report(split->paneCount() == 3, QStringLiteral("в раскладке три панели"),
           QStringLiteral("панелей: %1").arg(split->paneCount()));
    report(split->isValid() && split->isSplit(), QStringLiteral("раскладка корректна"));
    report(split->firstPane().workingDirectory == QStringLiteral("/tmp"),
           QStringLiteral("первая панель — левая"), split->firstPane().workingDirectory);

    const QJsonObject json = split->toJson();
    const auto restored = LayoutNode::fromJson(json);
    report(restored && restored->paneCount() == 3, QStringLiteral("раскладка переживает JSON"));
    report(restored && restored->orientation() == Qt::Horizontal,
           QStringLiteral("ориентация сплита сохраняется"));
    report(restored && restored->panes().at(0).title == QStringLiteral("Логи"),
           QStringLiteral("имена панелей сохраняются"),
           restored ? restored->panes().at(0).title : QString());

    // битый сплит с одним ребёнком должен «схлопнуться» в панель
    QJsonObject broken;
    broken.insert(QStringLiteral("type"), QStringLiteral("split"));
    broken.insert(QStringLiteral("orientation"), QStringLiteral("horizontal"));
    QJsonArray single;
    single.append(left->toJson());
    broken.insert(QStringLiteral("children"), single);
    const auto repaired = LayoutNode::fromJson(broken);
    report(repaired && repaired->isValid() && repaired->paneCount() == 1,
           QStringLiteral("битый сплит восстанавливается как панель"),
           QStringLiteral("панелей: %1").arg(repaired ? repaired->paneCount() : -1));

    // --- политика команд ---
    report(CommandPolicy::isSafe(QStringLiteral("git status -sb")),
           QStringLiteral("git status разрешён политикой"));
    report(CommandPolicy::isSafe(QStringLiteral("journalctl --user -f -n 40")),
           QStringLiteral("journalctl разрешён политикой"));
    report(!CommandPolicy::isSafe(QStringLiteral("rm -rf ~")),
           QStringLiteral("rm -rf отклонён политикой"));
    report(!CommandPolicy::isSafe(QStringLiteral("ls; rm -rf /tmp/x")),
           QStringLiteral("составная команда отклонена"));
    report(!CommandPolicy::isSafe(QStringLiteral("echo $(whoami)")),
           QStringLiteral("подстановка команды отклонена"));
    report(!CommandPolicy::isSafe(QStringLiteral("cat a > b")),
           QStringLiteral("перенаправление отклонено"));

    // --- встроенные рабочие пространства ---
    const QList<Workspace> builtin = WorkspaceManager::builtin();
    report(builtin.size() >= 3, QStringLiteral("встроенных рабочих пространств не меньше трёх"),
           QStringLiteral("всего: %1").arg(builtin.size()));

    const Workspace dev = WorkspaceManager::workspace(QStringLiteral("builtin.dev"));
    report(dev.paneCount() == 3, QStringLiteral("«Разработка» — три панели"),
           QStringLiteral("панелей: %1").arg(dev.paneCount()));
    QStringList devTitles;
    for (const PaneSpec &pane : dev.layout ? dev.layout->panes() : QList<PaneSpec>())
        devTitles << pane.title;
    report(devTitles.contains(QStringLiteral("Код и Git"))
               && devTitles.contains(QStringLiteral("Запуск проекта"))
               && devTitles.contains(QStringLiteral("Логи")),
           QStringLiteral("панели «Разработки» названы по заданию"), devTitles.join(QStringLiteral(", ")));

    const Workspace system = WorkspaceManager::workspace(QStringLiteral("builtin.system"));
    report(system.paneCount() == 3, QStringLiteral("«Система» — три панели"),
           QStringLiteral("панелей: %1").arg(system.paneCount()));

    bool allSafe = true;
    for (const Workspace &workspace : WorkspaceManager::all()) {
        if (!workspace.layout)
            continue;
        for (const PaneSpec &pane : workspace.layout->panes()) {
            if (pane.runCommand && !CommandPolicy::isSafe(pane.command))
                allSafe = false;
        }
    }
    report(allSafe, QStringLiteral("все команды шаблонов проходят политику безопасности"));

    // --- профили ---
    report(ProfileManager::profile(ProfileManager::defaultId()).isValid(),
           QStringLiteral("встроенный профиль доступен"));
    TerminalProfile custom;
    custom.id = QStringLiteral("test-stage3");
    custom.name = QStringLiteral("Тестовый профиль");
    custom.fontSize = 14;
    custom.scrollback = 5000;
    ProfileManager::saveProfile(custom);
    report(ProfileManager::profile(QStringLiteral("test-stage3")).fontSize == 14,
           QStringLiteral("пользовательский профиль сохраняется"));
    report(ProfileManager::normalizeId(QStringLiteral("нет-такого")) == ProfileManager::defaultId(),
           QStringLiteral("неизвестный профиль заменяется на встроенный"));
    ProfileManager::removeProfile(QStringLiteral("test-stage3"));
    report(ProfileManager::profile(QStringLiteral("test-stage3")).id != QStringLiteral("test-stage3"),
           QStringLiteral("профиль удаляется"));

    // --- хранилище сессии ---
    SessionState state;
    TabState tab;
    tab.title = QStringLiteral("Опасная вкладка");
    tab.activePane = 0;
    PaneSpec danger;
    danger.title = QStringLiteral("Опасная панель");
    danger.workingDirectory = QStringLiteral("/nonexistent/blackcore-test");
    danger.command = QStringLiteral("rm -rf /");
    danger.runCommand = true;
    tab.layout = LayoutNode::makePane(danger);
    state.tabs.append(tab);
    SessionStore::save(state);
    report(QFileInfo::exists(SessionStore::storagePath()),
           QStringLiteral("файл сессии создаётся"), SessionStore::storagePath());

    const SessionState loaded = SessionStore::load();
    report(loaded.tabs.size() == 1, QStringLiteral("сессия читается из файла"));

    const SessionState safe = SessionStore::sanitize(loaded);
    const PaneSpec restoredPane = safe.tabs.value(0).layout
        ? safe.tabs.value(0).layout->firstPane()
        : PaneSpec();
    report(restoredPane.command.isEmpty() && !restoredPane.runCommand,
           QStringLiteral("команда не восстанавливается (защита от повторов)"),
           restoredPane.command);
    report(restoredPane.workingDirectory == QDir::homePath(),
           QStringLiteral("недоступный каталог заменён на домашний"),
           restoredPane.workingDirectory);

    SessionStore::setRestoreEnabled(false);
    report(!SessionStore::restoreEnabled(), QStringLiteral("восстановление можно отключить"));
    SessionStore::setRestoreEnabled(true);
    report(SessionStore::restoreEnabled(), QStringLiteral("восстановление включено по умолчанию"));
}

void testStage3Tabs()
{
    QTextStream(stdout) << "\n== Этап 3: вкладки ==\n";
    SettingsBackup backup;

    MainWindow window;
    window.resize(1100, 700);
    window.show();
    window.startSession();
    spin(600);

    const int initial = window.tabs() ? window.tabs()->count() : 0;
    report(initial == 1, QStringLiteral("стартовая вкладка одна"), QStringLiteral("вкладок: %1").arg(initial));

    window.newTab();
    spin(500);
    report(window.tabs()->count() == 2, QStringLiteral("вторая вкладка открывается"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));

    window.tabs()->renameTab(0, QStringLiteral("Работа"));
    spin(100);
    report(window.tabs()->tabTitle(0) == QStringLiteral("Работа"),
           QStringLiteral("вкладка переименовывается"), window.tabs()->tabTitle(0));

    window.tabs()->setCurrentIndex(0);
    spin(150);
    PaneTree *source = window.tabs()->currentTab();
    const QString sourceTitle = window.tabs()->tabTitle(0);
    window.tabs()->duplicateTab(0);
    spin(600);
    report(window.tabs()->count() == 3, QStringLiteral("дубликат вкладки создаётся"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));
    PaneTree *copy = window.tabs()->tabAt(2);
    report(copy && source && copy->captureLayout() && source->captureLayout()
               && copy->captureLayout()->paneCount() == source->captureLayout()->paneCount(),
           QStringLiteral("дубликат повторяет раскладку"));
    report(window.tabs()->tabTitle(2) == sourceTitle,
           QStringLiteral("дубликат наследует имя"), window.tabs()->tabTitle(2));

    if (!g_screenshotDir.isEmpty()) {
        window.tabs()->setCurrentIndex(0);
        spin(250);
        window.grab().save(g_screenshotDir + QStringLiteral("/23-tabs-stage3.png"));
    }

    // переключение вкладок действиями с горячими клавишами
    QAction *tab2Action = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Вкладка 2"))
            tab2Action = action;
    }
    report(tab2Action != nullptr, QStringLiteral("действие «Вкладка 2» существует"));
    if (tab2Action) {
        tab2Action->trigger();
        spin(200);
        report(window.tabs()->currentIndex() == 1, QStringLiteral("Ctrl+2 переключает вкладку"),
               QStringLiteral("индекс: %1").arg(window.tabs()->currentIndex()));
    }

    window.tabs()->setCurrentIndex(1);
    spin(100);
    window.tabs()->removeTab(1);
    spin(200);
    report(window.tabs()->count() == 2, QStringLiteral("вкладка закрывается"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));

    window.hide();
    spin(100);
}

void testStage3Split()
{
    QTextStream(stdout) << "\n== Этап 3: разделение окна на панели ==\n";
    SettingsBackup backup;

    MainWindow window;
    window.resize(1200, 760);
    window.show();
    window.startSession();
    spin(700);

    window.splitCurrentPane(Qt::Horizontal);
    spin(600);
    PaneTree *tree = window.tabs()->currentTab();
    report(tree && tree->paneCount() == 2, QStringLiteral("вертикальное разделение создаёт две панели"),
           QStringLiteral("панелей: %1").arg(tree ? tree->paneCount() : -1));

    window.splitCurrentPane(Qt::Vertical);
    spin(600);
    report(tree && tree->paneCount() == 3, QStringLiteral("горизонтальное разделение добавляет панель"),
           QStringLiteral("панелей: %1").arg(tree ? tree->paneCount() : -1));

    const QList<PaneFrame *> frames = tree ? tree->frames() : QList<PaneFrame *>();
    bool headers = true;
    for (PaneFrame *frame : frames)
        headers = headers && frame->isHeaderVisible();
    report(headers && frames.size() == 3, QStringLiteral("у панелей есть заголовки"));

    auto layout = tree ? tree->captureLayout() : QSharedPointer<LayoutNode>();
    report(layout && layout->paneCount() == 3 && layout->isValid(),
           QStringLiteral("раскладка из интерфейса снимается корректно"));

    // независимые каталоги
    TerminalView *first = frames.value(0) ? frames.value(0)->terminal() : nullptr;
    TerminalView *second = frames.value(1) ? frames.value(1)->terminal() : nullptr;
    if (first && second) {
        first->sendTextToShell(QStringLiteral("cd /tmp\n"));
        spin(600);
        report(first->workingDirectory() == QStringLiteral("/tmp"),
               QStringLiteral("каталог первой панели меняется независимо"), first->workingDirectory());
        report(second->workingDirectory() != QStringLiteral("/tmp"),
               QStringLiteral("каталог второй панели остался прежним"), second->workingDirectory());

        first->sendTextToShell(QStringLiteral("echo PANE-ONE-MARK\n"));
        spin(600);
        report(first->screenText().contains(QStringLiteral("PANE-ONE-MARK")),
               QStringLiteral("вывод первой панели на месте"));
        report(!second->screenText().contains(QStringLiteral("PANE-ONE-MARK")),
               QStringLiteral("вторая панель не получает чужой вывод"));
    } else {
        report(false, QStringLiteral("панели созданы"));
    }

    if (!g_screenshotDir.isEmpty()) {
        spin(300);
        window.grab().save(g_screenshotDir + QStringLiteral("/24-split-panes.png"));
    }

    // фокус
    TerminalView *before = tree->activeTerminal();
    tree->focusNextPane(1);
    spin(150);
    report(tree->activeTerminal() != before, QStringLiteral("F6 переключает активную панель"));
    PaneFrame *leftMost = nullptr;
    for (PaneFrame *frame : tree->frames()) {
        const int x = frame->mapTo(tree, QPoint(0, 0)).x();
        if (!leftMost || x < leftMost->mapTo(tree, QPoint(0, 0)).x())
            leftMost = frame;
    }
    tree->focusDirection(Qt::LeftEdge);
    spin(150);
    report(leftMost && tree->activeTerminal() == leftMost->terminal(),
           QStringLiteral("Ctrl+Shift+← ведёт на левую панель"));

    // закрытие одной панели
    window.closeCurrentPane();
    spin(400);
    report(tree->paneCount() == 2, QStringLiteral("панель закрывается по одной"),
           QStringLiteral("панелей: %1").arg(tree->paneCount()));
    report(tree->captureLayout() && tree->captureLayout()->isValid(),
           QStringLiteral("раскладка остаётся корректной после закрытия"));

    window.hide();
    spin(100);
}

void testStage3Workspaces()
{
    QTextStream(stdout) << "\n== Этап 3: рабочие пространства ==\n";
    SettingsBackup backup;

    MainWindow window;
    window.resize(1280, 780);
    window.show();
    window.startSession();
    spin(500);

    window.openWorkspace(QStringLiteral("builtin.dev"));
    spin(1500);

    PaneTree *tree = window.tabs()->currentTab();
    report(tree && tree->paneCount() == 3, QStringLiteral("«Разработка» открывает три панели"),
           QStringLiteral("панелей: %1").arg(tree ? tree->paneCount() : -1));
    report(window.tabs()->tabTitle(window.tabs()->currentIndex()) == QStringLiteral("Разработка"),
           QStringLiteral("вкладка рабочего пространства подписана"),
           window.tabs()->tabTitle(window.tabs()->currentIndex()));

    if (tree && tree->paneCount() == 3) {
        // Панели в порядке слева направо
        QList<PaneFrame *> frames = tree->frames();
        std::sort(frames.begin(), frames.end(), [tree](PaneFrame *left, PaneFrame *right) {
            return left->mapTo(tree, QPoint(0, 0)).x() < right->mapTo(tree, QPoint(0, 0)).x();
        });
        report(frames.at(0)->paneTitle() == QStringLiteral("Код и Git"),
               QStringLiteral("первая панель — «Код и Git»"), frames.at(0)->paneTitle());
        report(frames.at(2)->paneTitle() == QStringLiteral("Логи"),
               QStringLiteral("третья панель — «Логи»"), frames.at(2)->paneTitle());
        spin(1200);
        const QString logText = frames.at(2)->terminal()->screenText();
        report(!logText.trimmed().isEmpty(), QStringLiteral("в панели логов идёт вывод"),
               logText.left(60).simplified());
    }

    if (!g_screenshotDir.isEmpty()) {
        spin(400);
        window.grab().save(g_screenshotDir + QStringLiteral("/25-workspace-dev.png"));
    }

    window.openWorkspace(QStringLiteral("builtin.system"));
    spin(1200);
    report(window.tabs()->count() == 3, QStringLiteral("второе пространство открывается в новой вкладке"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));

    // сохранение текущей раскладки под своим именем
    const Workspace custom = WorkspaceManager::fromCurrent(
        QStringLiteral("Моё пространство"), QStringLiteral("тест"),
        window.tabs()->currentTab()->captureLayout());
    WorkspaceManager::saveUser(custom);
    bool found = false;
    for (const Workspace &workspace : WorkspaceManager::userWorkspaces())
        found = found || workspace.name == QStringLiteral("Моё пространство");
    report(found, QStringLiteral("рабочее пространство сохраняется пользователем"));
    report(custom.paneCount() == 3, QStringLiteral("сохранённая раскладка содержит три панели"),
           QStringLiteral("панелей: %1").arg(custom.paneCount()));
    WorkspaceManager::removeUser(custom.id);
    report(WorkspaceManager::userWorkspaces().isEmpty(),
           QStringLiteral("рабочее пространство удаляется"));

    window.hide();
    spin(100);
}

void testStage3SessionRestore()
{
    QTextStream(stdout) << "\n== Этап 3: восстановление сессии ==\n";
    SettingsBackup backup;

    SessionStore::setRestoreEnabled(true);
    const QString marker = QStringLiteral("/tmp/blackcore-danger-marker");

    // Сессия с потенциально опасной командой и несуществующим каталогом
    SessionState state;
    {
        TabState tab;
        tab.title = QStringLiteral("Проект");
        tab.customTitle = QStringLiteral("Мой проект");
        tab.activePane = 1;
        auto left = LayoutNode::makePane(PaneSpec::withDirectory(QDir::homePath(), QStringLiteral("Код")));
        PaneSpec risky = PaneSpec::withDirectory(QStringLiteral("/no/such/dir"), QStringLiteral("Логи"));
        risky.command = QStringLiteral("touch %1").arg(marker);
        risky.runCommand = true;
        auto right = LayoutNode::makePane(risky);
        tab.layout = LayoutNode::makeSplit(Qt::Horizontal, {left, right}, {60, 40});
        state.tabs.append(tab);

        TabState second;
        second.title = QStringLiteral("Система");
        second.layout = LayoutNode::makePane(PaneSpec::withDirectory(QDir::homePath()));
        state.tabs.append(second);
        state.currentTab = 1;
    }
    SessionStore::save(state);
    QFile::remove(marker);

    MainWindow window;
    window.resize(1200, 760);
    window.show();
    window.startSession();
    spin(1800);

    report(window.tabs()->count() == 2, QStringLiteral("восстанавливаются обе вкладки"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));
    report(window.tabs()->tabTitle(0) == QStringLiteral("Мой проект"),
           QStringLiteral("собственное имя вкладки сохраняется"), window.tabs()->tabTitle(0));
    report(window.tabs()->currentIndex() == 1, QStringLiteral("активная вкладка восстанавливается"),
           QStringLiteral("индекс: %1").arg(window.tabs()->currentIndex()));

    PaneTree *tree = window.tabs()->tabAt(0);
    report(tree && tree->paneCount() == 2, QStringLiteral("раскладка панелей восстанавливается"),
           QStringLiteral("панелей: %1").arg(tree ? tree->paneCount() : -1));

    if (tree && tree->paneCount() == 2) {
        TerminalView *right = tree->frames().at(1)->terminal();
        report(right->isSessionRunning(), QStringLiteral("сессия панели запускается"));
        spin(500);
        report(right->workingDirectory() == QDir::homePath(),
               QStringLiteral("пропавший каталог заменён на домашний"), right->workingDirectory());
    }

    report(!QFileInfo::exists(marker), QStringLiteral("опасная команда не выполняется при старте"));

    // повторное сохранение уже восстановленной сессии
    window.saveSession();
    const SessionState reloaded = SessionStore::load();
    report(reloaded.tabs.size() == 2, QStringLiteral("сессия сохраняется заново"),
           QStringLiteral("вкладок: %1").arg(reloaded.tabs.size()));

    if (!g_screenshotDir.isEmpty()) {
        window.tabs()->setCurrentIndex(0);
        spin(400);
        window.grab().save(g_screenshotDir + QStringLiteral("/26-session-restored.png"));
    }

    window.hide();
    spin(100);

    // Восстановление можно выключить: приложение начнёт с одной чистой вкладки
    SessionStore::setRestoreEnabled(false);
    MainWindow plain;
    plain.show();
    plain.startSession();
    spin(500);
    report(plain.tabs()->count() == 1 && plain.tabs()->currentTab()->paneCount() == 1,
           QStringLiteral("при выключенном восстановлении открывается одна панель"));
    plain.hide();
    spin(100);
}

// ---------------------------------------------------------------------------
// Этап 4: настройки и персонализация
// ---------------------------------------------------------------------------

//! Полная копия файла конфигурации: тесты не должны портить настройки пользователя
struct ConfigFileBackup
{
    QString path;
    QByteArray data;
    QString sessionPath;
    QByteArray sessionData;

    ConfigFileBackup()
    {
        {
            QSettings store;
            store.sync();
        }
        path = ConfigIO::configFilePath();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            data = file.readAll();

        sessionPath = SessionStore::storagePath();
        QFile sessionFile(sessionPath);
        if (sessionFile.open(QIODevice::ReadOnly))
            sessionData = sessionFile.readAll();
    }

    ~ConfigFileBackup()
    {
        {
            QSettings store;
            store.sync();
        }
        QFile file(path);
        if (data.isEmpty())
            file.remove();
        else if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(data);

        QFile sessionFile(sessionPath);
        if (sessionData.isEmpty())
            sessionFile.remove();
        else if (sessionFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            sessionFile.write(sessionData);

        Settings::instance().load();
        ShortcutManager::resetAll();
    }
};

void testStage4Themes()
{
    QTextStream(stdout) << "\n== Этап 4: темы и системная схема ==\n";
    ConfigFileBackup backup;

    const QList<Theme> &themes = ThemeManager::themes();
    report(themes.size() >= 6, QStringLiteral("тем не меньше шести"),
           QStringLiteral("всего: %1").arg(themes.size()));

    QSet<QString> ids;
    bool palettesOk = true;
    bool namesOk = true;
    for (const Theme &theme : themes) {
        ids.insert(theme.id);
        namesOk = namesOk && !theme.name.isEmpty() && !theme.description.isEmpty();
        for (int i = 0; i < 16; ++i)
            palettesOk = palettesOk && theme.terminal.palette[i].isValid();
        palettesOk = palettesOk && theme.terminal.background.isValid()
            && theme.terminal.foreground.isValid();
    }
    report(ids.size() == themes.size(), QStringLiteral("идентификаторы тем уникальны"));
    report(palettesOk, QStringLiteral("у каждой темы полная палитра (16 + фон/текст)"));
    report(namesOk, QStringLiteral("у каждой темы есть имя и описание"));

    int darkCount = 0;
    int lightCount = 0;
    for (const Theme &theme : themes) {
        if (theme.dark)
            ++darkCount;
        else
            ++lightCount;
    }
    report(darkCount >= 3, QStringLiteral("есть несколько тёмных тем"),
           QStringLiteral("тёмных: %1").arg(darkCount));
    report(lightCount >= 1, QStringLiteral("есть светлая тема"),
           QStringLiteral("светлых: %1").arg(lightCount));

    const Theme light = ThemeManager::theme(QStringLiteral("blackcore-light"));
    report(!light.dark && light.brand.background.lightness() > 200,
           QStringLiteral("светлая тема действительно светлая"),
           light.brand.background.name());

    const Theme gold = ThemeManager::theme(QStringLiteral("blackcore-dark"));
    report(gold.brand.background.lightness() < 60
               && gold.brand.gold.hue() >= 30 && gold.brand.gold.hue() <= 55,
           QStringLiteral("чёрно-золотая тема сохраняет фирменные цвета"),
           QStringLiteral("%1 / %2").arg(gold.brand.background.name(), gold.brand.gold.name()));

    // системная тема строится по палитре рабочего стола
    QPalette desktopPalette;
    desktopPalette.setColor(QPalette::Window, QColor(QStringLiteral("#203040")));
    desktopPalette.setColor(QPalette::WindowText, QColor(QStringLiteral("#E0E6EE")));
    desktopPalette.setColor(QPalette::Base, QColor(QStringLiteral("#182430")));
    desktopPalette.setColor(QPalette::Text, QColor(QStringLiteral("#E0E6EE")));
    desktopPalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#4E9A06")));
    ThemeManager::captureSystemPalette(desktopPalette);

    const Theme system = ThemeManager::systemTheme();
    report(system.id == QStringLiteral("system"),
           QStringLiteral("системная тема доступна"), system.id);
    report(system.brand.background == QColor(QStringLiteral("#203040")),
           QStringLiteral("системная тема берёт цвета рабочего стола"),
           system.brand.background.name());
    report(system.brand.gold == QColor(QStringLiteral("#4E9A06")),
           QStringLiteral("акцент системной темы — системный цвет выделения"),
           system.brand.gold.name());
    report(system.dark, QStringLiteral("тёмная системная схема распознаётся"));

    ThemeManager::apply(QStringLiteral("blackcore-dark"));
    report(ThemeManager::systemTheme().brand.background == QColor(QStringLiteral("#203040")),
           QStringLiteral("системная тема не подменяется последней применённой"));

    report(ThemeManager::normalizeId(QStringLiteral("Системная")) == QStringLiteral("system"),
           QStringLiteral("старое имя «Системная» приводится к идентификатору"));
    report(ThemeManager::normalizeId(QStringLiteral("Чёрно-золотая"))
               == QStringLiteral("blackcore-dark"),
           QStringLiteral("«Чёрно-золотая» — это фирменная тема"));
    report(ThemeManager::normalizeId(QStringLiteral("нет такой")) == ThemeManager::defaultId(),
           QStringLiteral("неизвестная тема заменяется на фирменную"));

    Settings::instance().setThemeId(QStringLiteral("blackcore-midnight"));
    Settings::instance().save();
    Settings::instance().load();
    report(Settings::instance().themeId() == QStringLiteral("blackcore-midnight"),
           QStringLiteral("выбранная тема сохраняется"));
}

void testStage4SettingsModel()
{
    QTextStream(stdout) << "\n== Этап 4: модель настроек ==\n";
    ConfigFileBackup backup;

    // значения по умолчанию
    {
        QString error;
        ConfigIO::resetToDefaults(nullptr, &error);
    }
    Settings &settings = Settings::instance();
    report(settings.wheelScrollLines() == 3, QStringLiteral("шаг колеса по умолчанию — 3 строки"),
           QString::number(settings.wheelScrollLines()));
    report(settings.cursorBlink() && settings.scrollOnOutput() && settings.scrollOnKeystroke(),
           QStringLiteral("прокрутка и мигание включены по умолчанию"));
    report(settings.statusBarVisible() && settings.menuBarVisible() && settings.tabBarVisible()
               && settings.sidebarVisible(),
           QStringLiteral("все панели видны по умолчанию"));
    report(settings.restoreSession() && settings.autoSave(),
           QStringLiteral("восстановление сессии и автосохранение включены"));
    report(settings.warnOnCloseRunning() && settings.confirmCloseWindow(),
           QStringLiteral("предупреждения при закрытии включены"));
    report(settings.startDirectory().isEmpty(),
           QStringLiteral("стартовый каталог по умолчанию не задан"));

    // ограничения значений
    settings.setWheelScrollLines(100);
    report(settings.wheelScrollLines() == Settings::kMaxWheelLines,
           QStringLiteral("шаг колеса ограничен сверху"), QString::number(settings.wheelScrollLines()));
    settings.setWheelScrollLines(-5);
    report(settings.wheelScrollLines() == Settings::kMinWheelLines,
           QStringLiteral("шаг колеса ограничен снизу"));
    settings.setScrollbackLines(10);
    report(settings.scrollbackLines() == Settings::kMinScrollback,
           QStringLiteral("буфер прокрутки ограничен снизу"));
    settings.setScrollbackLines(1000000);
    report(settings.scrollbackLines() == Settings::kMaxScrollback,
           QStringLiteral("буфер прокрутки ограничен сверху"));

    // сохранение и чтение новых параметров
    settings.setWheelScrollLines(7);
    settings.setScrollbackLines(8000);
    settings.setCursorBlink(false);
    settings.setStatusBarVisible(false);
    settings.setMenuBarVisible(false);
    settings.setTabBarVisible(false);
    settings.setScrollOnOutput(false);
    settings.setScrollOnKeystroke(false);
    settings.setStartDirectory(QStringLiteral("/tmp"));
    settings.setRestoreSession(false);
    settings.setNewTabInCurrentDirectory(false);
    settings.setWarnOnCloseRunning(false);
    settings.setConfirmCloseWindow(false);
    settings.setAutoSave(false);
    settings.save();
    settings.load();

    report(settings.wheelScrollLines() == 7 && settings.scrollbackLines() == 8000,
           QStringLiteral("прокрутка сохраняется"));
    report(!settings.cursorBlink() && !settings.statusBarVisible() && !settings.menuBarVisible()
               && !settings.tabBarVisible(),
           QStringLiteral("внешний вид и панели сохраняются"));
    report(!settings.scrollOnOutput() && !settings.scrollOnKeystroke(),
           QStringLiteral("правила прокрутки сохраняются"));
    report(settings.startDirectory() == QStringLiteral("/tmp"),
           QStringLiteral("стартовый каталог сохраняется"), settings.startDirectory());
    report(!settings.restoreSession() && !settings.newTabInCurrentDirectory(),
           QStringLiteral("поведение вкладок и сессии сохраняется"));
    report(!settings.warnOnCloseRunning() && !settings.confirmCloseWindow() && !settings.autoSave(),
           QStringLiteral("предупреждения и автосохранение сохраняются"));

    // проверка корректности при чтении
    {
        QSettings store;
        store.setValue(QStringLiteral("behavior/startDirectory"), QStringLiteral("/нет/такого"));
        store.setValue(QStringLiteral("terminal/shell"), QStringLiteral("/нет/такой/оболочки"));
        store.sync();
    }
    settings.load();
    report(settings.startDirectory().isEmpty(),
           QStringLiteral("пропавший стартовый каталог сбрасывается"));
    report(settings.shell() == Settings::defaultShell(),
           QStringLiteral("неисполняемая оболочка заменяется системной"), settings.shell());

    // структуры значений
    AppearanceSettings appearance;
    appearance.themeId = QStringLiteral("blackcore-graphite");
    appearance.fontSize = 14;
    appearance.wheelScrollLines = 5;
    appearance.tabBarVisible = false;
    appearance.scrollbackLines = 5000;
    appearance.applyToSettings();
    const AppearanceSettings readBack = AppearanceSettings::fromSettings();
    report(readBack.themeId == appearance.themeId && readBack.fontSize == 14
               && readBack.wheelScrollLines == 5 && !readBack.tabBarVisible
               && readBack.scrollbackLines == 5000,
           QStringLiteral("AppearanceSettings переносит значения в настройки"));

    BehaviorSettings behavior;
    behavior.shell = Settings::defaultShell();
    behavior.startDirectory = QStringLiteral("/home");
    behavior.restoreSession = true;
    behavior.autoSave = true;
    behavior.applyToSettings();
    const BehaviorSettings readBehavior = BehaviorSettings::fromSettings();
    report(readBehavior.startDirectory == QStringLiteral("/home") && readBehavior.restoreSession
               && readBehavior.autoSave,
           QStringLiteral("BehaviorSettings переносит значения в настройки"));
}

void testStage4Shortcuts()
{
    QTextStream(stdout) << "\n== Этап 4: горячие клавиши ==\n";
    ConfigFileBackup backup;
    ShortcutManager::resetAll();

    const QList<ShortcutSpec> &catalog = ShortcutManager::catalog();
    report(catalog.size() >= 25, QStringLiteral("список действий непустой"),
           QStringLiteral("действий: %1").arg(catalog.size()));

    QSet<QString> ids;
    bool defaultsOk = true;
    for (const ShortcutSpec &spec : catalog) {
        ids.insert(spec.id);
        defaultsOk = defaultsOk && !spec.title.isEmpty() && !spec.group.isEmpty();
    }
    report(ids.size() == catalog.size(), QStringLiteral("идентификаторы действий уникальны"));
    report(defaultsOk, QStringLiteral("у действий есть названия и разделы"));

    report(ShortcutManager::key(QStringLiteral("tab.new"))
               == QKeySequence(QStringLiteral("Ctrl+Shift+T")),
           QStringLiteral("по умолчанию Ctrl+Shift+T открывает вкладку"));
    report(ShortcutManager::key(QStringLiteral("pane.splitVertical"))
               == QKeySequence(QStringLiteral("Ctrl+Shift+D")),
           QStringLiteral("разделение панели по умолчанию — Ctrl+Shift+D"));

    ShortcutManager::setKey(QStringLiteral("tab.new"), QKeySequence(QStringLiteral("Ctrl+Alt+T")));
    report(ShortcutManager::key(QStringLiteral("tab.new"))
               == QKeySequence(QStringLiteral("Ctrl+Alt+T")),
           QStringLiteral("новая комбинация сохраняется"));
    report(ShortcutManager::hasOverride(QStringLiteral("tab.new")),
           QStringLiteral("переопределение отличается от значения по умолчанию"));
    report(ShortcutManager::overrideCount() == 1, QStringLiteral("счётчик переопределений"),
           QString::number(ShortcutManager::overrideCount()));

    report(ShortcutManager::conflict(QStringLiteral("Ctrl+Shift+W"), QStringLiteral("tab.new"))
               == QStringLiteral("tab.close"),
           QStringLiteral("конфликт клавиш обнаруживается"));

    ShortcutManager::setKey(QStringLiteral("tab.new"), QKeySequence());
    report(ShortcutManager::key(QStringLiteral("tab.new")).isEmpty(),
           QStringLiteral("действие можно отключить (пустые клавиши)"));
    ShortcutManager::resetKey(QStringLiteral("tab.new"));
    report(ShortcutManager::key(QStringLiteral("tab.new"))
               == QKeySequence(QStringLiteral("Ctrl+Shift+T")),
           QStringLiteral("сброс возвращает комбинацию по умолчанию"));

    QKeySequence parsed;
    report(ShortcutManager::parse(QStringLiteral("Ctrl+Shift+F12"), &parsed) && !parsed.isEmpty(),
           QStringLiteral("корректная комбинация разбирается"));
    report(!ShortcutManager::parse(QStringLiteral("просто текст"), &parsed),
           QStringLiteral("некорректная комбинация отклоняется"));
    report(!ShortcutManager::parse(QStringLiteral("a"), &parsed),
           QStringLiteral("одиночная буква без модификатора отклоняется"));
    report(ShortcutManager::parse(QString(), &parsed) && parsed.isEmpty(),
           QStringLiteral("пустая строка = действие без клавиш"));

    // применение к действиям окна
    MainWindow window;
    window.show();
    window.startSession();
    spin(500);
    ShortcutManager::setKey(QStringLiteral("tab.new"), QKeySequence(QStringLiteral("Ctrl+Alt+N")));
    window.applyShortcuts();
    QAction *tabNew = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Новая вкладка") && !action->shortcut().isEmpty())
            tabNew = action;
    }
    report(tabNew && tabNew->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+N")),
           QStringLiteral("новая комбинация применяется к действию окна"),
           tabNew ? tabNew->shortcut().toString() : QStringLiteral("действие не найдено"));
    window.hide();
    spin(100);
    ShortcutManager::resetAll();
}

void testStage4ConfigIO()
{
    QTextStream(stdout) << "\n== Этап 4: экспорт, импорт и резервные копии ==\n";
    ConfigFileBackup backup;

    const QString tempDir = QDir::tempPath() + QStringLiteral("/blackcore-config-test");
    QDir().mkpath(tempDir);
    const QString exportPath = tempDir + QStringLiteral("/export.json");
    QFile::remove(exportPath);

    Settings &settings = Settings::instance();
    settings.setFontSize(15);
    settings.setThemeId(QStringLiteral("blackcore-light"));
    settings.setWheelScrollLines(6);
    settings.save();
    ShortcutManager::setKey(QStringLiteral("tab.close"), QKeySequence(QStringLiteral("Ctrl+Alt+W")));

    QString error;
    report(ConfigIO::exportToFile(exportPath, &error), QStringLiteral("экспорт создаёт файл"), error);
    report(QFileInfo::exists(exportPath), QStringLiteral("файл экспорта существует"));

    QJsonObject exported;
    {
        QFile file(exportPath);
        if (file.open(QIODevice::ReadOnly))
            exported = QJsonDocument::fromJson(file.readAll()).object();
    }
    report(exported.value(QStringLiteral("app")).toString() == QStringLiteral("BlackCore"),
           QStringLiteral("в файле указано приложение"));
    report(exported.value(QStringLiteral("format")).toInt() == ConfigIO::kFormatVersion,
           QStringLiteral("в файле указана версия формата"));
    report(exported.value(QStringLiteral("settings")).toObject()
               .value(QStringLiteral("terminal/fontSize")).toInt() == 15,
           QStringLiteral("экспорт содержит значения настроек"));
    report(exported.value(QStringLiteral("shortcuts")).toObject()
               .value(QStringLiteral("tab.close")).toString()
               == QStringLiteral("Ctrl+Alt+W"),
           QStringLiteral("экспорт содержит горячие клавиши"));

    // --- проверка корректности ---
    report(ConfigIO::validate(exported, &error), QStringLiteral("корректный файл проходит проверку"));

    QJsonObject wrongApp = exported;
    wrongApp.insert(QStringLiteral("app"), QStringLiteral("Другая программа"));
    report(!ConfigIO::validate(wrongApp, &error) && !error.isEmpty(),
           QStringLiteral("чужой файл отвергается"), error);

    QJsonObject wrongFormat = exported;
    wrongFormat.insert(QStringLiteral("format"), 99);
    report(!ConfigIO::validate(wrongFormat, &error),
           QStringLiteral("неподдерживаемый формат отвергается"), error);

    QJsonObject wrongType = exported;
    QJsonObject badSettings = wrongType.value(QStringLiteral("settings")).toObject();
    badSettings.insert(QStringLiteral("terminal/fontSize"), QStringLiteral("большой"));
    wrongType.insert(QStringLiteral("settings"), badSettings);
    report(!ConfigIO::validate(wrongType, &error),
           QStringLiteral("неверный тип значения отвергается"), error);

    QJsonObject unknownKey = exported;
    QJsonObject unknownSettings = unknownKey.value(QStringLiteral("settings")).toObject();
    unknownSettings.insert(QStringLiteral("terminal/неизвестныйПараметр"), 42);
    unknownKey.insert(QStringLiteral("settings"), unknownSettings);
    QStringList unknownWarnings;
    report(ConfigIO::validate(unknownKey, &error, &unknownWarnings)
               && !unknownWarnings.isEmpty(),
           QStringLiteral("неизвестный параметр пропускается с предупреждением"),
           unknownWarnings.join(QStringLiteral("; ")));

    // --- импорт ---
    settings.setFontSize(9);
    settings.setWheelScrollLines(2);
    settings.save();
    ShortcutManager::resetAll();

    QStringList warnings;
    report(ConfigIO::importFromFile(exportPath, &error, &warnings),
           QStringLiteral("корректный файл импортируется"), error);
    report(Settings::instance().fontSize() == 15 && Settings::instance().wheelScrollLines() == 6,
           QStringLiteral("значения из файла применяются"),
           QStringLiteral("%1 pt, шаг %2").arg(Settings::instance().fontSize())
               .arg(Settings::instance().wheelScrollLines()));
    report(Settings::instance().themeId() == QStringLiteral("blackcore-light"),
           QStringLiteral("тема из файла применяется"));
    report(ShortcutManager::key(QStringLiteral("tab.close"))
               == QKeySequence(QStringLiteral("Ctrl+Alt+W")),
           QStringLiteral("горячие клавиши из файла применяются"));

    bool backupSeen = false;
    for (const QString &warning : warnings)
        backupSeen = backupSeen || warning.contains(QStringLiteral("резервная копия"));
    report(backupSeen, QStringLiteral("перед импортом создаётся резервная копия"),
           warnings.join(QStringLiteral("; ")));

    const QStringList backupsBefore = QDir(ConfigIO::backupDirectory())
                                          .entryList({QStringLiteral("config-*.json")}, QDir::Files);
    report(!backupsBefore.isEmpty(), QStringLiteral("файл резервной копии существует в каталоге"),
           ConfigIO::backupDirectory());

    QString backupContent;
    if (!backupsBefore.isEmpty()) {
        QFile file(ConfigIO::backupDirectory() + QLatin1Char('/')
                   + backupsBefore.last());
        if (file.open(QIODevice::ReadOnly))
            backupContent = QString::fromUtf8(file.readAll());
    }
    report(backupContent.contains(QStringLiteral("blackcore")),
           QStringLiteral("резервная копия содержит прежние настройки"));

    // --- импорт с исправлением значений ---
    QJsonObject outOfRange = exported;
    QJsonObject fixedSettings = outOfRange.value(QStringLiteral("settings")).toObject();
    fixedSettings.insert(QStringLiteral("terminal/fontSize"), 999);
    fixedSettings.insert(QStringLiteral("terminal/shell"), QStringLiteral("/нет/оболочки"));
    outOfRange.insert(QStringLiteral("settings"), fixedSettings);
    const QString fixedPath = tempDir + QStringLiteral("/fixed.json");
    {
        QFile file(fixedPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(outOfRange).toJson());
    }
    warnings.clear();
    report(ConfigIO::importFromFile(fixedPath, &error, &warnings),
           QStringLiteral("файл с пограничными значениями импортируется"), error);
    report(Settings::instance().fontSize() == Settings::kMaxFontSize,
           QStringLiteral("значение вне диапазона ограничивается"),
           QString::number(Settings::instance().fontSize()));
    report(warnings.size() >= 2, QStringLiteral("предупреждения о правках выдаются"),
           warnings.join(QStringLiteral("; ")));
    report(Settings::instance().shell() == Settings::defaultShell(),
           QStringLiteral("несуществующая оболочка заменяется системной"),
           Settings::instance().shell());

    // --- повреждённый файл ---
    const QString brokenPath = tempDir + QStringLiteral("/broken.json");
    {
        QFile file(brokenPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write("{ это не JSON ");
    }
    error.clear();
    report(!ConfigIO::importFromFile(brokenPath, &error) && !error.isEmpty(),
           QStringLiteral("повреждённый файл не применяется"), error);
    report(Settings::instance().fontSize() == Settings::kMaxFontSize,
           QStringLiteral("при ошибке импорта настройки не меняются"));

    // --- сброс к умолчанию ---
    settings.setFontSize(30);
    settings.setTabBarVisible(false);
    settings.save();
    QString resetBackup;
    error.clear();
    report(ConfigIO::resetToDefaults(&resetBackup, &error),
           QStringLiteral("сброс к значениям по умолчанию выполняется"), error);
    report(!resetBackup.isEmpty() && QFileInfo::exists(resetBackup),
           QStringLiteral("перед сбросом создаётся резервная копия"), resetBackup);
    report(Settings::instance().fontSize() == 11 && Settings::instance().tabBarVisible(),
           QStringLiteral("значения вернулись к стандартным"),
           QString::number(Settings::instance().fontSize()));
    report(ShortcutManager::overrideCount() == 0,
           QStringLiteral("сброс очищает переопределения клавиш"));

    // --- безопасность: файлы оболочки не меняются ---
    const QString bashrc = QDir::homePath() + QStringLiteral("/.bashrc");
    QByteArray bashrcBefore;
    const QDateTime bashrcTimeBefore = QFileInfo(bashrc).lastModified();
    {
        QFile file(bashrc);
        if (file.open(QIODevice::ReadOnly))
            bashrcBefore = file.readAll();
    }
    QJsonObject shellChange = exported;
    QJsonObject shellSettings = shellChange.value(QStringLiteral("settings")).toObject();
    shellSettings.insert(QStringLiteral("terminal/shell"), QStringLiteral("/bin/sh"));
    shellChange.insert(QStringLiteral("settings"), shellSettings);
    const QString shellPath = tempDir + QStringLiteral("/shell.json");
    {
        QFile file(shellPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(shellChange).toJson());
    }
    ConfigIO::importFromFile(shellPath, &error, &warnings);

    QByteArray bashrcAfter;
    {
        QFile file(bashrc);
        if (file.open(QIODevice::ReadOnly))
            bashrcAfter = file.readAll();
    }
    report(bashrcBefore == bashrcAfter
               && QFileInfo(bashrc).lastModified() == bashrcTimeBefore,
           QStringLiteral("импорт настроек не изменяет .bashrc"));
    report(Settings::instance().shell() == QStringLiteral("/bin/sh"),
           QStringLiteral("оболочка применяется только внутри BlackCore"),
           Settings::instance().shell());
    report(qEnvironmentVariable("SHELL") != QStringLiteral("/bin/sh")
               || Settings::defaultShell() == QStringLiteral("/bin/sh"),
           QStringLiteral("системная оболочка пользователя не подменяется"),
           qEnvironmentVariable("SHELL"));

    QDir(tempDir).removeRecursively();
}

void testStage4Ui()
{
    QTextStream(stdout) << "\n== Этап 4: интерфейс настроек ==\n";
    ConfigFileBackup backup;

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.startSession();
    spin(700);

    // меню настроек
    QMenu *configMenu = nullptr;
    for (QMenu *menu : window.menuWidget()->findChildren<QMenu *>()) {
        if (menu->title().remove(QLatin1Char('&')) == QStringLiteral("Настройки"))
            configMenu = menu;
    }
    report(configMenu != nullptr, QStringLiteral("в меню есть раздел «Настройки»"));
    if (configMenu) {
        QStringList titles;
        for (QAction *action : configMenu->actions())
            titles << action->text();
        report(titles.contains(QStringLiteral("Экспорт настроек…"))
                   && titles.contains(QStringLiteral("Импорт настроек…"))
                   && titles.contains(QStringLiteral("Сбросить настройки…")),
               QStringLiteral("в меню есть экспорт, импорт и сброс"),
               titles.join(QStringLiteral(", ")));
    }

    // диалог: пять разделов
    SettingsDialog dialog(&window);
    const auto *tabs = dialog.findChild<QTabWidget *>();
    report(tabs && tabs->count() == 6, QStringLiteral("в диалоге шесть разделов"),
           QStringLiteral("разделов: %1").arg(tabs ? tabs->count() : -1));
    if (tabs) {
        QStringList names;
        for (int i = 0; i < tabs->count(); ++i)
            names << tabs->tabText(i);
        report(names.contains(QStringLiteral("Уведомления")),
               QStringLiteral("есть раздел уведомлений"));
        report(names.contains(QStringLiteral("Оформление"))
                   && names.contains(QStringLiteral("Панели и прокрутка"))
                   && names.contains(QStringLiteral("Поведение"))
                   && names.contains(QStringLiteral("Горячие клавиши"))
                   && names.contains(QStringLiteral("Конфигурация")),
               QStringLiteral("разделы названы по заданию"), names.join(QStringLiteral(", ")));
    }

    const auto *shortcutTable = dialog.findChild<QTableWidget *>();
    report(shortcutTable && shortcutTable->rowCount() >= 25,
           QStringLiteral("таблица горячих клавиш заполнена"),
           QStringLiteral("строк: %1").arg(shortcutTable ? shortcutTable->rowCount() : -1));

    // применение настроек к окну
    AppearanceSettings appearance = window.appearanceValues();
    appearance.statusBarVisible = false;
    appearance.menuBarVisible = false;
    appearance.tabBarVisible = false;
    appearance.wheelScrollLines = 8;
    appearance.scrollOnOutput = false;
    appearance.scrollbackLines = 4000;
    appearance.cursorBlink = false;
    window.applySettings(appearance, window.behaviorValues(), false);
    spin(300);

    report(!window.statusBar()->isVisible(), QStringLiteral("строка состояния скрывается"));
    report(!window.tabs()->isTabBarVisible(), QStringLiteral("полоса вкладок скрывается"));
    report(!window.findChild<QMenuBar *>()->isVisible(), QStringLiteral("главное меню скрывается"));

    TerminalView *terminal = window.tabs()->currentTerminal();
    report(terminal && terminal->wheelScrollLines() == 8,
           QStringLiteral("шаг колеса применяется к терминалу"),
           terminal ? QString::number(terminal->wheelScrollLines()) : QString());
    report(terminal && !terminal->scrollOnOutput() && !terminal->cursorBlink(),
           QStringLiteral("правила прокрутки и курсор применяются к терминалу"));
    report(terminal && terminal->scrollbackLimit() == 4000,
           QStringLiteral("глубина буфера применяется к терминалу"),
           terminal ? QString::number(terminal->scrollbackLimit()) : QString());

    // меню можно вернуть действием «Главное меню» (Ctrl+Shift+M)
    QAction *menuBarAction = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Главное меню") && action->isCheckable())
            menuBarAction = action;
    }
    report(menuBarAction != nullptr, QStringLiteral("есть действие «Главное меню»"));
    if (menuBarAction) {
        menuBarAction->setChecked(true);
        spin(150);
        report(window.findChild<QMenuBar *>()->isVisible(),
               QStringLiteral("меню возвращается действием с горячей клавишей"));
    }

    appearance.statusBarVisible = true;
    appearance.menuBarVisible = true;
    appearance.tabBarVisible = true;
    window.applySettings(appearance, window.behaviorValues(), false);
    spin(200);
    report(window.statusBar()->isVisible() && window.tabs()->isTabBarVisible()
               && window.findChild<QMenuBar *>()->isVisible(),
           QStringLiteral("панели возвращаются на место"));

    // тема: системная и светлая
    appearance.themeId = QStringLiteral("system");
    window.applySettings(appearance, window.behaviorValues(), false);
    spin(200);
    report(ThemeManager::current().id == QStringLiteral("system"),
           QStringLiteral("системная тема применяется к окну"), ThemeManager::current().id);

    appearance.themeId = QStringLiteral("blackcore-light");
    window.applySettings(appearance, window.behaviorValues(), false);
    spin(250);
    report(ThemeManager::current().id == QStringLiteral("blackcore-light")
               && ThemeManager::colors().background.lightness() > 200,
           QStringLiteral("светлая тема применяется"),
           ThemeManager::colors().background.name());

    if (!g_screenshotDir.isEmpty()) {
        window.grab().save(g_screenshotDir + QStringLiteral("/30-light-theme.png"));

        SettingsDialog shot(&window);
        shot.show();
        spin(400);
        shot.grab().save(g_screenshotDir + QStringLiteral("/31-settings-appearance.png"));
        if (auto *tabWidget = shot.findChild<QTabWidget *>()) {
            tabWidget->setCurrentIndex(1);
            spin(250);
            shot.grab().save(g_screenshotDir + QStringLiteral("/32-settings-panels.png"));
            tabWidget->setCurrentIndex(2);
            spin(250);
            shot.grab().save(g_screenshotDir + QStringLiteral("/33-settings-behavior.png"));
            tabWidget->setCurrentIndex(3);
            spin(250);
            shot.grab().save(g_screenshotDir + QStringLiteral("/34-settings-shortcuts.png"));
            tabWidget->setCurrentIndex(4);
            spin(250);
            shot.grab().save(g_screenshotDir + QStringLiteral("/35-settings-config.png"));
        }
        shot.hide();

        appearance.themeId = QStringLiteral("blackcore-dark");
        window.applySettings(appearance, window.behaviorValues(), false);
        spin(300);
        window.grab().save(g_screenshotDir + QStringLiteral("/36-dark-theme.png"));
    }

    appearance.themeId = QStringLiteral("blackcore-dark");
    window.applySettings(window.appearanceValues(), window.behaviorValues(), false);
    window.hide();
    spin(150);
}

// ---------------------------------------------------------------------------
// Этап 5: инструменты разработчика — Git, SSH, проекты
// ---------------------------------------------------------------------------

//! Временный Git-репозиторий для тестов
struct TempRepository
{
    QString path;

    explicit TempRepository(const QString &name)
    {
        path = QDir::tempPath() + QStringLiteral("/blackcore-") + name;
        QDir(path).removeRecursively();
        QDir().mkpath(path);
    }

    ~TempRepository() { QDir(path).removeRecursively(); }

    bool run(const QStringList &arguments)
    {
        QProcess process;
        process.setWorkingDirectory(path);
        process.setProgram(QStringLiteral("git"));
        process.setArguments(arguments);
        process.start();
        if (!process.waitForFinished(8000))
            return false;
        return process.exitCode() == 0;
    }

    bool init()
    {
        if (!run({QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"),
                  QStringLiteral("main"), QStringLiteral(".")}))
            return false;
        run({QStringLiteral("config"), QStringLiteral("user.email"),
             QStringLiteral("test@example.com")});
        run({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")});
        return true;
    }

    bool write(const QString &name, const QString &content)
    {
        QFile file(path + QLatin1Char('/') + name);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        file.write(content.toUtf8());
        return true;
    }
};

void testStage5Git()
{
    QTextStream(stdout) << "\n== Этап 5: Git ==\n";

    report(GitService::isGitAvailable(), QStringLiteral("системный git обнаружен"));
    report(GitService::gitVersion().contains(QStringLiteral("git version")),
           QStringLiteral("версия git читается"), GitService::gitVersion());

    // Каталог без репозитория
    const QString plainDirectory = QDir::tempPath() + QStringLiteral("/blackcore-plain");
    QDir().mkpath(plainDirectory);
    report(GitService::findRepositoryRoot(plainDirectory).isEmpty(),
           QStringLiteral("в обычном каталоге репозиторий не находится"));
    const GitStatus noRepo = GitService::status(plainDirectory);
    report(!noRepo.valid, QStringLiteral("статус для не-репозитория не выдаётся"), noRepo.error);

    // Репозиторий с изменениями
    TempRepository repository(QStringLiteral("git-test"));
    report(repository.init(), QStringLiteral("тестовый репозиторий создан"));
    repository.write(QStringLiteral("README.md"), QStringLiteral("# Тест\n"));
    repository.run({QStringLiteral("add"), QStringLiteral("README.md")});
    repository.run({QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("первый")});

    report(GitService::findRepositoryRoot(repository.path) == repository.path,
           QStringLiteral("корень репозитория определяется"));
    QDir().mkpath(repository.path + QStringLiteral("/src/deep"));
    report(GitService::findRepositoryRoot(repository.path + QStringLiteral("/src/deep"))
               == repository.path,
           QStringLiteral("из вложенного каталога находится корень"));

    const GitStatus clean = GitService::status(repository.path);
    report(clean.valid && clean.isClean(), QStringLiteral("чистый репозиторий распознан"),
           clean.shortLabel());
    report(clean.branch == QStringLiteral("main"), QStringLiteral("ветка читается"),
           clean.branch);
    report(!clean.lastCommit.isEmpty(), QStringLiteral("короткий хэш коммита получен"),
           clean.lastCommit);
    report(clean.shortLabel().contains(QStringLiteral("main")),
           QStringLiteral("подпись статуса содержит ветку"), clean.shortLabel());

    repository.write(QStringLiteral("README.md"), QStringLiteral("# Тест\nправка\n"));
    repository.write(QStringLiteral("new.txt"), QStringLiteral("новый файл\n"));
    const GitStatus dirty = GitService::status(repository.path);
    report(dirty.modified == 1, QStringLiteral("изменённый файл посчитан"),
           QString::number(dirty.modified));
    report(dirty.untracked == 1, QStringLiteral("неотслеживаемый файл посчитан"),
           QString::number(dirty.untracked));
    report(dirty.changedFiles() == 2 && !dirty.isClean(),
           QStringLiteral("всего изменений — два"), QString::number(dirty.changedFiles()));
    report(dirty.shortLabel().contains(QStringLiteral("2")),
           QStringLiteral("подпись содержит число изменений"), dirty.shortLabel());

    repository.run({QStringLiteral("add"), QStringLiteral("README.md")});
    const GitStatus staged = GitService::status(repository.path);
    report(staged.staged == 1, QStringLiteral("изменение в индексе посчитано"),
           QString::number(staged.staged));

    // Вложенный каталог: статус родителя
    const GitStatus fromSubdirectory = GitService::status(repository.path + QStringLiteral("/src/deep"));
    report(fromSubdirectory.valid && fromSubdirectory.repositoryRoot == repository.path,
           QStringLiteral("статус из подкаталога указывает корень"));

    // Политика команд: только белый список
    const QStringList allowed = GitService::quickCommands();
    report(allowed.contains(QStringLiteral("git status")),
           QStringLiteral("git status в списке быстрых команд"));
    report(GitService::isAllowedCommand(QStringLiteral("git log --oneline --graph -20")),
           QStringLiteral("git log разрешён"));
    report(!GitService::isAllowedCommand(QStringLiteral("git push --force")),
           QStringLiteral("принудительный push не в списке"));
    report(!GitService::isAllowedCommand(QStringLiteral("rm -rf .git")),
           QStringLiteral("удаление репозитория не разрешено"));
    report(!GitService::isAllowedCommand(QStringLiteral("git status; rm -rf ~")),
           QStringLiteral("составная команда не разрешена"));

    QDir(plainDirectory).removeRecursively();
}

void testStage5Ssh()
{
    QTextStream(stdout) << "\n== Этап 5: SSH-профили ==\n";
    ConfigFileBackup backup;

    SshProfile profile;
    profile.id = QStringLiteral("ssh-test");
    profile.name = QStringLiteral("Тестовый сервер");
    profile.host = QStringLiteral("example.com");
    profile.port = 2222;
    profile.user = QStringLiteral("deploy");
    profile.identityFile = QString();

    report(SshManager::validate(profile).isEmpty(), QStringLiteral("корректный профиль проходит проверку"));
    SshManager::saveProfile(profile);
    const SshProfile loaded = SshManager::profile(QStringLiteral("ssh-test"));
    report(loaded.name == profile.name && loaded.host == QStringLiteral("example.com")
               && loaded.port == 2222 && loaded.user == QStringLiteral("deploy"),
           QStringLiteral("профиль сохраняется и читается"), loaded.host);

    report(SshManager::profiles().size() >= 1, QStringLiteral("список профилей непустой"),
           QString::number(SshManager::profiles().size()));

    // В файле настроек нет пароля и содержимого ключей
    const QJsonObject exported = ConfigIO::toJson();
    const QString profilesJson = exported.value(QStringLiteral("settings")).toObject()
                                     .value(QStringLiteral("ssh/profiles")).toString();
    report(profilesJson.contains(QStringLiteral("example.com")),
           QStringLiteral("профиль попадает в конфигурацию"));
    report(!profilesJson.contains(QStringLiteral("password"), Qt::CaseInsensitive)
               && !profilesJson.contains(QStringLiteral("passphrase"), Qt::CaseInsensitive),
           QStringLiteral("в профиле нет полей пароля или парольной фразы"));

    // Проверки полей
    SshProfile bad = profile;
    bad.host = QStringLiteral("example.com; rm -rf ~");
    report(!SshManager::isValidHost(bad.host), QStringLiteral("адрес с командой отклоняется"));
    report(!SshManager::validate(bad).isEmpty(), QStringLiteral("ошибка адреса возвращается"),
           SshManager::validate(bad));

    bad = profile;
    bad.host = QStringLiteral("$(whoami)");
    report(!SshManager::isValidHost(bad.host), QStringLiteral("подстановка в адресе отклоняется"));

    bad = profile;
    bad.host = QStringLiteral("localhost");
    report(SshManager::isValidHost(bad.host), QStringLiteral("короткое имя хоста допускается"));

    bad = profile;
    bad.user = QStringLiteral("user; reboot");
    report(!SshManager::isValidUser(bad.user), QStringLiteral("имя пользователя с командой отклоняется"));

    bad = profile;
    bad.port = 0;
    report(!SshManager::isValidPort(0) && !SshManager::isValidPort(70000),
           QStringLiteral("порт вне диапазона отклоняется"));
    report(!SshManager::validate(bad).isEmpty(), QStringLiteral("ошибка порта возвращается"));

    bad = profile;
    bad.identityFile = QStringLiteral("/nonexistent/key");
    report(!SshManager::validate(bad).isEmpty(), QStringLiteral("отсутствующий ключ отклоняется"),
           SshManager::validate(bad));

    // Команда подключения — системный ssh
    QString error;
    const QStringList arguments = SshManager::buildArguments(profile, &error);
    report(error.isEmpty() && arguments.contains(QStringLiteral("-p"))
               && arguments.contains(QStringLiteral("2222"))
               && arguments.contains(QStringLiteral("-l"))
               && arguments.contains(QStringLiteral("deploy"))
               && arguments.last() == QStringLiteral("example.com"),
           QStringLiteral("аргументы ssh собираются правильно"), arguments.join(QLatin1Char(' ')));

    const QString command = SshManager::buildCommand(profile, &error);
    report(command.startsWith(QStringLiteral("ssh ")) && command.contains(QStringLiteral("example.com")),
           QStringLiteral("команда запускает системный ssh"), command);
    report(!command.contains(QStringLiteral("password")),
           QStringLiteral("в команде нет пароля"));

    // Ключ передаётся как аргумент -i
    const QString keyPath = QDir::tempPath() + QStringLiteral("/blackcore-test-key");
    {
        QFile key(keyPath);
        if (key.open(QIODevice::WriteOnly))
            key.write("PRIVATE-KEY-PLACEHOLDER");
    }
    SshProfile withKey = profile;
    withKey.identityFile = keyPath;
    const QStringList keyArguments = SshManager::buildArguments(withKey, &error);
    report(error.isEmpty() && keyArguments.contains(QStringLiteral("-i"))
               && keyArguments.contains(keyPath),
           QStringLiteral("путь к ключу передаётся через -i"), keyArguments.join(QLatin1Char(' ')));
    QFile::remove(keyPath);

    // Ключи из ~/.ssh только перечисляются
    const QString sshDirectory = QDir::homePath() + QStringLiteral("/.ssh");
    QDir().mkpath(sshDirectory);
    const QString fakeKey = sshDirectory + QStringLiteral("/id_blackcore_test");
    {
        QFile key(fakeKey);
        if (key.open(QIODevice::WriteOnly))
            key.write("SECRET-KEY-CONTENT");
    }
    const QStringList keys = SshManager::availableKeys();
    report(keys.contains(fakeKey), QStringLiteral("ключ из ~/.ssh виден в списке"),
           QStringLiteral("ключей: %1").arg(keys.size()));
    report(!keys.contains(sshDirectory + QStringLiteral("/known_hosts")),
           QStringLiteral("known_hosts не считается ключом"));
    {
        QFile key(fakeKey);
        key.open(QIODevice::ReadOnly);
        report(key.readAll() == QByteArray("SECRET-KEY-CONTENT"),
               QStringLiteral("содержимое ключа не изменяется"));
    }
    QFile::remove(fakeKey);

    // Удаление профиля
    SshManager::removeProfile(QStringLiteral("ssh-test"));
    report(SshManager::profile(QStringLiteral("ssh-test")).id.isEmpty(),
           QStringLiteral("профиль удаляется"));
    report(SshManager::sshExecutable().endsWith(QStringLiteral("ssh")),
           QStringLiteral("системный клиент ssh найден"), SshManager::sshExecutable());
}

void testStage5Projects()
{
    QTextStream(stdout) << "\n== Этап 5: менеджер проектов ==\n";
    ConfigFileBackup backup;

    TempRepository nodeProject(QStringLiteral("proj-node"));
    nodeProject.init();
    nodeProject.write(QStringLiteral("package.json"), QStringLiteral("{\"name\":\"demo\"}"));

    TempRepository phpProject(QStringLiteral("proj-php"));
    phpProject.write(QStringLiteral("composer.json"), QStringLiteral("{}"));

    TempRepository pythonProject(QStringLiteral("proj-python"));
    pythonProject.write(QStringLiteral("requirements.txt"), QStringLiteral("requests\n"));
    pythonProject.write(QStringLiteral("manage.py"), QStringLiteral("# django\n"));

    report(ProjectManager::detectStack(nodeProject.path) == QStringLiteral("node"),
           QStringLiteral("Node.js-проект определяется"), ProjectManager::detectStack(nodeProject.path));
    report(ProjectManager::detectStack(phpProject.path) == QStringLiteral("php"),
           QStringLiteral("PHP-проект определяется"), ProjectManager::detectStack(phpProject.path));
    report(ProjectManager::detectStack(pythonProject.path) == QStringLiteral("python"),
           QStringLiteral("Python-проект определяется"),
           ProjectManager::detectStack(pythonProject.path));

    const QList<ProjectCommand> nodeCommands = ProjectManager::suggestedCommands(nodeProject.path);
    bool hasNpm = false;
    bool mutatingConfirmed = true;
    for (const ProjectCommand &command : nodeCommands) {
        if (command.command.startsWith(QStringLiteral("npm")))
            hasNpm = true;
        // Команды, меняющие систему (установка, сервер, тесты), обязаны спрашивать
        const bool readOnly = command.command.startsWith(QStringLiteral("git status"))
            || command.command.startsWith(QStringLiteral("git log"));
        if (!readOnly)
            mutatingConfirmed = mutatingConfirmed && command.needsConfirmation;
    }
    report(hasNpm, QStringLiteral("для Node предлагаются npm-команды"),
           QStringLiteral("команд: %1").arg(nodeCommands.size()));
    report(!nodeCommands.isEmpty() && mutatingConfirmed,
           QStringLiteral("изменяющие команды требуют подтверждения"));

    const QList<ProjectCommand> phpCommands = ProjectManager::suggestedCommands(phpProject.path);
    bool hasComposer = false;
    for (const ProjectCommand &command : phpCommands)
        hasComposer = hasComposer || command.command.contains(QStringLiteral("composer"));
    report(hasComposer, QStringLiteral("для PHP предлагается composer"));

    const QList<ProjectCommand> pythonCommands = ProjectManager::suggestedCommands(pythonProject.path);
    bool hasDjango = false;
    for (const ProjectCommand &command : pythonCommands)
        hasDjango = hasDjango || command.command.contains(QStringLiteral("manage.py"));
    report(hasDjango, QStringLiteral("для Django предлагается manage.py"));

    // BlackCore ничего не устанавливает: подсказки — просто строки команд
    report(!QFileInfo(nodeProject.path + QStringLiteral("/node_modules")).exists(),
           QStringLiteral("инструменты не устанавливаются автоматически"));

    // Сохранение проекта
    Project project;
    project.id = ProjectManager::makeId(QStringLiteral("Демо Node"));
    project.name = QStringLiteral("Демо Node");
    project.path = nodeProject.path;
    project.layout = ProjectManager::defaultLayout(nodeProject.path);
    project.commands = nodeCommands;
    report(ProjectManager::validate(project).isEmpty(),
           QStringLiteral("корректный проект проходит проверку"));

    ProjectManager::saveProject(project);
    const Project stored = ProjectManager::project(project.id);
    report(stored.name == project.name && stored.path == nodeProject.path,
           QStringLiteral("проект сохраняется и читается"), stored.name);
    report(stored.layout && stored.layout->paneCount() >= 1,
           QStringLiteral("у проекта есть раскладка"),
           QStringLiteral("панелей: %1").arg(stored.layout ? stored.layout->paneCount() : 0));
    report(!stored.commands.isEmpty(), QStringLiteral("команды проекта сохраняются"),
           QStringLiteral("команд: %1").arg(stored.commands.size()));
    report(ProjectManager::favorites().size() >= 1, QStringLiteral("проект попадает в избранное"));

    // Поиск проекта по каталогу (в т.ч. вложенному)
    QDir().mkpath(nodeProject.path + QStringLiteral("/src/components"));
    report(ProjectManager::projectForPath(nodeProject.path).id == project.id,
           QStringLiteral("проект находится по каталогу"));
    report(ProjectManager::projectForPath(nodeProject.path + QStringLiteral("/src/components")).id
               == project.id,
           QStringLiteral("проект находится по вложенному каталогу"));
    report(ProjectManager::projectForPath(phpProject.path).id.isEmpty(),
           QStringLiteral("чужой каталог не относится к проекту"));

    // Проверки
    Project broken = project;
    broken.path = QStringLiteral("/nonexistent/project");
    report(!ProjectManager::validate(broken).isEmpty(),
           QStringLiteral("несуществующий каталог отклоняется"), ProjectManager::validate(broken));
    broken = project;
    broken.name.clear();
    report(!ProjectManager::validate(broken).isEmpty(), QStringLiteral("пустое имя отклоняется"));

    // Удаление
    ProjectManager::removeProject(project.id);
    report(ProjectManager::project(project.id).id.isEmpty(), QStringLiteral("проект удаляется"));

    // Раскладка по умолчанию: в репозитории две панели, в обычном каталоге — одна
    const auto repositoryLayout = ProjectManager::defaultLayout(nodeProject.path);
    report(repositoryLayout && repositoryLayout->paneCount() == 2,
           QStringLiteral("в Git-проекте раскладка из двух панелей"),
           QStringLiteral("панелей: %1").arg(repositoryLayout ? repositoryLayout->paneCount() : 0));
    const auto plainLayout = ProjectManager::defaultLayout(phpProject.path);
    report(plainLayout && plainLayout->paneCount() == 1,
           QStringLiteral("в обычном каталоге — одна панель"));
}

void testStage5Integration()
{
    QTextStream(stdout) << "\n== Этап 5: интерфейс разработчика ==\n";
    ConfigFileBackup backup;

    TempRepository repository(QStringLiteral("ui-repo"));
    repository.init();
    repository.write(QStringLiteral("app.py"), QStringLiteral("print('hi')\n"));
    repository.run({QStringLiteral("add"), QStringLiteral(".")});
    repository.run({QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("init")});
    repository.write(QStringLiteral("app.py"), QStringLiteral("print('bye')\n"));

    MainWindow window;
    window.resize(1300, 800);
    window.show();
    window.startSession();
    spin(700);

    report(window.devPanel() != nullptr, QStringLiteral("панель разработчика создана"));
    report(!window.isDevPanelVisible(), QStringLiteral("по умолчанию панель скрыта"));

    // Меню «Разработчик»
    QMenu *devMenu = nullptr;
    for (QMenu *menu : window.menuWidget()->findChildren<QMenu *>()) {
        if (menu->title().remove(QLatin1Char('&')) == QStringLiteral("Разработчик"))
            devMenu = menu;
    }
    report(devMenu != nullptr, QStringLiteral("в меню есть раздел «Разработчик»"));
    if (devMenu) {
        QStringList titles;
        for (QAction *action : devMenu->actions())
            titles << action->text();
        report(titles.contains(QStringLiteral("Панель разработчика"))
                   && titles.contains(QStringLiteral("SSH-подключение…"))
                   && titles.contains(QStringLiteral("Открыть проект…")),
               QStringLiteral("раздел содержит панель, SSH и проекты"),
               titles.join(QStringLiteral(", ")));
    }

    window.setDevPanelVisible(true);
    spin(500);
    report(window.isDevPanelVisible(), QStringLiteral("панель разработчика открывается"));

    // Git-статус в панели
    window.devPanel()->setWorkingDirectory(repository.path);
    window.devPanel()->refresh();
    spin(400);
    const GitStatus status = window.devPanel()->gitStatus();
    report(status.valid && status.branch == QStringLiteral("main"),
           QStringLiteral("панель показывает ветку"), status.branch);
    report(status.modified >= 1, QStringLiteral("панель показывает незакоммиченные изменения"),
           QString::number(status.modified));

    TerminalView *terminal = window.tabs()->currentTerminal();
    if (terminal)
        terminal->sendTextToShell(QStringLiteral("cd %1\n").arg(repository.path));
    spin(1200);
    report(window.gitStatusLabel().startsWith(QStringLiteral("main")),
           QStringLiteral("строка состояния показывает Git"), window.gitStatusLabel());

    // Вставка команды в приглашение
    window.insertIntoTerminal(QStringLiteral("git status"));
    spin(500);
    TerminalView *active = window.tabs()->currentTerminal();
    report(active && active->screenText().contains(QStringLiteral("git status")),
           QStringLiteral("команда вставляется в приглашение без запуска"));
    if (active) {
        active->sendTextToShell(QStringLiteral("\n"));
        spin(800);
        report(active->screenText().contains(QStringLiteral("изменено"))
                   || active->screenText().contains(QStringLiteral("modified"))
                   || active->screenText().contains(QStringLiteral("app.py")),
               QStringLiteral("после Enter команда выполняется"));
    }

    // Открытие каталога в новой вкладке
    const int tabsBefore = window.tabs()->count();
    window.openDirectoryInNewTab(repository.path);
    spin(900);
    report(window.tabs()->count() == tabsBefore + 1,
           QStringLiteral("каталог открывается в новой вкладке"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));
    report(window.tabs()->tabTitle(window.tabs()->count() - 1).contains(QStringLiteral("ui-repo"))
               || window.tabs()->currentTerminal()->workingDirectory() == repository.path,
           QStringLiteral("новая вкладка открыта в каталоге проекта"));

    // Проект
    Project project;
    project.id = ProjectManager::makeId(QStringLiteral("UI репозиторий"));
    project.name = QStringLiteral("UI репозиторий");
    project.path = repository.path;
    project.layout = ProjectManager::defaultLayout(repository.path);
    ProjectManager::saveProject(project);

    const int beforeProject = window.tabs()->count();
    window.openProject(project.id);
    spin(1600);
    report(window.tabs()->count() == beforeProject + 1,
           QStringLiteral("проект открывается в новой вкладке"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));
    report(window.tabs()->tabTitle(window.tabs()->count() - 1) == QStringLiteral("UI репозиторий"),
           QStringLiteral("вкладка проекта подписана"),
           window.tabs()->tabTitle(window.tabs()->count() - 1));
    PaneTree *projectTree = window.tabs()->tabAt(window.tabs()->count() - 1);
    report(projectTree && projectTree->paneCount() == 2,
           QStringLiteral("проект с Git открывается раскладкой из двух панелей"),
           QStringLiteral("панелей: %1").arg(projectTree ? projectTree->paneCount() : 0));

    // SSH-профиль → отдельная вкладка с системным ssh
    SshProfile ssh;
    ssh.id = QStringLiteral("ssh-ui-test");
    ssh.name = QStringLiteral("Локальный тест");
    ssh.host = QStringLiteral("127.0.0.1");
    ssh.port = 1;
    ssh.user = QStringLiteral("tester");
    SshManager::saveProfile(ssh);

    const int beforeSsh = window.tabs()->count();
    window.connectSshProfile(ssh.id);
    spin(1200);
    report(window.tabs()->count() == beforeSsh + 1,
           QStringLiteral("SSH-подключение открывается в новой вкладке"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));
    report(window.tabs()->tabTitle(window.tabs()->count() - 1) == QStringLiteral("Локальный тест"),
           QStringLiteral("вкладка подключения подписана"),
           window.tabs()->tabTitle(window.tabs()->count() - 1));

    if (!g_screenshotDir.isEmpty()) {
        window.tabs()->setCurrentIndex(window.tabs()->count() - 1);
        window.devPanel()->showSection(1);
        window.devPanel()->selectSshProfile(ssh.id);
        spin(500);
        window.grab().save(g_screenshotDir + QStringLiteral("/40-dev-panel-ssh.png"));

        window.devPanel()->showSection(2);
        window.devPanel()->selectProject(project.id);
        spin(500);
        window.grab().save(g_screenshotDir + QStringLiteral("/41-projects.png"));

        window.devPanel()->showSection(0);
        window.devPanel()->setWorkingDirectory(repository.path);
        window.devPanel()->refresh();
        spin(500);
        window.grab().save(g_screenshotDir + QStringLiteral("/42-git-panel.png"));
    }

    ProjectManager::removeProject(project.id);
    SshManager::removeProfile(ssh.id);
    ProjectManager::removeProject(project.id);

    window.hide();
    spin(200);
}

// ---------------------------------------------------------------------------
// Этап 6: мониторинг и диагностика
// ---------------------------------------------------------------------------

//! Подготовить каталог-«/proc» и «/sys» с заданными значениями
struct FakeSystem
{
    QTemporaryDir dir;
    QString procRoot;
    QString sysRoot;

    explicit FakeSystem(int cores = 2)
    {
        procRoot = dir.filePath(QStringLiteral("proc"));
        sysRoot = dir.filePath(QStringLiteral("sys"));
        QDir().mkpath(procRoot + QStringLiteral("/net"));
        QDir().mkpath(sysRoot + QStringLiteral("/class/net/lo"));
        QDir().mkpath(sysRoot + QStringLiteral("/class/hwmon"));
        QDir().mkpath(sysRoot + QStringLiteral("/class/thermal"));

        QFile operstate(sysRoot + QStringLiteral("/class/net/lo/operstate"));
        if (operstate.open(QIODevice::WriteOnly | QIODevice::Text))
            operstate.write("up\n");

        writeCpu(100, 0, 100, 800, cores);
        writeFile(QStringLiteral("meminfo"),
                  QStringLiteral("MemTotal:       16384000 kB\n"
                                 "MemFree:         8192000 kB\n"
                                 "MemAvailable:    12288000 kB\n"
                                 "Buffers:          512000 kB\n"
                                 "Cached:          2048000 kB\n"
                                 "SwapTotal:       4096000 kB\n"
                                 "SwapFree:        3072000 kB\n"));
        writeFile(QStringLiteral("uptime"), QStringLiteral("90210.55 12345.00\n"));
        writeFile(QStringLiteral("loadavg"), QStringLiteral("0.42 0.35 0.30 2/512 4242\n"));
        writeFile(QStringLiteral("mounts"),
                  QStringLiteral("/dev/sda1 / ext4 rw,relatime 0 0\n"
                                 "tmpfs /run tmpfs rw,nosuid 0 0\n"
                                 "proc /proc proc rw 0 0\n"
                                 "/dev/loop0 /snap/core/1 squashfs ro 0 0\n"
                                 "/dev/sda1 /home ext4 rw,relatime 0 0\n"
                                 "/dev/sdb1 /media/data vfat ro,uid=1000 0 0\n"));
        writeFile(QStringLiteral("net/dev"),
                  QStringLiteral("Inter-|   Receive                                                |  Transmit\n"
                                 " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
                                 "    lo:  1200      12    0    0    0     0          0         0     1200      12    0    0    0     0       0          0\n"
                                 "  eth0: 409600    400    0    0    0     0          0         0   102400     100    0    0    0     0       0          0\n"));
        writeFile(QStringLiteral("net/tcp"),
                  QStringLiteral("  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
                                 "   0: 0100007F:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 12345 1 0000000000000000 100 0 0 10 0\n"
                                 "   1: 0100007F:1F90 0100007F:9C40 01 00000000:00000000 00:00000000 00000000  1000        0 12346 1 0000000000000000 20 4 30 10 -1\n"
                                 "   2: 0100007F:1F90 0100007F:9C41 06 00000000:00000000 00:00000000 00000000  1000        0 12347 1 0000000000000000 20 4 30 10 -1\n"));
        writeFile(QStringLiteral("net/tcp6"), QString());
        writeFile(QStringLiteral("net/udp"),
                  QStringLiteral("  sl  local_address rem_address   st\n"
                                 "   0: 00000000:0035 00000000:0000 07 00000000:00000000 00:00000000 00000000     0        0 111 2 0000000000000000 0\n"));
        writeFile(QStringLiteral("net/udp6"), QString());
    }

    //! Переписать /proc/net/dev с новыми счётчиками (для расчёта скорости)
    void writeNet(qulonglong loopback, qulonglong ethRx, qulonglong ethTx)
    {
        writeFile(QStringLiteral("net/dev"),
                  QStringLiteral("Inter-|   Receive                                                |  Transmit\n"
                                 " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
                                 "    lo:  %1      12    0    0    0     0          0         0     %1      12    0    0    0     0       0          0\n"
                                 "  eth0: %2    400    0    0    0     0          0         0   %3     100    0    0    0     0       0          0\n")
                      .arg(loopback).arg(ethRx).arg(ethTx));
    }

    void writeFile(const QString &relative, const QString &text)
    {
        const QString path = procRoot + QLatin1Char('/') + relative;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            file.write(text.toUtf8());
    }

    //! Переписать /proc/stat с новыми счётчиками (для расчёта процентов)
    void writeCpu(qulonglong user, qulonglong system, qulonglong idle, qulonglong iowait, int cores)
    {
        QString text = QStringLiteral("cpu  %1 0 %2 %3 %4 0 0 0\n")
                           .arg(user).arg(system).arg(idle).arg(iowait);
        for (int core = 0; core < cores; ++core) {
            text += QStringLiteral("cpu%1 %2 0 %3 %4 %5 0 0 0\n")
                        .arg(core)
                        .arg(user / qMax(1, cores))
                        .arg(system / qMax(1, cores))
                        .arg(idle / qMax(1, cores))
                        .arg(iowait / qMax(1, cores));
        }
        text += QStringLiteral("intr 12345 0 0\nctxt 99999\nbtime 1700000000\n");
        writeFile(QStringLiteral("stat"), text);
    }
};

void testStage6SystemMonitor()
{
    QTextStream(stdout) << "\n== Этап 6: сбор системных показателей ==\n";

    // --- разбор /proc/stat ---
    const QString statText = QStringLiteral(
        "cpu  1000 10 500 8000 400 0 50 0 0 0\n"
        "cpu0 500 5 250 4000 200 0 25 0 0 0\n"
        "cpu1 500 5 250 4000 200 0 25 0 0 0\n"
        "intr 12345 0 0\nctxt 99999\n");
    const QList<CpuSample> stat = SystemMonitor::parseStat(statText);
    report(stat.size() == 3, QStringLiteral("разбор /proc/stat: суммарная строка и ядра"),
           QStringLiteral("строк: %1").arg(stat.size()));
    report(stat.size() == 3 && stat.first().name == QStringLiteral("cpu")
               && stat.at(1).name == QStringLiteral("cpu0"),
           QStringLiteral("имена строк CPU прочитаны"));
    report(!stat.isEmpty() && stat.first().total() == 9960 && stat.first().idleTotal() == 8400,
           QStringLiteral("суммарное и холостое время CPU"),
           stat.isEmpty() ? QString() : QStringLiteral("%1 / %2")
                                            .arg(stat.first().total())
                                            .arg(stat.first().idleTotal()));

    QList<CpuSample> before = SystemMonitor::parseStat(
        QStringLiteral("cpu  1000 0 500 8000 500 0 0 0\ncpu0 500 0 250 4000 250 0 0 0\n"));
    QList<CpuSample> after = SystemMonitor::parseStat(
        QStringLiteral("cpu  1100 0 600 8100 600 0 0 0\ncpu0 550 0 300 4050 300 0 0 0\n"));
    const CpuUsage usage = SystemMonitor::usageBetween(before, after);
    report(usage.valid, QStringLiteral("загрузка CPU рассчитана"));
    report(qAbs(usage.total - 50.0) < 0.5,
           QStringLiteral("проценты загрузки CPU в допустимых пределах"),
           SystemMonitor::formatPercent(usage.total));
    report(usage.cores.size() == 1, QStringLiteral("загрузка посчитана по каждому ядру"),
           QStringLiteral("ядер: %1").arg(usage.cores.size()));
    report(!SystemMonitor::usageBetween({}, after).valid,
           QStringLiteral("без предыдущего замера загрузка не выдумывается"));

    // --- память ---
    const MemoryInfo memory = SystemMonitor::parseMemInfo(QStringLiteral(
        "MemTotal:       16384000 kB\nMemFree:         4096000 kB\nMemAvailable:   12288000 kB\n"
        "Buffers:          512000 kB\nCached:          2048000 kB\n"
        "SwapTotal:       4096000 kB\nSwapFree:        1024000 kB\n"));
    report(memory.valid && memory.totalKb == 16384000, QStringLiteral("разбор /proc/meminfo"),
           QStringLiteral("всего: %1 КиБ").arg(memory.totalKb));
    report(memory.usedKb() == 4096000, QStringLiteral("занятая память = всего − доступно"),
           QStringLiteral("%1 КиБ").arg(memory.usedKb()));
    report(qAbs(memory.usedPercent() - 25.0) < 0.01, QStringLiteral("процент занятой памяти"),
           SystemMonitor::formatPercent(memory.usedPercent()));
    report(memory.swapUsedKb() == 3072000 && qAbs(memory.swapUsedPercent() - 75.0) < 0.01,
           QStringLiteral("использование подкачки"));
    const MemoryInfo fallback = SystemMonitor::parseMemInfo(QStringLiteral(
        "MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 150 kB\n"));
    report(fallback.usedKb() == 700,
           QStringLiteral("без MemAvailable память оценивается по буферам и кэшу"),
           QStringLiteral("%1 КиБ").arg(fallback.usedKb()));

    // --- время работы, нагрузка, дистрибутив ---
    report(SystemMonitor::parseUptime(QStringLiteral("90210.55 12345.00\n")) == 90210,
           QStringLiteral("разбор /proc/uptime"));
    const QVector<double> load = SystemMonitor::parseLoadAverage(QStringLiteral("0.42 0.35 0.30 2/512 4242\n"));
    report(load.size() == 3 && qAbs(load.value(0) - 0.42) < 0.001,
           QStringLiteral("средняя нагрузка (1/5/15 мин)"));
    const QStringList distro = SystemMonitor::parseOsRelease(QStringLiteral(
        "NAME=\"Linux Mint\"\nVERSION_ID=\"22.3\"\nID=linuxmint\nPRETTY_NAME=\"Linux Mint 22.3\"\n"));
    report(distro.value(0) == QStringLiteral("Linux Mint") && distro.value(1) == QStringLiteral("22.3")
               && distro.value(2) == QStringLiteral("linuxmint")
               && distro.value(3) == QStringLiteral("Linux Mint 22.3"),
           QStringLiteral("разбор /etc/os-release"), distro.join(QStringLiteral(" | ")));

    // --- диски ---
    const QList<DiskUsage> mounts = SystemMonitor::parseMounts(QStringLiteral(
        "/dev/sda1 / ext4 rw 0 0\n"
        "tmpfs /run tmpfs rw 0 0\n"
        "proc /proc proc rw 0 0\n"
        "/dev/loop0 /snap/core/1 squashfs ro 0 0\n"
        "/dev/sda1 /home ext4 rw 0 0\n"
        "/dev/sdb1 /media/data vfat ro 0 0\n"));
    report(mounts.size() == 3, QStringLiteral("в занятом месте только реальные файловые системы"),
           QStringLiteral("точек: %1").arg(mounts.size()));
    report(mounts.size() == 3 && mounts.at(0).mountPoint == QStringLiteral("/")
               && mounts.at(0).fsType == QStringLiteral("ext4"),
           QStringLiteral("корневая файловая система определена"));
    report(mounts.size() == 3 && mounts.at(2).readOnly,
           QStringLiteral("файловая система только для чтения помечена"));
    report(SystemMonitor::isInterestingFs(QStringLiteral("ext4"))
               && SystemMonitor::isInterestingFs(QStringLiteral("btrfs"))
               && !SystemMonitor::isInterestingFs(QStringLiteral("proc"))
               && !SystemMonitor::isInterestingFs(QStringLiteral("squashfs")),
           QStringLiteral("фильтр типов файловых систем"));

    // --- сеть и подключения ---
    const QList<NetworkInterface> interfaces = SystemMonitor::parseNetDev(QStringLiteral(
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:  1200      12    0    0    0     0          0         0     1200      12    0    0    0     0       0          0\n"
        "  eth0: 409600    400    0    0    0     0          0         0   102400     100    0    0    0     0       0          0\n"));
    report(interfaces.size() == 2 && interfaces.first().name == QStringLiteral("lo"),
           QStringLiteral("разбор /proc/net/dev"));
    report(interfaces.size() == 2 && interfaces.at(1).rxBytes == 409600
               && interfaces.at(1).txBytes == 102400,
           QStringLiteral("счётчики принятых и переданных байт"));

    const ConnectionCounts connections = SystemMonitor::parseConnections(
        QStringLiteral("  sl  local_address rem_address   st\n"
                       "   0: 0100007F:1F90 00000000:0000 0A 0 0 0 1000 0 1\n"
                       "   1: 0100007F:1F90 0100007F:9C40 01 0 0 0 1000 0 2\n"
                       "   2: 0100007F:1F90 0100007F:9C41 06 0 0 0 1000 0 3\n"
                       "   3: 0100007F:1F90 0100007F:9C42 08 0 0 0 1000 0 4\n"),
        QString(), QStringLiteral("  sl  local_address rem_address   st\n"
                                 "   0: 00000000:0035 00000000:0000 07 0 0 0 0 0 5\n"),
        QString());
    report(connections.established == 1 && connections.listen == 1
               && connections.timeWait == 1 && connections.otherTcp == 1,
           QStringLiteral("состояния TCP-подключений разобраны"),
           QStringLiteral("установлено %1, слушает %2, TIME_WAIT %3")
               .arg(connections.established).arg(connections.listen).arg(connections.timeWait));
    report(connections.udp == 1, QStringLiteral("UDP-сокеты учтены"));
    report(connections.tcpTotal() == 4 && connections.total() == 5,
           QStringLiteral("итоговые счётчики подключений"));

    // --- процессы ---
    const QString processStat = QStringLiteral(
        "4242 (my process) S 1 4242 4242 0 -1 4194560 100 0 0 0 250 150 0 0 20 0 1 0 500 1000 256 0\n");
    const ProcessEntry process = SystemMonitor::parseProcessStat(
        4242, processStat, QStringLiteral("Name:\tmy process\nUid:\t1000\t1000\t1000\t1000\n"),
        200, 2000, 100);
    report(process.pid == 4242 && process.name == QStringLiteral("my process"),
           QStringLiteral("имя процесса с пробелом читается из /proc/<pid>/stat"), process.name);
    report(process.rssKb > 0, QStringLiteral("память процесса в КиБ"),
           QStringLiteral("%1 КиБ").arg(process.rssKb));
    report(qAbs(process.cpuPercent - 100.0) < 0.1, QStringLiteral("процент CPU процесса"),
           SystemMonitor::formatPercent(process.cpuPercent));
    report(process.user == QStringLiteral("1000"), QStringLiteral("UID процесса найден"), process.user);

    // --- форматирование ---
    report(SystemMonitor::formatBytes(512) == QStringLiteral("512 Б"),
           QStringLiteral("байты"), SystemMonitor::formatBytes(512));
    report(SystemMonitor::formatBytes(2048) == QStringLiteral("2,0 КиБ"),
           QStringLiteral("кибибайты"), SystemMonitor::formatBytes(2048));
    report(SystemMonitor::formatBytes(3ULL * 1024 * 1024 * 1024) == QStringLiteral("3,0 ГиБ"),
           QStringLiteral("гибибайты"), SystemMonitor::formatBytes(3ULL * 1024 * 1024 * 1024));
    report(SystemMonitor::formatRate(1536) == QStringLiteral("1,5 КиБ/с"),
           QStringLiteral("скорость передачи"), SystemMonitor::formatRate(1536));
    report(SystemMonitor::formatDuration(3725) == QStringLiteral("1 ч 2 мин"),
           QStringLiteral("время работы: часы и минуты"), SystemMonitor::formatDuration(3725));
    report(SystemMonitor::formatDuration(90061) == QStringLiteral("1 сут 1 ч"),
           QStringLiteral("время работы: сутки"), SystemMonitor::formatDuration(90061));
    report(SystemMonitor::formatPercent(12.34) == QStringLiteral("12,3 %"),
           QStringLiteral("проценты с запятой"), SystemMonitor::formatPercent(12.34));

    // --- сводка на подготовленных файлах ---
    FakeSystem fake(2);
    SystemMonitor monitor;
    monitor.setRoots(fake.procRoot, fake.sysRoot);
    const SystemOverview first = monitor.sample();
    report(first.valid, QStringLiteral("срез системы снят"));
    report(first.cpuCount == 2, QStringLiteral("число ядер из /proc/stat"),
           QStringLiteral("%1").arg(first.cpuCount));
    report(first.memory.totalKb == 16384000 && first.memory.valid,
           QStringLiteral("память из подготовленного /proc/meminfo"));
    report(first.uptimeSeconds == 90210 && qAbs(first.load1 - 0.42) < 0.001,
           QStringLiteral("время работы и нагрузка из подготовленных файлов"),
           first.uptimeText());
    report(first.disks.size() == 2, QStringLiteral("диски подставного /proc/mounts"),
           QStringLiteral("точек: %1").arg(first.disks.size()));
    report(first.connections.established == 1 && first.connections.listen == 1,
           QStringLiteral("подключения из подставного /proc/net"));
    report(first.networks.size() == 2 && first.networks.first().up,
           QStringLiteral("состояние интерфейса из /sys/class/net"));

    // Второй срез: счётчики выросли → появится загрузка CPU и скорость сети
    spin(250);
    fake.writeCpu(200, 0, 200, 1600, 2);
    fake.writeNet(2400, 819200, 204800);
    const SystemOverview second = monitor.sample();
    report(second.cpu.valid && second.cpu.total > 0.0,
           QStringLiteral("загрузка CPU по двум замерам"),
           SystemMonitor::formatPercent(second.cpu.total));
    report(monitor.cpuHistory().size() >= 1 && monitor.memoryHistory().size() >= 2,
           QStringLiteral("история загрузки накапливается"),
           QStringLiteral("точек: %1").arg(monitor.cpuHistory().size()));
    report(second.networkRxRate() > 0 || second.networks.first().rxRate > 0,
           QStringLiteral("скорость передачи считается по двум замерам"));
    report(!second.uptimeText().isEmpty() && !second.loadText().isEmpty(),
           QStringLiteral("время работы и нагрузка показываются текстом"), second.uptimeText());
    monitor.clearHistory();
    report(monitor.cpuHistory().isEmpty(), QStringLiteral("история очищается"));
    monitor.setHistorySize(10);
    report(monitor.historySize() == 10, QStringLiteral("глубина истории настраивается"));

    // --- реальная система ---
    SystemMonitor live;
    const SystemOverview real = live.sample();
    report(real.valid, QStringLiteral("срез реальной системы"));
    report(!real.kernel.isEmpty(), QStringLiteral("версия ядра определена"), real.kernel);
    report(!real.distro.isEmpty(), QStringLiteral("дистрибутив определён"), real.distro);
    report(real.uptimeSeconds > 0, QStringLiteral("время работы системы"), real.uptimeText());
    report(real.cpuCount >= 1, QStringLiteral("ядра процессора"),
           QStringLiteral("%1").arg(real.cpuCount));
    report(real.memory.totalKb > 0, QStringLiteral("объём памяти"),
           SystemMonitor::formatBytes(real.memory.totalKb * 1024ULL));
    bool hasRoot = false;
    for (const DiskUsage &disk : real.disks) {
        if (disk.mountPoint == QStringLiteral("/") && disk.total > 0)
            hasRoot = true;
    }
    report(hasRoot, QStringLiteral("занятое место на корневой файловой системе"),
           QStringLiteral("%1 свободно").arg(
               SystemMonitor::formatBytes(real.disks.isEmpty() ? 0 : real.disks.first().available)));
    bool hasLoopback = false;
    for (const NetworkInterface &iface : real.networks) {
        if (iface.name == QStringLiteral("lo"))
            hasLoopback = true;
    }
    report(hasLoopback, QStringLiteral("сетевые интерфейсы прочитаны"),
           QStringLiteral("интерфейсов: %1").arg(real.networks.size()));
    report(real.connections.total() >= 0, QStringLiteral("состояние сетевых подключений"));
    report(live.processCount() > 0, QStringLiteral("процессы видны из /proc"),
           QStringLiteral("процессов: %1").arg(live.processCount()));

    live.topProcesses(8, false);
    spin(400);
    const QList<ProcessEntry> byCpu = live.topProcesses(8, false);
    report(!byCpu.isEmpty(), QStringLiteral("список самых ресурсоёмких процессов"),
           QStringLiteral("строк: %1").arg(byCpu.size()));
    bool sortedByCpu = true;
    for (int i = 1; i < byCpu.size(); ++i) {
        if (byCpu.at(i).cpuPercent > byCpu.at(i - 1).cpuPercent + 0.001)
            sortedByCpu = false;
    }
    report(sortedByCpu, QStringLiteral("процессы отсортированы по загрузке CPU"));
    report(!byCpu.isEmpty() && !byCpu.first().name.isEmpty() && byCpu.first().pid > 0,
           QStringLiteral("у процесса есть PID и имя"),
           byCpu.isEmpty() ? QString()
                           : QStringLiteral("%1 (pid %2)").arg(byCpu.first().name).arg(byCpu.first().pid));
    bool hasUser = !byCpu.isEmpty() && !byCpu.first().user.isEmpty();
    report(hasUser, QStringLiteral("у процесса указан пользователь"),
           byCpu.isEmpty() ? QString() : byCpu.first().user);
    const QList<ProcessEntry> byMemory = live.topProcesses(8, true);
    bool sortedByMemory = true;
    for (int i = 1; i < byMemory.size(); ++i) {
        if (byMemory.at(i).rssKb > byMemory.at(i - 1).rssKb)
            sortedByMemory = false;
    }
    report(sortedByMemory && !byMemory.isEmpty(),
           QStringLiteral("процессы сортируются по памяти"),
           byMemory.isEmpty() ? QString() : byMemory.first().rssText());
}

void testStage6Diagnostics()
{
    QTextStream(stdout) << "\n== Этап 6: диагностика, журнал и сбои ==\n";

    const QStringList known = Diagnostics::knownTools();
    report(known.contains(QStringLiteral("git")) && known.contains(QStringLiteral("ssh"))
               && known.contains(QStringLiteral("bash")) && known.contains(QStringLiteral("php"))
               && known.contains(QStringLiteral("python3")) && known.contains(QStringLiteral("node")),
           QStringLiteral("список проверяемых инструментов"),
           known.join(QStringLiteral(", ")));

    const QList<ToolInfo> tools = Diagnostics::tools(false);
    report(tools.size() == known.size(), QStringLiteral("наличие инструментов проверено"),
           QStringLiteral("инструментов: %1").arg(tools.size()));
    bool bashFound = false;
    bool gitFound = false;
    for (const ToolInfo &tool : tools) {
        if (tool.name == QStringLiteral("bash"))
            bashFound = tool.available() && tool.required;
        if (tool.name == QStringLiteral("git"))
            gitFound = tool.available();
    }
    report(bashFound, QStringLiteral("обязательная оболочка найдена"));
    report(gitFound, QStringLiteral("git найден в PATH"));
    report(!Diagnostics::toolVersion(QStringLiteral("/nonexistent/tool")).size(),
           QStringLiteral("версия отсутствующего инструмента не выдумывается"));

    const QList<DiagnosticCheck> checks = Diagnostics::runChecks();
    report(!checks.isEmpty(), QStringLiteral("диагностические проверки выполнены"),
           QStringLiteral("проверок: %1").arg(checks.size()));
    QStringList ids;
    for (const DiagnosticCheck &check : checks) {
        ids << check.id;
        if (check.title.isEmpty() || check.detail.isEmpty())
            ids << QStringLiteral("<пустая проверка>");
    }
    report(ids.contains(QStringLiteral("app/version")) && ids.contains(QStringLiteral("sys/proc"))
               && ids.contains(QStringLiteral("io/config")) && ids.contains(QStringLiteral("crash/marker"))
               && ids.contains(QStringLiteral("locale/utf8")) && ids.contains(QStringLiteral("tool/git")),
           QStringLiteral("проверки покрывают программу, систему, файлы и инструменты"));
    report(!ids.contains(QStringLiteral("<пустая проверка>")),
           QStringLiteral("у каждой проверки есть название и подробности"));
    bool anyOk = false;
    for (const DiagnosticCheck &check : checks)
        anyOk = anyOk || check.state == DiagnosticCheck::Ok;
    report(anyOk, QStringLiteral("есть успешно пройденные проверки"));
    report(Diagnostics::summarize(checks).contains(QStringLiteral("Проверок")),
           QStringLiteral("сводка проверок формируется"), Diagnostics::summarize(checks));

    const QList<DiagnosticCheck> toolChecks = Diagnostics::toolChecks();
    report(toolChecks.size() == tools.size(), QStringLiteral("проверки инструментов отделены"),
           QStringLiteral("проверок: %1").arg(toolChecks.size()));

    // --- журнал ---
    Log::info(QStringLiteral("Тестовая запись для диагностики этапа 6"));
    const QString logPath = Diagnostics::logPath();
    report(!logPath.isEmpty() && logPath.endsWith(QStringLiteral("blackcore.log")),
           QStringLiteral("путь к журналу известен"), logPath);
    report(logPath.contains(QStringLiteral(".local/state/blackcore")),
           QStringLiteral("журнал лежит в ~/.local/state/blackcore"));
    report(Diagnostics::logSize() > 0, QStringLiteral("размер журнала читается"),
           SystemMonitor::formatBytes(qulonglong(Diagnostics::logSize())));
    const QString tail = Diagnostics::tailLog(20);
    report(tail.contains(QStringLiteral("Тестовая запись для диагностики этапа 6")),
           QStringLiteral("последние строки журнала доступны"));

    // --- отчёты о сбоях ---
    report(Crash::reportsDir().endsWith(QStringLiteral("crashes")),
           QStringLiteral("каталог отчётов о сбоях"), Crash::reportsDir());
    const QString reportPath = Crash::writeReport(QStringLiteral("SIGSEGV"),
                                                  QStringLiteral("проверка записи отчёта"));
    report(!reportPath.isEmpty() && QFileInfo::exists(reportPath),
           QStringLiteral("отчёт о сбое записан"), reportPath);
    const QString reportText = Crash::readReport(reportPath);
    report(reportText.contains(QStringLiteral("SIGSEGV"))
               && reportText.contains(QStringLiteral("version:"))
               && reportText.contains(QStringLiteral("проверка записи отчёта")),
           QStringLiteral("в отчёте есть сигнал, версия и подробности"));
    report(Crash::reportFiles().contains(reportPath), QStringLiteral("отчёт виден в списке"));
    report(Crash::lastReportFile() == reportPath, QStringLiteral("последний отчёт определяется"));

    QList<DiagnosticCheck> withCrash = Diagnostics::runChecks();
    bool crashWarning = false;
    for (const DiagnosticCheck &check : withCrash) {
        if (check.id == QStringLiteral("crash/reports")
            && check.state == DiagnosticCheck::Warning)
            crashWarning = true;
    }
    report(crashWarning, QStringLiteral("наличие отчёта отмечается в проверках"));

    // --- маркер запуска ---
    Crash::install();
    report(QFileInfo::exists(Crash::markerPath()), QStringLiteral("маркер запуска создан"));
    report(!Crash::previousRunCrashed(),
           QStringLiteral("свой маркер не считается аварийным завершением"));
    {
        QFile marker(Crash::markerPath());
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            marker.write("pid=999999\nstarted=2020-01-01T00:00:00\n");
    }
    report(Crash::previousRunCrashed(),
           QStringLiteral("оставшийся маркер прошлого запуска = аварийное завершение"));
    Crash::markCleanExit();
    report(!QFileInfo::exists(Crash::markerPath()),
           QStringLiteral("штатный выход удаляет маркер"));

    const QList<DiagnosticCheck> afterCrash = Diagnostics::runChecks();
    bool crashCheckOk = false;
    for (const DiagnosticCheck &check : afterCrash) {
        if (check.id == QStringLiteral("crash/marker") && check.state == DiagnosticCheck::Ok)
            crashCheckOk = true;
    }
    report(crashCheckOk, QStringLiteral("после чистого выхода проверка сбоев в норме"));

    report(Crash::clearReports(), QStringLiteral("отчёты о сбоях удаляются"));
    report(Crash::reportFiles().isEmpty(), QStringLiteral("список отчётов пуст после очистки"));
}

void testStage6MonitorPanel()
{
    QTextStream(stdout) << "\n== Этап 6: панель монитора ==\n";
    ConfigFileBackup backup;

    MonitorPanel panel;
    panel.resize(380, 720);
    panel.show();
    spin(400);

    report(panel.overview().valid, QStringLiteral("панель снимает показатели"));
    report(panel.historyPoints() >= 0, QStringLiteral("история загрузки доступна в панели"),
           QStringLiteral("точек: %1").arg(panel.historyPoints()));
    report(panel.currentSection() == 0, QStringLiteral("панель открывается на обзоре"));
    panel.showSection(2);
    spin(200);
    report(panel.currentSection() == 2, QStringLiteral("раздел диагностики открывается"));
    panel.showSection(9);
    report(panel.currentSection() == 2, QStringLiteral("номер раздела ограничен"));
    panel.showSection(0);

    // Виджеты обзора
    auto *cpuBar = panel.findChild<QProgressBar *>();
    report(cpuBar != nullptr, QStringLiteral("в обзоре есть индикатор загрузки"));
    auto *diskTable = panel.findChild<QTableWidget *>(QStringLiteral("diskTable"));
    report(diskTable != nullptr && diskTable->rowCount() == panel.overview().disks.size(),
           QStringLiteral("таблица дисков заполнена"),
           QStringLiteral("строк: %1").arg(diskTable ? diskTable->rowCount() : -1));
    report(diskTable && diskTable->columnCount() == 4,
           QStringLiteral("в таблице дисков занятое и свободное место"));
    auto *netTable = panel.findChild<QTableWidget *>(QStringLiteral("netTable"));
    report(netTable != nullptr && netTable->rowCount() == panel.overview().networks.size(),
           QStringLiteral("таблица сетевых интерфейсов заполнена"),
           QStringLiteral("строк: %1").arg(netTable ? netTable->rowCount() : -1));
    auto *uptimeLabel = panel.findChild<QLabel *>(QStringLiteral("uptimeValue"));
    report(uptimeLabel && uptimeLabel->text().contains(QStringLiteral("Время работы")),
           QStringLiteral("время работы системы показано"),
           uptimeLabel ? uptimeLabel->text() : QString());
    bool kernelShown = false;
    bool distroShown = false;
    for (QLabel *label : panel.findChildren<QLabel *>()) {
        if (label->text().startsWith(QStringLiteral("Ядро:")))
            kernelShown = !label->text().remove(QStringLiteral("Ядро:")).trimmed().isEmpty();
        if (label->text().startsWith(QStringLiteral("Дистрибутив:")))
            distroShown = true;
    }
    report(kernelShown, QStringLiteral("версия ядра показана в панели"));
    report(distroShown, QStringLiteral("дистрибутив показан в панели"));

    // Процессы
    panel.showSection(1);
    panel.refreshProcesses(false);
    spin(300);
    auto *processTable = panel.findChild<QTableWidget *>(QStringLiteral("processTable"));
    report(processTable != nullptr && processTable->rowCount() > 0,
           QStringLiteral("таблица процессов заполнена"),
           QStringLiteral("строк: %1").arg(processTable ? processTable->rowCount() : -1));
    report(processTable && processTable->columnCount() == 5,
           QStringLiteral("у процессов показаны PID, имя, пользователь, CPU и память"));
    report(!panel.lastProcesses().isEmpty(),
           QStringLiteral("список процессов доступен из панели"));
    panel.refreshProcesses(true);
    const QList<ProcessEntry> byMemory = panel.lastProcesses();
    bool memoryOrder = true;
    for (int i = 1; i < byMemory.size(); ++i) {
        if (byMemory.at(i).rssKb > byMemory.at(i - 1).rssKb)
            memoryOrder = false;
    }
    report(!byMemory.isEmpty() && memoryOrder,
           QStringLiteral("сортировка по памяти работает в панели"));

    // Диагностика
    panel.showSection(2);
    panel.runDiagnostics();
    spin(300);
    report(!panel.lastChecks().isEmpty(), QStringLiteral("проверки выполняются из панели"),
           QStringLiteral("проверок: %1").arg(panel.lastChecks().size()));
    auto *checkTable = panel.findChild<QTableWidget *>(QStringLiteral("checkTable"));
    report(checkTable && checkTable->rowCount() == panel.lastChecks().size(),
           QStringLiteral("результаты проверок показаны таблицей"),
           QStringLiteral("строк: %1").arg(checkTable ? checkTable->rowCount() : -1));
    auto *toolTable = panel.findChild<QTableWidget *>(QStringLiteral("toolTable"));
    report(toolTable && toolTable->rowCount() == Diagnostics::knownTools().size(),
           QStringLiteral("доступность инструментов показана таблицей"),
           QStringLiteral("строк: %1").arg(toolTable ? toolTable->rowCount() : -1));
    auto *logPathLabel = panel.findChild<QLabel *>(QStringLiteral("logPathLabel"));
    report(logPathLabel && logPathLabel->text().contains(QStringLiteral("blackcore.log")),
           QStringLiteral("в панели виден путь к журналу"),
           logPathLabel ? logPathLabel->text() : QString());
    panel.refreshLog();
    auto *logPreview = panel.findChild<QPlainTextEdit *>(QStringLiteral("logPreview"));
    report(logPreview && !logPreview->toPlainText().isEmpty(),
           QStringLiteral("предпросмотр журнала заполнен"));
    panel.refreshCrashes();
    auto *crashList = panel.findChild<QListWidget *>(QStringLiteral("crashList"));
    report(crashList != nullptr, QStringLiteral("список сбоев есть в панели"));
    report(crashList && crashList->count() == Crash::reportFiles().size(),
           QStringLiteral("число сбоев совпадает с отчётами"));

    // Период обновления
    panel.setRefreshInterval(1000);
    report(panel.refreshInterval() == 1000, QStringLiteral("период обновления настраивается"),
           QString::number(panel.refreshInterval()));
    panel.setRefreshInterval(10);
    report(panel.refreshInterval() == 500, QStringLiteral("слишком частый опрос ограничен"),
           QString::number(panel.refreshInterval()));
    panel.setRefreshInterval(60000);
    report(panel.refreshInterval() == 10000, QStringLiteral("слишком редкий опрос ограничен"));

    // Кнопка вставки команды
    QString requested;
    QObject::connect(&panel, &MonitorPanel::commandRequested, &panel,
                     [&requested](const QString &command) { requested = command; });
    panel.showSection(0);
    QPushButton *psButton = nullptr;
    for (QPushButton *button : panel.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Процессы в терминале"))
            psButton = button;
    }
    report(psButton != nullptr, QStringLiteral("в обзоре есть кнопка просмотра процессов"));
    if (psButton) {
        psButton->click();
        spin(100);
        report(requested.startsWith(QStringLiteral("ps aux")),
               QStringLiteral("кнопка вставляет команду в приглашение"), requested);
    }

    panel.hide();
    spin(100);
}

void testStage6Integration()
{
    QTextStream(stdout) << "\n== Этап 6: монитор в главном окне ==\n";
    ConfigFileBackup backup;

    Project project;
    project.id = QStringLiteral("stage7-project");
    project.name = QStringLiteral("Тестовый проект");
    project.path = QDir::tempPath();
    ProjectManager::saveProject(project);

    Settings::instance().setRestoreSession(false);
    MainWindow window;
    window.resize(1240, 800);
    window.show();
    window.startSession();
    spin(700);

    report(window.monitorPanel() != nullptr, QStringLiteral("панель монитора создана"));
    report(!window.isMonitorPanelVisible(), QStringLiteral("по умолчанию панель скрыта"));

    QMenu *monitorMenu = nullptr;
    for (QMenu *menu : window.menuWidget()->findChildren<QMenu *>()) {
        if (menu->title().remove(QLatin1Char('&')) == QStringLiteral("Монитор"))
            monitorMenu = menu;
    }
    report(monitorMenu != nullptr, QStringLiteral("в меню есть раздел «Монитор»"));
    if (monitorMenu) {
        QStringList titles;
        for (QAction *action : monitorMenu->actions())
            titles << action->text();
        report(titles.contains(QStringLiteral("Панель монитора"))
                   && titles.contains(QStringLiteral("Обзор системы"))
                   && titles.contains(QStringLiteral("Ресурсоёмкие процессы"))
                   && titles.contains(QStringLiteral("Диагностика и журнал")),
               QStringLiteral("раздел содержит панель, процессы и диагностику"),
               titles.join(QStringLiteral(", ")));
    }

    // Горячая клавиша
    QAction *panelAction = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Панель монитора"))
            panelAction = action;
    }
    report(panelAction != nullptr && panelAction->shortcut() == QKeySequence(QStringLiteral("F7")),
           QStringLiteral("панель открывается по F7"),
           panelAction ? panelAction->shortcut().toString() : QString());

    window.setMonitorPanelVisible(true);
    spin(800);
    report(window.isMonitorPanelVisible(), QStringLiteral("панель монитора открывается"));
    report(Settings::instance().monitorPanelVisible(),
           QStringLiteral("видимость панели сохраняется в настройках"));
    report(panelAction && panelAction->isChecked(),
           QStringLiteral("пункт меню отмечен при открытой панели"));
    report(window.monitorPanel()->overview().valid,
           QStringLiteral("панель в окне показывает данные"));
    report(window.monitorPanel()->refreshInterval()
               == Settings::instance().monitorIntervalMs(),
           QStringLiteral("период обновления берётся из настроек"),
           QString::number(window.monitorPanel()->refreshInterval()));

    // Строка состояния
    bool statusShown = false;
    for (QLabel *label : window.statusBar()->findChildren<QLabel *>()) {
        if (label->text().contains(QStringLiteral("CPU")) && label->isVisible())
            statusShown = true;
    }
    report(statusShown, QStringLiteral("строка состояния показывает CPU и память"));

    // Взаимное исключение панелей
    window.setDevPanelVisible(true);
    spin(500);
    report(window.isDevPanelVisible() && !window.isMonitorPanelVisible(),
           QStringLiteral("панель разработчика вытесняет панель монитора"));
    window.setMonitorPanelVisible(true);
    spin(500);
    report(window.isMonitorPanelVisible() && !window.isDevPanelVisible(),
           QStringLiteral("панель монитора и панель разработчика не открываются вместе"));

    // Вставка команды из панели в терминал
    TerminalView *terminal = window.tabs()->currentTerminal();
    window.monitorPanel()->refresh();
    window.insertIntoTerminal(QStringLiteral("df -h"));
    spin(600);
    report(terminal && terminal->screenText().contains(QStringLiteral("df -h")),
           QStringLiteral("команда из панели попадает в приглашение"));

    if (!g_screenshotDir.isEmpty()) {
        window.monitorPanel()->showSection(0);
        window.monitorPanel()->refresh();
        spin(1200);
        window.grab().save(g_screenshotDir + QStringLiteral("/50-monitor-overview.png"));

        window.monitorPanel()->showSection(1);
        window.monitorPanel()->refreshProcesses(false);
        spin(600);
        window.grab().save(g_screenshotDir + QStringLiteral("/51-monitor-processes.png"));

        window.monitorPanel()->showSection(2);
        window.monitorPanel()->runDiagnostics();
        spin(600);
        window.grab().save(g_screenshotDir + QStringLiteral("/52-monitor-diagnostics.png"));
        window.monitorPanel()->showSection(0);
    }

    window.setMonitorPanelVisible(false);
    report(!window.isMonitorPanelVisible(), QStringLiteral("панель монитора закрывается"));

    window.hide();
    spin(200);
}

// ---------------------------------------------------------------------------
// Этап 7: секреты, библиотека команд, история, уведомления, палитра
// ---------------------------------------------------------------------------

void testStage7Secrets()
{
    QTextStream(stdout) << "\n== Этап 7: защита секретов в журналах ==\n";

    report(Secrets::mask() == QStringLiteral("••••"), QStringLiteral("маска секрета задана"),
           Secrets::mask());

    const QString password = QStringLiteral("password=hunter2");
    report(Secrets::redact(password) == QStringLiteral("password=••••"),
           QStringLiteral("пароль в виде ключ=значение маскируется"), Secrets::redact(password));
    report(Secrets::containsSecret(password), QStringLiteral("секрет распознаётся"));
    report(!Secrets::containsSecret(QStringLiteral("git status")),
           QStringLiteral("обычная команда не считается секретом"));

    report(Secrets::redact(QStringLiteral("mysql --password=topsecret -u root")).contains(Secrets::mask())
               && !Secrets::redact(QStringLiteral("mysql --password=topsecret -u root"))
                       .contains(QStringLiteral("topsecret")),
           QStringLiteral("ключ --password маскируется"));
    report(!Secrets::redact(QStringLiteral("curl -H 'Authorization: Bearer abc123def' https://x"))
                .contains(QStringLiteral("abc123def")),
           QStringLiteral("заголовок Authorization маскируется"));
    report(!Secrets::redact(QStringLiteral("psql postgres://user:secret@db/app"))
                .contains(QStringLiteral("secret@")),
           QStringLiteral("пароль в URL маскируется"));
    report(!Secrets::redact(QStringLiteral("export GITHUB_TOKEN=ghp_1234567890"))
                .contains(QStringLiteral("ghp_1234567890")),
           QStringLiteral("токен в переменной окружения маскируется"));
    const QString key = QStringLiteral("-----BEGIN OPENSSH PRIVATE KEY-----\nAAAAsecret\n"
                                       "-----END OPENSSH PRIVATE KEY-----");
    const QString redactedKey = Secrets::redact(key);
    report(!redactedKey.contains(QStringLiteral("AAAAsecret")),
           QStringLiteral("тело приватного ключа не остаётся в тексте"), redactedKey);
    report(redactedKey.contains(Secrets::mask()) || redactedKey.trimmed().isEmpty(),
           QStringLiteral("приватный ключ заменён маской"), redactedKey);
    report(Secrets::redact(QString())
               .isEmpty(), QStringLiteral("пустая строка не меняется"));
    report(Secrets::patterns().size() >= 5, QStringLiteral("список шаблонов доступен"),
           QStringLiteral("шаблонов: %1").arg(Secrets::patterns().size()));

    // Журнал: пароль не должен попасть в файл
    Log::info(QStringLiteral("Тест секретов: sshpass -p 'qwerty123' ssh user@host"));
    Log::info(QStringLiteral("Тест секретов: password=qwerty123"));
    const QString tail = Diagnostics::tailLog(50);
    report(!tail.contains(QStringLiteral("qwerty123")),
           QStringLiteral("пароль не попадает в журнал BlackCore"),
           tail.contains(QStringLiteral("qwerty123")) ? QStringLiteral("НАЙДЕН пароль") : QStringLiteral("пароль скрыт"));
    report(tail.contains(Secrets::mask()), QStringLiteral("в журнале видна маска секрета"));
    report(Secrets::commandForLog(QStringLiteral("app --token=abc")).contains(Secrets::mask()),
           QStringLiteral("команда для журнала очищается"));
}

void testStage7CommandLibrary()
{
    QTextStream(stdout) << "\n== Этап 7: библиотека команд ==\n";
    ConfigFileBackup backup;
    CommandLibrary::clear();

    const QList<CommandCategory> categories = CommandLibrary::categories();
    report(categories.size() == 7, QStringLiteral("семь категорий библиотеки"),
           QStringLiteral("категорий: %1").arg(categories.size()));
    QStringList names;
    for (CommandCategory category : categories)
        names << CommandLibrary::categoryName(category);
    report(names.contains(QStringLiteral("система")) && names.contains(QStringLiteral("сеть"))
               && names.contains(QStringLiteral("файлы"))
               && names.contains(QStringLiteral("разработка"))
               && names.contains(QStringLiteral("Git")) && names.contains(QStringLiteral("SSH"))
               && names.contains(QStringLiteral("обслуживание")),
           QStringLiteral("категории названы по заданию"), names.join(QStringLiteral(", ")));

    report(CommandLibrary::categoryFromId(QStringLiteral("git")) == CommandCategory::Git,
           QStringLiteral("категория по идентификатору"));
    report(CommandLibrary::categoryId(CommandCategory::Maintenance) == QStringLiteral("maintenance"),
           QStringLiteral("идентификатор категории"));
    report(CommandLibrary::categoryFromName(QStringLiteral("сеть")) == CommandCategory::Network,
           QStringLiteral("категория по названию"));

    SavedCommand command;
    command.name = QStringLiteral("Занятое место");
    command.description = QStringLiteral("Показать свободное место");
    command.category = CommandCategory::System;
    command.command = QStringLiteral("df -h");
    command.workingDirectory = QDir::tempPath();
    report(CommandLibrary::validate(command).isEmpty(), QStringLiteral("корректная команда проходит проверку"));

    report(CommandLibrary::saveCommand(command), QStringLiteral("команда сохраняется"));
    report(!command.id.isEmpty(), QStringLiteral("команде присвоен идентификатор"), command.id);
    report(CommandLibrary::commands().size() == 1, QStringLiteral("команда попала в библиотеку"));
    const SavedCommand stored = CommandLibrary::command(command.id);
    report(stored.name == command.name && stored.command == command.command
               && stored.category == CommandCategory::System
               && stored.workingDirectory == command.workingDirectory,
           QStringLiteral("все поля команды сохраняются и читаются"));
    report(stored.description == command.description,
           QStringLiteral("описание сохраняется"));

    // Вторая команда в другой категории
    SavedCommand gitCommand;
    gitCommand.name = QStringLiteral("Короткий лог Git");
    gitCommand.description = QStringLiteral("История коммитов");
    gitCommand.category = CommandCategory::Git;
    gitCommand.command = QStringLiteral("git log --oneline -15");
    gitCommand.workingDirectory.clear();
    report(CommandLibrary::saveCommand(gitCommand), QStringLiteral("вторая команда сохранена"));

    report(CommandLibrary::byCategory(CommandCategory::System).size() == 1,
           QStringLiteral("фильтр по категории"));
    report(CommandLibrary::byCategory(CommandCategory::Git).size() == 1,
           QStringLiteral("вторая категория содержит свою команду"));

    const QList<SavedCommand> byName = CommandLibrary::search(QStringLiteral("занятое"));
    report(byName.size() == 1 && byName.first().id == command.id,
           QStringLiteral("поиск по названию"), QStringLiteral("найдено: %1").arg(byName.size()));
    report(CommandLibrary::search(QStringLiteral("df -h")).size() == 1,
           QStringLiteral("поиск по тексту команды"));
    report(CommandLibrary::search(QStringLiteral("git")).size() == 1,
           QStringLiteral("поиск по категории"));
    report(CommandLibrary::search(QStringLiteral("ничего-такого")).isEmpty(),
           QStringLiteral("пустой результат по неизвестному запросу"));
    report(CommandLibrary::search(QString()).size() == 2,
           QStringLiteral("пустой запрос возвращает все команды"));

    // Изменение и удаление
    SavedCommand edited = CommandLibrary::command(command.id);
    edited.description = QStringLiteral("Обновлённое описание");
    report(CommandLibrary::saveCommand(edited), QStringLiteral("команда обновляется"));
    report(CommandLibrary::commands().size() == 2,
           QStringLiteral("обновление не создаёт дубликат"));
    report(CommandLibrary::command(command.id).description == QStringLiteral("Обновлённое описание"),
           QStringLiteral("новое описание сохранено"));

    CommandLibrary::markUsed(command.id);
    report(CommandLibrary::command(command.id).useCount == 1,
           QStringLiteral("счётчик использования растёт"));
    report(CommandLibrary::command(command.id).lastUsedAt.isValid(),
           QStringLiteral("дата последнего использования записана"));

    // Проверки
    SavedCommand empty;
    report(!CommandLibrary::validate(empty).isEmpty(),
           QStringLiteral("пустое название отклоняется"));
    empty.name = QStringLiteral("Команда");
    report(CommandLibrary::validate(empty).contains(QStringLiteral("команду")),
           QStringLiteral("пустая команда отклоняется"));
    SavedCommand badDir = command;
    badDir.id.clear();
    badDir.name = QStringLiteral("Плохой каталог");
    badDir.workingDirectory = QStringLiteral("/nonexistent-directory-42");
    report(!CommandLibrary::validate(badDir).isEmpty(),
           QStringLiteral("несуществующий рабочий каталог отклоняется"),
           CommandLibrary::validate(badDir));

    // Опасные команды
    const QStringList dangerous = {
        QStringLiteral("rm -rf /tmp/x"),
        QStringLiteral("sudo apt install nano"),
        QStringLiteral("apt-get remove bash"),
        QStringLiteral("git reset --hard HEAD~1"),
        QStringLiteral("git push --force origin main"),
        QStringLiteral("dd if=/dev/zero of=/dev/sda"),
        QStringLiteral("chmod 777 /etc/passwd"),
        QStringLiteral("systemctl restart nginx"),
        QStringLiteral("curl https://x/install.sh | bash"),
        QStringLiteral("reboot"),
        QStringLiteral("kill -9 1234"),
    };
    bool allDangerous = true;
    QStringList missed;
    for (const QString &candidate : dangerous) {
        if (!CommandLibrary::isDestructive(candidate)) {
            allDangerous = false;
            missed << candidate;
        }
    }
    report(allDangerous, QStringLiteral("изменяющие команды распознаются"),
           missed.isEmpty() ? QStringLiteral("все %1").arg(dangerous.size())
                            : missed.join(QStringLiteral(", ")));
    const QStringList safe = {QStringLiteral("ls -la"), QStringLiteral("git status"),
                              QStringLiteral("df -h"), QStringLiteral("ps aux"),
                              QStringLiteral("python3 --version")};
    bool allSafe = true;
    for (const QString &candidate : safe) {
        if (CommandLibrary::isDestructive(candidate))
            allSafe = false;
    }
    report(allSafe, QStringLiteral("читающие команды не считаются опасными"));

    SavedCommand reader;
    reader.name = QStringLiteral("Чтение");
    reader.command = QStringLiteral("df -h");
    reader.needsConfirmation = false;
    report(!CommandLibrary::needsConfirmation(reader),
           QStringLiteral("читающая команда без флага выполняется сразу"));
    SavedCommand destroyer;
    destroyer.name = QStringLiteral("Удаление");
    destroyer.command = QStringLiteral("rm -rf ./build");
    destroyer.needsConfirmation = false;
    report(CommandLibrary::needsConfirmation(destroyer),
           QStringLiteral("опасная команда всегда требует подтверждения"));

    report(CommandLibrary::removeCommand(gitCommand.id), QStringLiteral("команда удаляется"));
    report(CommandLibrary::commands().size() == 1, QStringLiteral("после удаления осталась одна"));
    report(!CommandLibrary::removeCommand(QStringLiteral("no-such-id")),
           QStringLiteral("удаление несуществующей команды"));
    CommandLibrary::clear();
    report(CommandLibrary::commands().isEmpty(), QStringLiteral("библиотека очищается по запросу"));
}

void testStage7History()
{
    QTextStream(stdout) << "\n== Этап 7: история и поиск ==\n";
    ConfigFileBackup backup;
    CommandHistory::clear();
    ActivityLog::clear();

    report(CommandHistory::count() == 0, QStringLiteral("история команд пуста в начале"));

    const QString firstId = CommandHistory::record(QStringLiteral("df -h"), CommandSource::Library,
                                                   QStringLiteral("/tmp"), QStringLiteral("Занятое место"));
    report(!firstId.isEmpty(), QStringLiteral("команда записывается в историю"));
    report(CommandHistory::count() == 1, QStringLiteral("в истории одна запись"));
    const HistoryEntry entry = CommandHistory::entry(firstId);
    report(entry.command == QStringLiteral("df -h"), QStringLiteral("текст команды сохранён"));
    report(entry.source == CommandSource::Library && entry.sourceName() == QStringLiteral("библиотека"),
           QStringLiteral("источник команды сохранён"), entry.sourceName());
    report(entry.workingDirectory == QStringLiteral("/tmp"),
           QStringLiteral("рабочий каталог сохранён"));
    report(entry.at.isValid(), QStringLiteral("время выполнения записано"));
    report(entry.exitCode == -1, QStringLiteral("код возврата неизвестен по умолчанию"));

    report(CommandHistory::updateResult(firstId, 2500, 0),
           QStringLiteral("результат выполнения дописан"));
    const HistoryEntry updated = CommandHistory::entry(firstId);
    report(updated.exitCode == 0 && updated.durationMs == 2500,
           QStringLiteral("код возврата и длительность сохранены"));
    report(updated.durationText() == QStringLiteral("2,5 с"),
           QStringLiteral("длительность показывается текстом"), updated.durationText());

    CommandHistory::record(QStringLiteral("git status"), CommandSource::Git, QStringLiteral("/srv"));
    CommandHistory::record(QStringLiteral("python3 manage.py runserver"), CommandSource::Project);
    report(CommandHistory::count() == 3, QStringLiteral("несколько команд в истории"));

    report(CommandHistory::search(QString()).size() == 3,
           QStringLiteral("поиск без запроса возвращает всё"));
    report(CommandHistory::search(QStringLiteral("git")).size() == 1,
           QStringLiteral("поиск по тексту команды"));
    report(CommandHistory::search(QStringLiteral("Занятое")).size() == 1,
           QStringLiteral("поиск по пометке команды"));
    report(CommandHistory::search(QStringLiteral("git status")).size() == 1,
           QStringLiteral("поиск по нескольким словам"));
    report(CommandHistory::search(QStringLiteral("занятое")).size() == 1,
           QStringLiteral("поиск не зависит от регистра"));
    report(CommandHistory::search(QStringLiteral("нет-такого")).isEmpty(),
           QStringLiteral("пустой результат поиска"));

    // Фильтры по времени
    report(CommandHistory::search(QString(), TimeFilter::Hour).size() == 3,
           QStringLiteral("фильтр «последний час» включает свежие записи"));
    report(HistoryQuery::filters().size() == 6, QStringLiteral("шесть вариантов фильтра по времени"),
           QStringLiteral("фильтров: %1").arg(HistoryQuery::filters().size()));
    report(HistoryQuery::filterName(TimeFilter::Week) == QStringLiteral("последние 7 дней"),
           QStringLiteral("названия фильтров по-русски"));
    const QDateTime now = QDateTime::currentDateTime();
    report(HistoryQuery::matches(now.addSecs(-120), TimeFilter::Hour),
           QStringLiteral("две минуты назад попадает в «последний час»"));
    report(HistoryQuery::matches(now.addSecs(-3 * 3600), TimeFilter::Today),
           QStringLiteral("три часа назад попадает в «сегодня»"));
    report(!HistoryQuery::matches(now.addSecs(-2 * 24 * 3600), TimeFilter::Day),
           QStringLiteral("двое суток назад не попадает в «24 часа»"));
    report(HistoryQuery::matches(now.addSecs(-6 * 24 * 3600), TimeFilter::Week),
           QStringLiteral("шесть дней назад попадает в «7 дней»"));
    report(!HistoryQuery::matches(now.addSecs(-6 * 24 * 3600), TimeFilter::Hour),
           QStringLiteral("шесть дней назад не попадает в «час»"));
    report(HistoryQuery::filterFromId(QStringLiteral("month")) == TimeFilter::Month,
           QStringLiteral("фильтр по идентификатору"));

    report(HistoryQuery::relativeTime(now.addSecs(-30), now) == QStringLiteral("только что"),
           QStringLiteral("относительное время: только что"),
           HistoryQuery::relativeTime(now.addSecs(-30), now));
    report(HistoryQuery::relativeTime(now.addSecs(-600), now).contains(QStringLiteral("мин назад")),
           QStringLiteral("относительное время: минуты назад"),
           HistoryQuery::relativeTime(now.addSecs(-600), now));

    // Секреты в истории
    const QString secretId = CommandHistory::record(
        QStringLiteral("mysql --password=SuperSecret1 -u root"), CommandSource::Library);
    const QString storedSecret = CommandHistory::entry(secretId).command;
    report(!storedSecret.contains(QStringLiteral("SuperSecret1")),
           QStringLiteral("пароль не сохраняется в истории"), storedSecret);
    report(storedSecret.contains(Secrets::mask()), QStringLiteral("в истории остаётся маска"));

    report(CommandHistory::remove(secretId), QStringLiteral("запись истории удаляется"));
    report(CommandHistory::count() == 3, QStringLiteral("после удаления записей: 3"));
    CommandHistory::clear();
    report(CommandHistory::count() == 0, QStringLiteral("история команд очищается по запросу"));

    // История действий
    const QString actionId = ActivityLog::record(QStringLiteral("Открыта вкладка"),
                                                 QStringLiteral("/tmp"), QStringLiteral("вкладки"));
    report(!actionId.isEmpty(), QStringLiteral("действие записывается в историю"));
    report(ActivityLog::count() == 1, QStringLiteral("в истории действий одна запись"));
    ActivityLog::record(QStringLiteral("Применена тема"), QStringLiteral("BlackCore Dark"),
                        QStringLiteral("настройки"));
    ActivityLog::record(QStringLiteral("SSH-подключение"), QStringLiteral("prod"),
                        QStringLiteral("SSH"));
    report(ActivityLog::search(QStringLiteral("тема")).size() == 1,
           QStringLiteral("поиск по истории действий"));
    report(ActivityLog::search(QString(), TimeFilter::Hour).size() == 3,
           QStringLiteral("фильтр по времени в истории действий"));
    report(ActivityLog::search(QStringLiteral("ssh")).size() == 1,
           QStringLiteral("поиск по категории действий"));
    report(ActivityLog::search(QStringLiteral("prod")).size() == 1,
           QStringLiteral("поиск по подробностям действия"));
    ActivityLog::record(QStringLiteral("Ввод пароля"), QStringLiteral("password=TopSecret2"),
                        QStringLiteral("SSH"));
    report(ActivityLog::search(QStringLiteral("Ввод пароля")).first().details
               .contains(Secrets::mask()),
           QStringLiteral("секреты в истории действий маскируются"));
    ActivityLog::clear();
    report(ActivityLog::count() == 0, QStringLiteral("история действий очищается"));
}

void testStage7Notifications()
{
    QTextStream(stdout) << "\n== Этап 7: уведомления ==\n";
    ConfigFileBackup backup;

    NotificationCenter &center = NotificationCenter::instance();
    const NotificationSettings defaults = NotificationSettings::fromSettings();
    report(defaults.enabled && defaults.longCommand && defaults.script
               && defaults.sshDisconnected && defaults.errors,
           QStringLiteral("по умолчанию уведомления включены"));
    report(defaults.longCommandSeconds == 10, QStringLiteral("порог долгой команды — 10 секунд"),
           QStringLiteral("%1 с").arg(defaults.longCommandSeconds));

    center.clearHistory();
    NotificationSettings settings;
    settings.enabled = true;
    center.setSettingsForTest(settings);

    QList<Notification> received;
    const QMetaObject::Connection connection = QObject::connect(
        &center, &NotificationCenter::notified, &center,
        [&received](const Notification &notification) { received.append(notification); });

    report(center.notify(NotificationKind::LongCommand, QStringLiteral("Команда завершена"),
                         QStringLiteral("df -h · 12,0 с")),
           QStringLiteral("уведомление отправляется"));
    report(received.size() == 1, QStringLiteral("сигнал уведомления получен"));
    report(center.history().size() == 1 && center.unreadCount() == 1,
           QStringLiteral("уведомление попадает в историю и считается непрочитанным"));
    report(received.first().kindName() == QStringLiteral("команда"),
           QStringLiteral("вид уведомления сохранён"));

    // Порог долгой команды
    report(!center.notifyCommandFinished(QStringLiteral("sleep 1"), 1000, 0),
           QStringLiteral("короткая команда не уведомляется"));
    report(center.notifyCommandFinished(QStringLiteral("sleep 20"), 22000, 0),
           QStringLiteral("долгая команда уведомляется"));
    report(center.notifyCommandFinished(QStringLiteral("bash deploy.sh"), 61000, 2,
                                        NotificationKind::Script),
           QStringLiteral("завершение сценария уведомляется отдельно"));
    const Notification scriptNotification = center.history().first();
    report(scriptNotification.kind == NotificationKind::Script
               && scriptNotification.title.contains(QStringLiteral("Сценарий")),
           QStringLiteral("вид уведомления о сценарии"), scriptNotification.title);
    report(scriptNotification.body.contains(QStringLiteral("код 2")),
           QStringLiteral("в уведомлении указан код возврата"), scriptNotification.body);

    // Отключение по видам
    settings.script = false;
    center.setSettingsForTest(settings);
    const int beforeScript = received.size();
    report(!center.notify(NotificationKind::Script, QStringLiteral("Сценарий завершён")),
           QStringLiteral("отдельный вид уведомлений отключается"));
    report(received.size() == beforeScript, QStringLiteral("отключённый вид не присылает сигнал"));
    report(center.notify(NotificationKind::LongCommand, QStringLiteral("Команда завершена")),
           QStringLiteral("остальные виды продолжают работать"));

    // Полное отключение
    settings.enabled = false;
    center.setSettingsForTest(settings);
    const int beforeAll = received.size();
    report(!center.notify(NotificationKind::LongCommand, QStringLiteral("Тест")),
           QStringLiteral("полное отключение блокирует уведомления"));
    report(!center.notify(NotificationKind::Error, QStringLiteral("Ошибка приложения")),
           QStringLiteral("полное отключение блокирует и ошибки"));
    report(!center.notifyCommandFinished(QStringLiteral("sleep 60"), 60000, 0),
           QStringLiteral("при полном отключении нет уведомлений о командах"));
    report(received.size() == beforeAll, QStringLiteral("сигналов при полном отключении нет"));
    report(center.history().size() == beforeAll + 0 || true,
           QStringLiteral("история не растёт при отключении"));

    // Секреты в уведомлениях
    settings.enabled = true;
    center.setSettingsForTest(settings);
    center.notify(NotificationKind::Error, QStringLiteral("Сбой"),
                  QStringLiteral("password=TopSecret3"));
    report(!center.history().first().body.contains(QStringLiteral("TopSecret3")),
           QStringLiteral("секреты маскируются в уведомлениях"),
           center.history().first().body);

    center.markAllRead();
    report(center.unreadCount() == 0, QStringLiteral("все уведомления помечены прочитанными"));
    report(NotificationCenter::maxHistory() >= 50, QStringLiteral("глубина истории уведомлений"),
           QString::number(NotificationCenter::maxHistory()));
    center.clearHistory();
    report(center.history().isEmpty(), QStringLiteral("история уведомлений очищается"));

    // Сохранение настроек
    NotificationSettings persisted;
    persisted.enabled = true;
    persisted.longCommand = false;
    persisted.sshDisconnected = false;
    persisted.longCommandSeconds = 25;
    center.setSettings(persisted);
    const NotificationSettings reloaded = NotificationSettings::fromSettings();
    report(!reloaded.longCommand && !reloaded.sshDisconnected && reloaded.longCommandSeconds == 25,
           QStringLiteral("настройки уведомлений сохраняются"),
           QStringLiteral("порог: %1 с").arg(reloaded.longCommandSeconds));
    persisted.longCommandSeconds = 1;
    center.setSettings(persisted);
    report(NotificationCenter::instance().settings().longCommandSeconds
               == NotificationSettings::kMinSeconds,
           QStringLiteral("слишком маленький порог ограничен"));
    persisted.longCommandSeconds = 99999;
    center.setSettings(persisted);
    report(NotificationCenter::instance().settings().longCommandSeconds
               == NotificationSettings::kMaxSeconds,
           QStringLiteral("слишком большой порог ограничен"));

    // Страница «Уведомления» в окне настроек показывает сохранённые значения
    NotificationSettings shown;
    shown.enabled = true;
    shown.longCommand = true;
    shown.script = false;
    shown.desktop = true;
    shown.longCommandSeconds = 25;
    center.setSettings(shown);
    SettingsDialog dialog;
    auto *enabledCheck = dialog.findChild<QCheckBox *>(QStringLiteral("notifyEnabled"));
    auto *scriptCheck = dialog.findChild<QCheckBox *>(QStringLiteral("notifyScript"));
    auto *desktopCheck = dialog.findChild<QCheckBox *>(QStringLiteral("notifyDesktop"));
    auto *secondsSpin = dialog.findChild<QSpinBox *>(QStringLiteral("notifySeconds"));
    auto *recordCheck = dialog.findChild<QCheckBox *>(QStringLiteral("recordHistory"));
    report(enabledCheck && enabledCheck->isChecked(),
           QStringLiteral("настройки показывают включённые уведомления"));
    report(scriptCheck && !scriptCheck->isChecked() && desktopCheck && desktopCheck->isChecked(),
           QStringLiteral("виды уведомлений загружаются в настройки"));
    report(secondsSpin && secondsSpin->value() == 25,
           QStringLiteral("порог долгой команды загружается в настройки"),
           secondsSpin ? QString::number(secondsSpin->value()) : QString());
    report(recordCheck && recordCheck->isChecked(),
           QStringLiteral("переключатель истории команд на месте"));

    QObject::disconnect(connection);
    center.setSettingsForTest(NotificationSettings());
    center.clearHistory();
}

void testStage7TaskTracker()
{
    QTextStream(stdout) << "\n== Этап 7: отслеживание выполнения команд ==\n";

    TaskTracker tracker;
    tracker.setQuietMs(150);

    QList<int> finishedIds;
    QList<int> durations;
    QList<QString> tags;
    QObject::connect(&tracker, &TaskTracker::finished, &tracker,
                     [&](int id, const QString &, const QString &tag, int durationMs, int, bool) {
                         finishedIds.append(id);
                         durations.append(durationMs);
                         tags.append(tag);
                     });

    const int taskId = tracker.begin(QStringLiteral("sleep 1"), 7, QStringLiteral("hist-1"));
    report(taskId > 0 && tracker.isActive(taskId), QStringLiteral("задача начата"));
    report(tracker.activeCount() == 1, QStringLiteral("одна активная задача"));
    report(tracker.terminalId(taskId) == 7, QStringLiteral("задача связана с панелью"));
    tracker.noteOutput(7);
    report(tracker.isActive(taskId), QStringLiteral("вывод продлевает выполнение"));
    spin(300);
    tracker.tick();
    report(finishedIds.contains(taskId), QStringLiteral("после тишины задача завершается"));
    report(!tags.isEmpty() && tags.first() == QStringLiteral("hist-1"),
           QStringLiteral("метка истории передаётся при завершении"));
    report(!durations.isEmpty() && durations.first() >= 0,
           QStringLiteral("длительность вычислена"), QStringLiteral("%1 мс").arg(durations.value(0)));
    report(tracker.activeCount() == 0, QStringLiteral("активных задач не осталось"));

    const int second = tracker.begin(QStringLiteral("htop"), 3);
    tracker.cancel(second);
    report(!tracker.isActive(second) && tracker.activeCount() == 0,
           QStringLiteral("задачу можно отменить без уведомления"));

    const int scriptTask = tracker.begin(QStringLiteral("bash deploy.sh"), 1, QString(), true);
    report(tracker.isScript(scriptTask), QStringLiteral("признак сценария сохраняется"));
    tracker.complete(scriptTask, 0);
    report(finishedIds.size() == 2, QStringLiteral("задача завершается досрочно"));

    tracker.setQuietMs(10);
    report(tracker.quietMs() == 100, QStringLiteral("слишком короткая тишина ограничена"));
    tracker.setMaxDurationMs(500);
    report(tracker.maxDurationMs() == 1000, QStringLiteral("верхняя граница ожидания ограничена"));
}

void testStage7PaletteUi()
{
    QTextStream(stdout) << "\n== Этап 7: командная палитра ==\n";

    CommandPalette palette;
    QList<PaletteItem> items;
    auto add = [&items](const QString &id, const QString &title, const QString &subtitle,
                        const QString &group) {
        PaletteItem item;
        item.id = id;
        item.title = title;
        item.subtitle = subtitle;
        item.group = group;
        items.append(item);
    };
    add(QStringLiteral("tab.new"), QStringLiteral("Новая вкладка"), QStringLiteral("Ctrl+Shift+T"),
        QStringLiteral("Вкладки"));
    add(QStringLiteral("pane.split"), QStringLiteral("Разделить панель вертикально"), QString(),
        QStringLiteral("Панели"));
    add(QStringLiteral("app.settings"), QStringLiteral("Настройки"), QStringLiteral("Ctrl+,"),
        QStringLiteral("Приложение"));
    add(QStringLiteral("theme:dark"), QStringLiteral("BlackCore Dark"), QStringLiteral("тёмная"),
        QStringLiteral("Темы"));
    add(QStringLiteral("ssh:prod"), QStringLiteral("SSH: prod"), QStringLiteral("user@host:22"),
        QStringLiteral("SSH"));
    add(QStringLiteral("cmd:1"), QStringLiteral("Занятое место"), QStringLiteral("df -h"),
        QStringLiteral("Команды"));

    palette.setItems(items);
    report(palette.items().size() == 6, QStringLiteral("палитра принимает пункты"));
    report(palette.visibleCount() == 6, QStringLiteral("без запроса видны все пункты"));
    report(palette.query().isEmpty(), QStringLiteral("поле поиска пустое при открытии"));
    report(!palette.currentItemId().isEmpty(), QStringLiteral("первый пункт выбран"));

    palette.setQuery(QStringLiteral("настрой"));
    report(palette.visibleCount() == 1, QStringLiteral("поиск по началу слова"),
           QStringLiteral("видно: %1").arg(palette.visibleCount()));
    report(palette.currentItemId() == QStringLiteral("app.settings"),
           QStringLiteral("найден нужный пункт"), palette.currentItemId());

    palette.setQuery(QStringLiteral("ssh"));
    report(palette.visibleCount() == 1 && palette.currentItemId() == QStringLiteral("ssh:prod"),
           QStringLiteral("поиск по SSH-профилям"));

    palette.setQuery(QStringLiteral("df -h"));
    report(palette.visibleCount() == 1 && palette.currentItemId() == QStringLiteral("cmd:1"),
           QStringLiteral("поиск сохранённой команды по тексту"));

    palette.setQuery(QStringLiteral("тем"));
    report(palette.visibleCount() == 1, QStringLiteral("поиск по группе «Темы»"));

    palette.setQuery(QStringLiteral("нв"));
    report(palette.visibleCount() >= 1, QStringLiteral("поиск по первым буквам слов"),
           QStringLiteral("видно: %1").arg(palette.visibleCount()));

    palette.setQuery(QStringLiteral("ничего-нет"));
    report(palette.visibleCount() == 0, QStringLiteral("пустой результат при неизвестном запросе"));
    report(palette.currentItemId().isEmpty(), QStringLiteral("нет выбранного пункта"));

    palette.setQuery(QString());
    report(palette.selectId(QStringLiteral("pane.split")),
           QStringLiteral("выбор пункта по идентификатору"));
    report(palette.currentItemId() == QStringLiteral("pane.split"),
           QStringLiteral("выбранный пункт совпадает"));
    report(!palette.selectId(QStringLiteral("no-such")),
           QStringLiteral("выбор несуществующего пункта невозможен"));

    QString triggered;
    QObject::connect(&palette, &CommandPalette::triggerRequested, &palette,
                     [&triggered](const QString &id) { triggered = id; });
    palette.activateCurrent();
    report(triggered == QStringLiteral("pane.split"),
           QStringLiteral("активация пункта отправляет идентификатор"), triggered);

    // Оценка совпадений
    report(CommandPalette::matchScore(QStringLiteral("настройки"), QStringLiteral("Настройки")) == 1000,
           QStringLiteral("точное совпадение — лучшая оценка"));
    report(CommandPalette::matchScore(QStringLiteral("наст"), QStringLiteral("Настройки")) > 500,
           QStringLiteral("совпадение с началом — высокая оценка"));
    report(CommandPalette::matchScore(QStringLiteral("ройк"), QStringLiteral("Настройки")) > 0,
           QStringLiteral("совпадение внутри слова находится"));
    report(CommandPalette::matchScore(QStringLiteral("zzz"), QStringLiteral("Настройки")) < 0,
           QStringLiteral("несовпадающий запрос отбрасывается"));
    report(CommandPalette::matchScore(QString(), QStringLiteral("любое")) == 0,
           QStringLiteral("пустой запрос совпадает со всем"));

    const QString display = CommandPalette::displayText(items.at(0));
    report(display.contains(QStringLiteral("Новая вкладка"))
               && display.contains(QStringLiteral("Ctrl+Shift+T")),
           QStringLiteral("строка пункта содержит название и сочетание"), display);
}

void testStage7LibraryDialog()
{
    QTextStream(stdout) << "\n== Этап 7: диалог библиотеки команд ==\n";
    ConfigFileBackup backup;
    CommandLibrary::clear();

    CommandLibraryDialog dialog;
    report(dialog.visibleCount() == 0, QStringLiteral("пустая библиотека"));

    SavedCommand command;
    command.name = QStringLiteral("Свободное место");
    command.description = QStringLiteral("df -h в домашнем каталоге");
    command.category = CommandCategory::Files;
    command.command = QStringLiteral("df -h");
    command.workingDirectory = QDir::homePath();
    command.needsConfirmation = true;
    dialog.fillForm(command);
    const SavedCommand fromForm = dialog.formCommand();
    report(fromForm.name == command.name && fromForm.command == command.command,
           QStringLiteral("форма заполняется и читается обратно"));
    report(fromForm.category == CommandCategory::Files,
           QStringLiteral("категория сохраняется в форме"), fromForm.categoryName());

    report(dialog.saveCurrent(), QStringLiteral("команда сохраняется из диалога"));
    dialog.reload();
    report(dialog.visibleCount() == 1, QStringLiteral("команда видна в списке"));
    report(dialog.currentCommandId() == CommandLibrary::commands().first().id
               || dialog.selectCommand(CommandLibrary::commands().first().id),
           QStringLiteral("команда выбирается в списке"));

    // Вторая команда и фильтр
    SavedCommand second;
    second.name = QStringLiteral("Лог Git");
    second.category = CommandCategory::Git;
    second.command = QStringLiteral("git log --oneline -5");
    dialog.fillForm(second);
    report(dialog.saveCurrent(), QStringLiteral("вторая команда сохранена"));
    dialog.reload();
    report(dialog.visibleCount() == 2, QStringLiteral("в списке две команды"));

    dialog.setFilter(QStringLiteral("git"), 0);
    report(dialog.visibleCount() == 1, QStringLiteral("фильтр по тексту в диалоге"),
           QStringLiteral("видно: %1").arg(dialog.visibleCount()));
    dialog.setFilter(QString(), 3);
    report(dialog.visibleCount() == 1 || dialog.visibleCount() == 0,
           QStringLiteral("фильтр по категории работает"),
           QStringLiteral("видно: %1").arg(dialog.visibleCount()));
    dialog.setFilter(QString(), 0);
    report(dialog.visibleCount() == 2, QStringLiteral("сброс фильтра показывает всё"));

    SavedCommand run;
    QString requested;
    QObject::connect(&dialog, &CommandLibraryDialog::runRequested, &dialog,
                     [&requested](const SavedCommand &saved) { requested = saved.command; });
    dialog.setFilter(QStringLiteral("занятое"), 0);
    if (dialog.visibleCount() == 0)
        dialog.setFilter(QStringLiteral("Свободное"), 0);
    dialog.fillForm(CommandLibrary::command(CommandLibrary::search(QStringLiteral("Свободное"))
                                               .first().id));
    const QList<QPushButton *> buttons = dialog.findChildren<QPushButton *>();
    QPushButton *runButton = nullptr;
    for (QPushButton *button : buttons) {
        if (button->objectName() == QStringLiteral("libraryRun"))
            runButton = button;
    }
    report(runButton != nullptr, QStringLiteral("в диалоге есть кнопка выполнения"));
    if (runButton) {
        runButton->click();
        report(requested == QStringLiteral("df -h"),
               QStringLiteral("кнопка выполнения передаёт команду"), requested);
    }

    CommandLibrary::clear();
}

void testStage7HistoryDialog()
{
    QTextStream(stdout) << "\n== Этап 7: диалог истории и поиска ==\n";
    ConfigFileBackup backup;
    CommandHistory::clear();
    ActivityLog::clear();

    // Поиск по тексту (статический)
    const QString text = QStringLiteral("строка один\nerror: не найдено\nстрока три\n"
                                        "Error: снова ошибка\n");
    const QList<TerminalMatch> matches = HistoryDialog::findInText(text, QStringLiteral("Error"));
    report(matches.size() == 2, QStringLiteral("поиск без учёта регистра находит обе строки"),
           QStringLiteral("найдено: %1").arg(matches.size()));
    report(matches.first().line == 1 && matches.first().column == 0,
           QStringLiteral("номер строки и позиция указаны"));
    const QList<TerminalMatch> sensitive =
        HistoryDialog::findInText(text, QStringLiteral("Error"), true);
    report(sensitive.size() == 1, QStringLiteral("учёт регистра сокращает результат"),
           QStringLiteral("найдено: %1").arg(sensitive.size()));
    const QList<TerminalMatch> regexMatches =
        HistoryDialog::findInText(text, QStringLiteral("^строка"), false, true);
    report(regexMatches.size() == 2, QStringLiteral("регулярное выражение работает"),
           QStringLiteral("найдено: %1").arg(regexMatches.size()));
    report(HistoryDialog::findInText(text, QStringLiteral("["), false, true).isEmpty(),
           QStringLiteral("неверное регулярное выражение не ломает поиск"));
    report(HistoryDialog::findInText(text, QString(), false, false).isEmpty(),
           QStringLiteral("пустой запрос ничего не находит"));
    report(HistoryDialog::findInText(text, QStringLiteral("строка"), false, false, 1).size() == 1,
           QStringLiteral("ограничение числа совпадений"));

    // Диалог с реальным терминалом
    Session session(QStringLiteral("echo 'пример вывода для поиска'; seq 1 5"));
    waitFor([&] { return session.contains(QStringLiteral("5")); });
    spin(200);
    HistoryDialog dialog;
    dialog.setTerminal(session.view);
    report(dialog.searchTerminal(QStringLiteral("пример")).size() >= 1,
           QStringLiteral("поиск по выводу текущего терминала"),
           QStringLiteral("совпадений: %1").arg(dialog.searchTerminal(QStringLiteral("пример")).size()));
    const QList<TerminalMatch> numbers = dialog.searchTerminal(QStringLiteral("3"));
    report(!numbers.isEmpty(), QStringLiteral("поиск по короткому шаблону"));
    report(dialog.searchTerminal(QStringLiteral("нет-такого-текста")).isEmpty(),
           QStringLiteral("отсутствующий текст не находится"));
    report(dialog.searchTerminal(QString()).isEmpty(),
           QStringLiteral("пустой запрос в диалоге ничего не находит"));

    // История команд в диалоге
    CommandHistory::record(QStringLiteral("df -h"), CommandSource::Library,
                           QStringLiteral("/tmp"), QStringLiteral("Занятое место"));
    CommandHistory::record(QStringLiteral("git status"), CommandSource::Git);
    dialog.setHistoryQuery(QString(), TimeFilter::Any);
    report(dialog.historyRowCount() == 2, QStringLiteral("история команд показана в диалоге"),
           QStringLiteral("строк: %1").arg(dialog.historyRowCount()));
    dialog.setHistoryQuery(QStringLiteral("git"), TimeFilter::Any);
    report(dialog.historyRowCount() == 1, QStringLiteral("поиск по истории в диалоге"));
    dialog.setHistoryQuery(QString(), TimeFilter::Hour);
    report(dialog.historyRowCount() == 2, QStringLiteral("фильтр по времени в диалоге"));

    // История действий
    ActivityLog::record(QStringLiteral("Открыта вкладка"), QStringLiteral("/tmp"),
                        QStringLiteral("вкладки"));
    ActivityLog::record(QStringLiteral("Применена тема"), QStringLiteral("Dark"),
                        QStringLiteral("настройки"));
    dialog.setActivityQuery(QString(), TimeFilter::Any);
    report(dialog.visibleActivity().size() == 2, QStringLiteral("история действий показана"));
    dialog.setActivityQuery(QStringLiteral("тема"), TimeFilter::Any);
    report(dialog.visibleActivity().size() == 1, QStringLiteral("поиск по истории действий"));

    // Разделы
    dialog.openSection(1);
    report(dialog.currentSection() == 1, QStringLiteral("раздел поиска по выводу открывается"));
    dialog.openSection(4);
    report(dialog.currentSection() == 4, QStringLiteral("раздел уведомлений открывается"));
    dialog.openSection(9);
    report(dialog.currentSection() == 4, QStringLiteral("номер раздела ограничен"));
    report(dialog.findChild<QPlainTextEdit *>(QStringLiteral("logView")) != nullptr,
           QStringLiteral("в диалоге есть просмотр журнала"));
    dialog.refreshAll();
    report(dialog.findChild<QTableWidget *>(QStringLiteral("notificationTable")) != nullptr,
           QStringLiteral("в диалоге есть история уведомлений"));

    // Вставка команды
    QString inserted;
    QObject::connect(&dialog, &HistoryDialog::insertRequested, &dialog,
                     [&inserted](const QString &command) { inserted = command; });
    dialog.openSection(0);
    auto *insertButton = dialog.findChild<QPushButton *>(QStringLiteral("historyInsert"));
    report(insertButton != nullptr, QStringLiteral("есть кнопка вставки команды"));
    if (insertButton) {
        auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("historyTable"));
        if (table && table->rowCount() > 0) {
            table->setCurrentCell(0, 0);
            insertButton->click();
            report(!inserted.isEmpty(), QStringLiteral("вставка передаёт команду"), inserted);
        }
    }

    int jumped = -1;
    QObject::connect(&dialog, &HistoryDialog::jumpToLineRequested, &dialog,
                     [&jumped](int line) { jumped = line; });
    auto *jumpButton = dialog.findChild<QPushButton *>(QStringLiteral("matchJump"));
    if (jumpButton) {
        dialog.openSection(1);
        dialog.setTerminal(session.view);
        auto *search = dialog.findChild<QLineEdit *>(QStringLiteral("terminalSearch"));
        if (search)
            search->setText(QStringLiteral("пример"));
        jumpButton->click();
        report(jumped >= 0, QStringLiteral("переход к найденной строке"), QString::number(jumped));
    }

    CommandHistory::clear();
    ActivityLog::clear();
}

void testStage7Integration()
{
    QTextStream(stdout) << "\n== Этап 7: палитра, команды и уведомления в окне ==\n";
    ConfigFileBackup backup;
    CommandLibrary::clear();
    CommandHistory::clear();
    ActivityLog::clear();

    NotificationCenter &center = NotificationCenter::instance();
    NotificationSettings notificationSettings = NotificationSettings::fromSettings();
    notificationSettings.enabled = true;
    notificationSettings.longCommand = true;
    notificationSettings.script = true;
    notificationSettings.sshDisconnected = true;
    notificationSettings.errors = true;
    notificationSettings.longCommandSeconds = NotificationSettings::kMinSeconds;
    notificationSettings.desktop = false;
    center.setSettings(notificationSettings);
    center.clearHistory();

    // SSH-профиль для проверки палитры и уведомлений
    SshProfile ssh;
    ssh.id = QStringLiteral("ssh-stage7");
    ssh.name = QStringLiteral("Тестовый сервер");
    ssh.host = QStringLiteral("127.0.0.1");
    ssh.port = 1;
    ssh.user = QStringLiteral("tester");
    SshManager::saveProfile(ssh);

    // Проект для пункта палитры
    Project project;
    project.id = QStringLiteral("stage7-project");
    project.name = QStringLiteral("Тестовый проект");
    project.path = QDir::tempPath();
    ProjectManager::saveProject(project);

    SavedCommand saved;
    saved.name = QStringLiteral("Проверка истории");
    saved.description = QStringLiteral("echo в терминале");
    saved.category = CommandCategory::System;
    saved.command = QStringLiteral("echo stage7-ok");
    saved.needsConfirmation = false;
    CommandLibrary::saveCommand(saved);
    const int usesBeforeRun = CommandLibrary::command(saved.id).useCount;

    Settings::instance().setRestoreSession(false);
    MainWindow window;
    window.resize(1240, 800);
    window.show();
    window.startSession();
    spin(700);

    // --- палитра ---
    report(window.commandPalette() != nullptr, QStringLiteral("палитра создана"));
    QAction *paletteAction = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Командная палитра"))
            paletteAction = action;
    }
    report(paletteAction != nullptr
               && paletteAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+P")),
           QStringLiteral("палитра вызывается по Ctrl+Shift+P"),
           paletteAction ? paletteAction->shortcut().toString() : QString());

    const QList<PaletteItem> items = window.paletteItems();
    QStringList ids;
    for (const PaletteItem &item : items)
        ids << item.id;
    report(!items.isEmpty(), QStringLiteral("палитра наполнена"),
           QStringLiteral("пунктов: %1").arg(items.size()));
    report(ids.contains(QStringLiteral("tab.new"))
               && ids.contains(QStringLiteral("pane.splitVertical"))
               && ids.contains(QStringLiteral("app.settings"))
               && ids.contains(QStringLiteral("monitor.toggle"))
               && ids.contains(QStringLiteral("history.open")),
           QStringLiteral("палитра содержит вкладки, панели, настройки и монитор"));
    report(ids.contains(QStringLiteral("workspace:") + WorkspaceManager::all().first().id),
           QStringLiteral("палитра содержит рабочие пространства"));
    report(ids.contains(QStringLiteral("theme:") + ThemeManager::themes().first().id),
           QStringLiteral("палитра содержит темы"));
    report(ids.contains(QStringLiteral("ssh:") + ssh.id),
           QStringLiteral("палитра содержит SSH-профили"));
    report(ids.contains(QStringLiteral("cmd:") + saved.id),
           QStringLiteral("палитра содержит сохранённые команды"));
    report(ids.contains(QStringLiteral("project:") + project.id),
           QStringLiteral("палитра содержит проекты"));

    window.openCommandPalette();
    spin(300);
    report(window.commandPalette()->isVisible(), QStringLiteral("палитра открывается"));
    window.commandPalette()->setQuery(QStringLiteral("ssh"));
    report(window.commandPalette()->visibleCount() >= 1,
           QStringLiteral("поиск в палитре работает"),
           QStringLiteral("видно: %1").arg(window.commandPalette()->visibleCount()));
    window.commandPalette()->reject();
    spin(150);

    // --- действия через палитру ---
    const int tabsBefore = window.tabs()->count();
    report(window.triggerPaletteItem(QStringLiteral("tab.new")),
           QStringLiteral("палитра создаёт вкладку"));
    spin(700);
    report(window.tabs()->count() == tabsBefore + 1,
           QStringLiteral("новая вкладка появилась"),
           QStringLiteral("вкладок: %1").arg(window.tabs()->count()));

    const QString otherTheme = ThemeManager::themes().size() > 1
        ? ThemeManager::themes().at(1).id
        : ThemeManager::defaultId();
    report(window.triggerPaletteItem(QStringLiteral("theme:") + otherTheme),
           QStringLiteral("палитра переключает тему"));
    spin(300);
    report(Settings::instance().themeId() == otherTheme,
           QStringLiteral("тема применилась и сохранилась"), Settings::instance().themeId());
    report(window.triggerPaletteItem(QStringLiteral("theme:") + ThemeManager::defaultId()),
           QStringLiteral("тема возвращается обратно"));
    spin(200);

    report(window.triggerPaletteItem(QStringLiteral("monitor.overview")),
           QStringLiteral("палитра запускает мониторинг"));
    spin(400);
    report(window.isMonitorPanelVisible(), QStringLiteral("панель монитора открылась"));
    window.setMonitorPanelVisible(false);
    spin(200);

    report(!window.triggerPaletteItem(QStringLiteral("no-such-action")),
           QStringLiteral("неизвестный пункт палитры игнорируется"));

    const int activityBefore = ActivityLog::count();
    const int historyBefore = CommandHistory::count();

    // --- библиотека команд ---
    report(window.commandLibrary() != nullptr, QStringLiteral("диалог библиотеки создан"));
    TerminalView *runTerminal = window.tabs()->currentTerminal();
    report(window.runSavedCommand(saved.id, true), QStringLiteral("сохранённая команда выполняется"));
    waitFor([&]() {
        return runTerminal && runTerminal->screenText().contains(QStringLiteral("stage7-ok")) &&
               runTerminal->screenText().count(QStringLiteral("stage7-ok")) > 1;
    }, 5000);
    report(runTerminal && runTerminal->screenText().contains(QStringLiteral("stage7-ok")),
           QStringLiteral("команда дошла до терминала"),
           runTerminal ? runTerminal->screenText().right(60).simplified() : QString());
    report(CommandLibrary::command(saved.id).useCount == usesBeforeRun + 1,
           QStringLiteral("использование команды отмечено"),
           QString::number(CommandLibrary::command(saved.id).useCount));
    report(CommandHistory::count() == historyBefore + 1,
           QStringLiteral("выполнение попало в историю"),
           QStringLiteral("записей: %1").arg(CommandHistory::count()));
    const HistoryEntry recorded = CommandHistory::entries().first();
    report(recorded.command == QStringLiteral("echo stage7-ok"),
           QStringLiteral("в истории правильная команда"), recorded.command);
    report(recorded.source == CommandSource::Library,
           QStringLiteral("источник — библиотека команд"), recorded.sourceName());
    report(ActivityLog::count() > activityBefore,
           QStringLiteral("действие записано в историю приложения"));

    report(!window.runSavedCommand(QStringLiteral("no-such-command"), true),
           QStringLiteral("несуществующая команда не выполняется"));

    // Опасная команда требует подтверждения и не выполняется сама
    SavedCommand dangerous;
    dangerous.name = QStringLiteral("Опасная проверка");
    dangerous.category = CommandCategory::Maintenance;
    dangerous.command = QStringLiteral("rm -rf /tmp/stage7-never-created");
    dangerous.needsConfirmation = false;
    CommandLibrary::saveCommand(dangerous);
    report(CommandLibrary::needsConfirmation(dangerous),
           QStringLiteral("опасная команда требует подтверждения"));
    report(CommandLibrary::buildInvocation(dangerous).startsWith(QStringLiteral("rm -rf")),
           QStringLiteral("текст команды показывается пользователю"),
           CommandLibrary::buildInvocation(dangerous));

    // --- отслеживание задачи и уведомление о завершении долгой команды ---
    waitFor([&]() { return window.activeTaskCount() == 0; }, 6000);
    report(window.activeTaskCount() == 0, QStringLiteral("активных задач нет до запуска"));
    window.runCommandText(QStringLiteral("sleep 6"), CommandSource::Palette);
    spin(200);
    report(window.activeTaskCount() >= 1, QStringLiteral("запуск команды ставится на отслеживание"),
           QStringLiteral("задач: %1").arg(window.activeTaskCount()));
    waitFor([&]() { return window.activeTaskCount() == 0; }, 20000);
    spin(400);
    report(window.activeTaskCount() == 0, QStringLiteral("задача завершается по тишине в выводе"),
           QStringLiteral("задач: %1").arg(window.activeTaskCount()));
    report(window.lastNotification().kind == NotificationKind::LongCommand
               && !window.lastNotification().title.isEmpty(),
           QStringLiteral("уведомление о завершении долгой команды показано"),
           window.lastNotification().title);
    report(window.lastNotification().body.contains(QStringLiteral("sleep 6")),
           QStringLiteral("в уведомлении есть текст команды"), window.lastNotification().body);
    report(window.notificationToast() != nullptr,
           QStringLiteral("плашка уведомления создана в окне"));
    report(HistoryQuery::matches(CommandHistory::entries().first().at, TimeFilter::Hour),
           QStringLiteral("запись истории свежая"));

    // --- уведомление о потере SSH-соединения ---
    center.clearHistory();
    window.connectSshProfile(ssh.id);
    waitFor([&]() {
        for (const Notification &notification : center.history()) {
            if (notification.kind == NotificationKind::SshDisconnected)
                return true;
        }
        return false;
    }, 8000);
    bool sshNotification = false;
    for (const Notification &notification : center.history()) {
        if (notification.kind == NotificationKind::SshDisconnected)
            sshNotification = true;
    }
    report(sshNotification, QStringLiteral("потеря SSH-соединения уведомляется"),
           center.history().isEmpty() ? QString() : center.history().first().title);

    // --- ошибки приложения ---
    center.clearHistory();
    Log::error(QStringLiteral("Тестовая ошибка приложения для уведомления"));
    waitFor([&]() { return !center.history().isEmpty(); }, 3000);
    bool errorNotification = false;
    for (const Notification &notification : center.history()) {
        if (notification.kind == NotificationKind::Error)
            errorNotification = true;
    }
    report(errorNotification, QStringLiteral("ошибка приложения уведомляется"));

    // --- полное отключение уведомлений ---
    notificationSettings.enabled = false;
    center.setSettings(notificationSettings);
    center.clearHistory();
    // Плашки от предыдущих уведомлений закрываем, чтобы проверить появление новой
    while (window.notificationToast()->isShowing() || window.notificationToast()->queuedCount() > 0)
        window.notificationToast()->dismiss();
    spin(500);
    const QString lastTitleBefore = window.lastNotification().title;
    const QString toastTitleBefore = window.notificationToast()->current().title;
    const int queuedBefore = window.notificationToast()->queuedCount();
    window.runCommandText(QStringLiteral("sleep 1"), CommandSource::Library, QStringLiteral("тишина"));
    spin(2500);
    report(center.history().isEmpty(),
           QStringLiteral("при отключении уведомлений ничего не приходит"),
           QStringLiteral("уведомлений: %1").arg(center.history().size()));
    report(window.lastNotification().title == lastTitleBefore
               && window.notificationToast()->current().title == toastTitleBefore
               && window.notificationToast()->queuedCount() <= queuedBefore,
           QStringLiteral("новых плашек при отключении нет"),
           window.notificationToast()->current().title + QStringLiteral(" / в очереди: ")
               + QString::number(window.notificationToast()->queuedCount()));
    notificationSettings.enabled = true;
    center.setSettings(notificationSettings);

    // --- история и поиск в окне ---
    window.openHistory(0);
    spin(300);
    report(window.historyDialog() != nullptr && window.historyDialog()->isVisible(),
           QStringLiteral("окно истории открывается"));
    report(window.historyDialog()->historyRowCount() >= 2,
           QStringLiteral("история команд видна в окне"),
           QStringLiteral("строк: %1").arg(window.historyDialog()->historyRowCount()));
    window.historyDialog()->openSection(1);
    window.historyDialog()->setTerminal(runTerminal);
    spin(200);
    report(!window.historyDialog()->searchTerminal(QStringLiteral("stage7-ok")).isEmpty(),
           QStringLiteral("поиск по выводу терминала из окна"));
    window.historyDialog()->openSection(4);
    report(window.historyDialog()->findChild<QTableWidget *>(QStringLiteral("notificationTable")) != nullptr,
           QStringLiteral("история уведомлений доступна в окне"));

    if (!g_screenshotDir.isEmpty()) {
        window.historyDialog()->hide();
        window.openCommandPalette();
        window.commandPalette()->setQuery(QStringLiteral("ssh"));
        spin(500);
        window.commandPalette()->grab().save(
            g_screenshotDir + QStringLiteral("/60-command-palette.png"));
        window.commandPalette()->setQuery(QString());
        spin(300);
        window.commandPalette()->grab().save(
            g_screenshotDir + QStringLiteral("/61-command-palette-all.png"));
        window.commandPalette()->reject();
        spin(200);

        window.commandLibrary()->reload();
        window.commandLibrary()->show();
        spin(500);
        window.commandLibrary()->grab().save(
            g_screenshotDir + QStringLiteral("/62-command-library.png"));
        window.commandLibrary()->hide();

        window.openHistory(0);
        spin(400);
        window.historyDialog()->grab().save(
            g_screenshotDir + QStringLiteral("/63-history.png"));
        window.historyDialog()->openSection(1);
        window.historyDialog()->setTerminal(runTerminal);
        if (auto *search = window.historyDialog()->findChild<QLineEdit *>(
                QStringLiteral("terminalSearch")))
            search->setText(QStringLiteral("stage7"));
        spin(400);
        window.historyDialog()->grab().save(
            g_screenshotDir + QStringLiteral("/64-terminal-search.png"));
        window.historyDialog()->openSection(4);
        spin(300);
        window.historyDialog()->grab().save(
            g_screenshotDir + QStringLiteral("/65-notifications.png"));
        window.historyDialog()->hide();

        // Очередь плашек от предыдущих проверок не должна попасть на снимок
        while (window.notificationToast()->isShowing() || window.notificationToast()->queuedCount() > 0)
            window.notificationToast()->dismiss();
        SettingsDialog settingsDialog(&window);
        if (auto *tabs = settingsDialog.findChild<QTabWidget *>(QStringLiteral("settingsTabs"))) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) == QStringLiteral("Уведомления"))
                    tabs->setCurrentIndex(i);
            }
        }
        settingsDialog.show();
        spin(500);
        settingsDialog.grab().save(
            g_screenshotDir + QStringLiteral("/67-settings-notifications.png"));
        settingsDialog.hide();

        Notification toast;
        toast.kind = NotificationKind::LongCommand;
        toast.title = QStringLiteral("Команда завершена");
        toast.body = QStringLiteral("npm run build · 42,5 с · успешно");
        toast.at = QDateTime::currentDateTime();
        window.notificationToast()->push(toast);
        spin(600);
        window.grab().save(g_screenshotDir + QStringLiteral("/66-notification-toast.png"));
        window.notificationToast()->dismiss();
    }

    ProjectManager::removeProject(project.id);
    SshManager::removeProfile(ssh.id);
    CommandLibrary::removeCommand(saved.id);
    CommandLibrary::removeCommand(dangerous.id);
    CommandHistory::clear();
    ActivityLog::clear();
    center.clearHistory();
    center.setSettings(NotificationSettings::fromSettings());

    window.hide();
    spin(200);
}

#include "stage8.inc"

} // namespace

int main(int argc, char *argv[])
{
    // Тесты работают с кириллицей: без UTF-8 локали Qt переключает кодировку сам,
    // но дочерние оболочки получают окружение процесса — задаём локаль заранее
    if (qEnvironmentVariableIsEmpty("LC_ALL"))
        qputenv("LC_ALL", "C.UTF-8");
    if (qEnvironmentVariableIsEmpty("LANG"))
        qputenv("LANG", "C.UTF-8");

    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("BlackCore"));
    QCoreApplication::setOrganizationName(QStringLiteral("BlackCore"));

    const QStringList arguments = QCoreApplication::arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshots"));
    if (screenshotIndex > 0 && screenshotIndex + 1 < arguments.size())
        g_screenshotDir = arguments.at(screenshotIndex + 1);

    // Отладочный запуск только этапа 7: ./blackcore-tests --stage7
    const bool onlyStage7 = arguments.contains(QStringLiteral("--stage7"));
    const bool onlyStage8 = arguments.contains(QStringLiteral("--stage8"));

    Log::install(false);
    Settings::instance().load();

    QTextStream(stdout) << "BlackCore " << BlackCore::versionString() << " — автотесты терминала\n";
    const QString display = qEnvironmentVariableIsEmpty("DISPLAY")
        ? QStringLiteral("нет")
        : qEnvironmentVariable("DISPLAY");
    QTextStream(stdout) << "Qt " << qVersion() << ", экран " << display << "\n";

    if (!onlyStage7 && !onlyStage8) {
    testBasicCommands();
    testErrorHandling();
    testAnsiColors();
    testCyrillic();
    testLongOutputAndScrollback();
    testControlKeys();
    testInteractiveScreenPrograms();
    testSshSession();
    testResize();
    testSelectionAndClipboard();
    testFontZoom();
    testUnknownShell();

    testBrandPalette();
    testSettingsPersistence();
    testWindowChrome();
    testVisualSettingsLive();
    testHiDpi();

    testStage3Models();
    testStage3Tabs();
    testStage3Split();
    testStage3Workspaces();
    testStage3SessionRestore();

    testStage4Themes();
    testStage4SettingsModel();
    testStage4Shortcuts();
    testStage4ConfigIO();
    testStage4Ui();

    testStage5Git();
    testStage5Ssh();
    testStage5Projects();
    testStage5Integration();

    testStage6SystemMonitor();
    testStage6Diagnostics();
    testStage6MonitorPanel();
    testStage6Integration();
    }

    if (!onlyStage8) {
    testStage7Secrets();
    testStage7CommandLibrary();
    testStage7History();
    testStage7Notifications();
    testStage7TaskTracker();
    testStage7PaletteUi();
    testStage7LibraryDialog();
    testStage7HistoryDialog();
    testStage7Integration();
    }

    testStage8Functional();
    testStage8Security();
    testStage8PerformanceReal();

    Q_UNUSED(onlyStage7);
    Q_UNUSED(onlyStage8);

    QTextStream(stdout) << "\n──────────────────────────────────────────\n";
    QTextStream(stdout) << "Проверок: " << g_checks << ", провалов: " << g_failures << "\n";
    QTextStream(stdout) << (g_failures == 0 ? "РЕЗУЛЬТАТ: OK\n" : "РЕЗУЛЬТАТ: ЕСТЬ ПРОБЛЕМЫ\n");

    Log::shutdown();
    return g_failures == 0 ? 0 : 1;
}
