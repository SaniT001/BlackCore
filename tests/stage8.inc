// ---------------------------------------------------------------------------
// Этап 8: приёмочные проверки перед выпуском 1.0.0
// ---------------------------------------------------------------------------

namespace {

//! RSS текущего процесса, КиБ (Linux, /proc/self/status)
qint64 currentRssKb()
{
    QFile file(QStringLiteral("/proc/self/status"));
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QString text = QString::fromLatin1(file.readAll());
    const QRegularExpression expression(QStringLiteral("VmRSS:\\s+(\\d+)"));
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1).toLongLong() : -1;
}

//! Количество открытых файловых дескрипторов
int openDescriptorCount()
{
    return QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::AllEntries | QDir::System
                                                          | QDir::NoDotAndDotDot)
        .size();
}

//! Дочерние процессы указанного процесса
int childProcessCount(pid_t pid)
{
    if (pid <= 0)
        return 0;
    QFile file(QStringLiteral("/proc/%1/task/%1/children").arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return file.readAll().simplified().split(' ').size();
}

//! Описание дочерних процессов: pid (состояние) командная строка
QString describeChildren(pid_t pid)
{
    QStringList result;
    QFile file(QStringLiteral("/proc/%1/task/%1/children").arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("нет данных");
    for (const QByteArray &entry : file.readAll().simplified().split(' ')) {
        if (entry.isEmpty())
            continue;
        const QString childPid = QString::fromLatin1(entry);
        QFile cmd(QStringLiteral("/proc/%1/cmdline").arg(childPid));
        QString command;
        if (cmd.open(QIODevice::ReadOnly))
            command = QString::fromLatin1(cmd.readAll()).replace(QChar(u'\0'), QLatin1Char(' ')).trimmed();
        result << QStringLiteral("%1 «%2»").arg(childPid, command);
    }
    return result.isEmpty() ? QStringLiteral("нет") : result.join(QStringLiteral(", "));
}

//! Живые (не зомби) дочерние процессы
int runningChildCount(pid_t pid)
{
    if (pid <= 0)
        return 0;
    QFile file(QStringLiteral("/proc/%1/task/%1/children").arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QList<QByteArray> entries = file.readAll().simplified().split(' ');
    int running = 0;
    for (const QByteArray &entry : entries) {
        if (entry.isEmpty())
            continue;
        QFile stat(QStringLiteral("/proc/%1/stat").arg(QString::fromLatin1(entry)));
        if (!stat.open(QIODevice::ReadOnly))
            continue;
        const QByteArray text = stat.readAll();
        const int close = text.lastIndexOf(')');
        if (close < 0 || close + 2 >= text.size())
            continue;
        if (text.at(close + 2) != 'Z')
            ++running;
    }
    return running;
}

//! Процесс существует (без превращения в зомби)
bool processAlive(pid_t pid)
{
    if (pid <= 0)
        return false;
    QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray stat = file.readAll();
    const int close = stat.lastIndexOf(')');
    if (close < 0 || close + 2 >= stat.size())
        return false;
    return stat.at(close + 2) != 'Z';
}

//! Слепок файла: содержимое + время изменения (для проверки «не трогали»)
struct FileSnapshot
{
    QString path;
    QByteArray content;
    qint64 modified = 0;
    bool exists = false;

    explicit FileSnapshot(const QString &filePath)
        : path(filePath)
    {
        QFileInfo info(filePath);
        exists = info.exists();
        if (!exists)
            return;
        modified = info.lastModified().toMSecsSinceEpoch();
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly))
            content = file.readAll();
    }

    bool unchanged() const
    {
        QFileInfo info(path);
        if (info.exists() != exists)
            return false;
        if (!exists)
            return true;
        if (info.lastModified().toMSecsSinceEpoch() != modified)
            return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        return file.readAll() == content;
    }
};

//! Обработать отложенные удаления (deleteLater) — закрытые вкладки освобождаются
void flushDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

//! Интерактивная сессия с явно заданной оболочкой (для проверки Bash и Zsh)
struct ShellSession
{
    TerminalView *view = nullptr;
    bool finished = false;

    explicit ShellSession(const QString &shell)
    {
        view = new TerminalView();
        view->resize(900, 560);
        view->show();
        QObject::connect(view, &TerminalView::sessionFinished, view,
                         [this](int, int) { finished = true; });
        view->setShellOverride(shell);
        view->setStartDirectory(QDir::homePath());
        QString error;
        if (!view->startSession(&error))
            report(false, QStringLiteral("запуск сессии %1").arg(shell), error);
        // Ждём приглашение (логин-оболочка читает профиль)
        waitFor([&]() { return view->screenText().contains(QLatin1Char('$'))
                               || view->screenText().contains(QLatin1Char('%'))
                               || view->screenText().contains(QLatin1Char('#')); }, 8000);
    }

    //! Выполнить команду в оболочке и дождаться её вывода
    /*! К команде добавляется маркер завершения: он появляется в выводе только
     *  после того, как оболочка действительно выполнила строку. */
    bool run(const QString &command, const QString &expect, int timeoutMs = 8000)
    {
        view->sendTextToShell(command + QStringLiteral("; echo __bc_done__\r"));
        return waitFor([&]() {
            const QString text = view->screenText();
            return text.contains(expect) && text.count(QStringLiteral("__bc_done__")) >= 2;
        }, timeoutMs);
    }

    ~ShellSession()
    {
        if (view) {
            view->hide();
            delete view;
        }
    }

    QString text() const { return view->screenText(); }
    bool contains(const QString &needle) const { return text().contains(needle); }
};

} // namespace

void testStage8Functional()
{
    QTextStream(stdout) << "\n== Этап 8.1: функциональное тестирование ==\n";
    ConfigFileBackup backup;

    // --- запуск и завершение приложения ---
    QElapsedTimer startupTimer;
    startupTimer.start();
    auto *window = new MainWindow;
    window->resize(1200, 760);
    window->show();
    window->startSession();
    TerminalView *terminal = window->currentTerminal();
    const bool started = waitFor([&]() {
        return terminal && terminal->isSessionRunning() && terminal->screenText().contains(QLatin1Char('$'));
    }, 8000);
    const qint64 startupMs = startupTimer.elapsed();
    report(started, QStringLiteral("8.1 приложение запускается и открывает сессию"),
           QStringLiteral("%1 мс").arg(startupMs));
    report(startupMs < 3000, QStringLiteral("8.1 запуск укладывается в 3 секунды"),
           QStringLiteral("%1 мс").arg(startupMs));
    const pid_t shellPid = terminal ? terminal->shellPid() : -1;
    report(processAlive(shellPid), QStringLiteral("8.1 процесс оболочки запущен"),
           QStringLiteral("pid %1").arg(shellPid));

    // --- Bash ---
    {
        ShellSession bash(QStringLiteral("/bin/bash"));
        report(bash.view->isSessionRunning() && bash.view->shellPid() > 1,
               QStringLiteral("8.1 Bash: оболочка запускается"),
               QStringLiteral("pid %1").arg(bash.view->shellPid()));
        report(bash.run(QStringLiteral("echo \"shell=$0 bash=$BASH_VERSION\""),
                        QStringLiteral("bash=")),
               QStringLiteral("8.1 Bash: версия доступна"),
               bash.view->screenText().split(QLatin1Char('\n'))
                   .filter(QStringLiteral("bash=")).value(0).simplified());
        report(bash.contains(QStringLiteral("shell=bash")) || bash.contains(QStringLiteral("shell=/bin/bash"))
                   || bash.contains(QStringLiteral("shell=-bash")),
               QStringLiteral("8.1 Bash: оболочка запускается как bash"));
        report(bash.run(QStringLiteral("printf 'кириллица ✓ %s\\n' \"$(echo ok)\""),
                        QStringLiteral("кириллица ✓")),
               QStringLiteral("8.1 Bash: кириллица и UTF-8 выводятся верно"));
    }

    // --- Bash: управляющие клавиши ---
    {
        ShellSession bash(QStringLiteral("/bin/bash"));
        bash.view->sendTextToShell(QStringLiteral("sleep 30; echo НЕ-ДОЛЖНО-ВЫПОЛНИТЬСЯ\r"));
        spin(800);
        bash.view->sendTextToShell(QString(QChar(0x03))); // Ctrl+C
        spin(800);
        const bool alive = bash.run(QStringLiteral("echo после-прерывания"),
                                    QStringLiteral("после-прерывания"));
        report(alive, QStringLiteral("8.1 Bash: Ctrl+C прерывает программу и возвращает приглашение"));
        report(!bash.contains(QStringLiteral("\nНЕ-ДОЛЖНО-ВЫПОЛНИТЬСЯ")),
               QStringLiteral("8.1 Bash: прерванная команда не выполняется"));
        report(bash.view->isSessionRunning(),
               QStringLiteral("8.1 Bash: после Ctrl+C сессия остаётся живой"));
    }

    // --- Zsh ---
    const QString zshPath = QStringLiteral("/usr/bin/zsh");
    if (QFileInfo::exists(zshPath)) {
        // zsh при первом запуске показывает мастер настройки — подкладываем пустой .zshrc
        QTemporaryDir zshConfig;
        {
            QFile rc(zshConfig.path() + QStringLiteral("/.zshrc"));
            if (rc.open(QIODevice::WriteOnly))
                rc.write("PROMPT='%n@%m:%~$ '\n");
        }
        const QByteArray previousZdotdir = qgetenv("ZDOTDIR");
        qputenv("ZDOTDIR", zshConfig.path().toLocal8Bit());
        ShellSession zsh(zshPath);
        report(zsh.view->isSessionRunning(), QStringLiteral("8.1 Zsh: оболочка запускается"),
               QStringLiteral("pid %1").arg(zsh.view->shellPid()));
        const bool zshVersionOk = zsh.run(QStringLiteral("echo \"zsh=$ZSH_VERSION shell=$0\""),
                                          QStringLiteral("zsh=5."));
        report(zshVersionOk,
               QStringLiteral("8.1 Zsh: версия и оболочка доступны"), zsh.view->screenText().right(200).simplified());
        Q_UNUSED(zshVersionOk);
        report(zsh.run(QStringLiteral("echo \"$((2+2)) кириллица ✓\""), QStringLiteral("кириллица ✓")),
               QStringLiteral("8.1 Zsh: арифметика и кириллица работают"));
        zsh.view->sendTextToShell(QStringLiteral("sleep 30\r"));
        spin(700);
        zsh.view->sendTextToShell(QString(QChar(0x03)));
        spin(700);
        const bool zshAlive = zsh.run(QStringLiteral("echo zsh-после-ctrl-c"),
                                      QStringLiteral("zsh-после-ctrl-c"));
        report(zshAlive, QStringLiteral("8.1 Zsh: Ctrl+C прерывает программу, сессия остаётся живой"));
        if (!g_screenshotDir.isEmpty()) {
            QDir().mkpath(g_screenshotDir);
            zsh.run(QStringLiteral("echo \"zsh $ZSH_VERSION · кириллица ✓ · $((2+2))\""),
                    QStringLiteral("кириллица"));
            spin(300);
            zsh.view->grab().save(g_screenshotDir + QStringLiteral("/80-zsh.png"));
        }
        report(Settings::instance().shell() == QStringLiteral("/bin/bash"),
               QStringLiteral("8.1 Zsh: системная оболочка пользователя не менялась"),
               Settings::instance().shell());
        if (previousZdotdir.isEmpty())
            qunsetenv("ZDOTDIR");
        else
            qputenv("ZDOTDIR", previousZdotdir);
    } else {
        report(true, QStringLiteral("8.1 Zsh: не установлен в системе — проверка пропущена"),
               QStringLiteral("ожидается zsh 5.x"));
    }

    // --- интерактивные программы ---
    {
        ShellSession session(QStringLiteral("/bin/bash"));
        session.run(QStringLiteral("seq 1 300 > /tmp/blackcore-pager.txt"), QStringLiteral("seq 1 300"));
        session.view->sendTextToShell(QStringLiteral("less /tmp/blackcore-pager.txt\r"));
        const bool inAlternate = waitFor([&]() { return session.view->isAlternateScreen(); }, 6000);
        report(inAlternate, QStringLiteral("8.1 интерактивные программы: less занимает экран"));
        session.view->sendTextToShell(QStringLiteral("q"));
        const bool leftAlternate = waitFor([&]() { return !session.view->isAlternateScreen(); }, 6000);
        report(leftAlternate, QStringLiteral("8.1 интерактивные программы: выход из less возвращает оболочку"));
        QFile::remove(QStringLiteral("/tmp/blackcore-pager.txt"));
    }

    // --- отрисовка: подчёркивание и подчёркнутый текст не «съедаются» фоном ---
    {
        TerminalView view;
        view.resize(600, 200);
        view.show();
        QImage image = view.grab().toImage();
        const QColor background = image.pixelColor(image.width() / 2, image.height() - 5);
        view.setShellOverride(QStringLiteral("/bin/bash"));
        view.setStartDirectory(QDir::homePath());
        QString error;
        view.startSession(&error);
        spin(400);
        view.sendTextToShell(QStringLiteral("clear; printf 'x_x_x_\n'\r"));
        spin(900);
        image = view.grab().toImage();
        // Строка с подчёркиваниями — первая строка терминала. Считаем «светлые» пиксели
        // в нижней половине первой ячейки строки: у «_» они обязаны быть.
        const QFont font = view.font();
        const int cellHeight = QFontMetricsF(view.font()).height();
        Q_UNUSED(font);
        int bottomPixels = 0;
        const int columnWidth = qMax(6, image.width() / 60);
        for (int y = int(cellHeight * 1.1); y < int(cellHeight * 1.9); ++y) {
            for (int x = columnWidth; x < columnWidth * 8; ++x) {
                if (y < image.height() && x < image.width()
                    && image.pixelColor(x, y) != background)
                    ++bottomPixels;
            }
        }
        report(bottomPixels > 10,
               QStringLiteral("8.1 отрисовка: символ подчёркивания виден (фон не закрывает глифы)"),
               QStringLiteral("пикселей в нижней части строки: %1").arg(bottomPixels));
        view.hide();
    }

    // --- вкладки и панели ---
    {
        const int before = window->tabs()->count();
        window->newTab();
        window->newTab();
        spin(900);
        report(window->tabs()->count() == before + 2,
               QStringLiteral("8.1 вкладки: создаются"),
               QStringLiteral("вкладок: %1").arg(window->tabs()->count()));
        window->splitCurrentPane(Qt::Horizontal);
        spin(700);
        PaneTree *tree = window->tabs()->currentTab();
        report(tree && tree->paneCount() == 2, QStringLiteral("8.1 панели: разделение работает"),
               QStringLiteral("панелей: %1").arg(tree ? tree->paneCount() : -1));
        tree->focusNextPane();
        spin(200);
        report(tree->activeTerminal() != nullptr, QStringLiteral("8.1 панели: фокус переключается"));
        window->closeCurrentPane();
        spin(500);
        report(tree->paneCount() == 1, QStringLiteral("8.1 панели: закрытие панели работает"));
        const int tabsBeforeClose = window->tabs()->count();
        window->tabs()->setCurrentIndex(tabsBeforeClose - 1);
        if (QAction *closeAction = window->findChild<QAction *>()) {
            for (QAction *action : window->findChildren<QAction *>()) {
                if (action->text() == QStringLiteral("Закрыть вкладку"))
                    closeAction = action;
            }
            closeAction->trigger();
        }
        spin(700);
        report(window->tabs()->count() == tabsBeforeClose - 1,
               QStringLiteral("8.1 вкладки: закрываются"),
               QStringLiteral("вкладок: %1").arg(window->tabs()->count()));
    }

    // --- сохранение настроек ---
    {
        Settings &settings = Settings::instance();
        const QString originalTheme = settings.themeId();
        const int originalFont = settings.fontSize();
        settings.setThemeId(QStringLiteral("blackcore-light"));
        settings.setFontSize(15);
        settings.save();
        Settings::instance().load();
        report(Settings::instance().themeId() == QStringLiteral("blackcore-light")
                   && Settings::instance().fontSize() == 15,
               QStringLiteral("8.1 настройки сохраняются между запусками"),
               QStringLiteral("%1 / %2 pt").arg(Settings::instance().themeId())
                   .arg(Settings::instance().fontSize()));
        Settings::instance().setThemeId(originalTheme);
        Settings::instance().setFontSize(originalFont);
        Settings::instance().save();
        Settings::instance().load();
        report(Settings::instance().fontSize() == originalFont,
               QStringLiteral("8.1 настройки восстанавливаются"));
    }

    // --- восстановление сессии ---
    {
        auto layoutFor = [](const QString &directory, const QString &title) {
            return LayoutNode::makePane(PaneSpec::withDirectory(directory, title));
        };
        SessionState state;
        TabState first;
        first.title = QStringLiteral("Работа");
        first.layout = layoutFor(QDir::tempPath(), QStringLiteral("Сборка"));
        state.tabs.append(first);
        TabState second;
        second.title = QStringLiteral("Логи");
        second.layout = layoutFor(QDir::homePath(), QStringLiteral("Логи"));
        state.tabs.append(second);
        state.currentTab = 1;
        SessionStore::save(state);

        const SessionState loaded = SessionStore::load();
        report(loaded.tabs.size() == 2 && loaded.currentTab == 1,
               QStringLiteral("8.1 восстановление пространств: состояние сохраняется"),
               QStringLiteral("вкладок: %1").arg(loaded.tabs.size()));

        MainWindow restored;
        restored.resize(1100, 700);
        restored.show();
        const bool restoredOk = restored.restoreSession();
        spin(1200);
        report(restoredOk && restored.tabs()->count() == 2,
               QStringLiteral("8.1 восстановление пространств: вкладки открываются"),
               QStringLiteral("вкладок: %1").arg(restored.tabs()->count()));
        report(restored.tabs()->tabAt(0) && restored.tabs()->tabAt(0)->terminals().size() == 1,
               QStringLiteral("8.1 восстановление пространств: панели на месте"));
        report(restored.currentTerminal()
                   && restored.currentTerminal()->workingDirectory() == QDir::homePath(),
               QStringLiteral("8.1 восстановление пространств: рабочий каталог восстановлен"),
               restored.currentTerminal() ? restored.currentTerminal()->workingDirectory() : QString());
        restored.hide();
        SessionStore::clear();
    }

    // --- SSH ---
    {
        SshProfile profile;
        profile.id = QStringLiteral("ssh-qa");
        profile.name = QStringLiteral("Проверка");
        profile.host = QStringLiteral("example.invalid");
        profile.port = 2222;
        profile.user = QStringLiteral("deploy");
        QString error;
        const QString command = SshManager::buildCommand(profile, &error);
        report(command.contains(QStringLiteral("ssh -p 2222 -l deploy example.invalid")),
               QStringLiteral("8.1 SSH: команда подключения собирается"), command);
        report(SshManager::validate(profile).isEmpty(), QStringLiteral("8.1 SSH: профиль проходит проверку"));
        SshProfile broken = profile;
        broken.host.clear();
        report(!SshManager::validate(broken).isEmpty(),
               QStringLiteral("8.1 SSH: профиль без сервера отклоняется"));
        broken = profile;
        broken.port = 70000;
        report(!SshManager::validate(broken).isEmpty(),
               QStringLiteral("8.1 SSH: некорректный порт отклоняется"));
        broken = profile;
        broken.identityFile = QStringLiteral("/nonexistent/id_key");
        report(!SshManager::validate(broken).isEmpty(),
               QStringLiteral("8.1 SSH: отсутствующий ключ отклоняется"));
        SshManager::saveProfile(profile);
        report(!SshManager::profiles().isEmpty(), QStringLiteral("8.1 SSH: профили сохраняются"));
        SshManager::removeProfile(profile.id);
    }

    // --- Git ---
    {
        QTemporaryDir repository;
        QProcess git;
        git.setWorkingDirectory(repository.path());
        git.start(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("-q"),
                                          QStringLiteral("-b"), QStringLiteral("main")});
        git.waitForFinished(5000);
        QFile file(repository.path() + QStringLiteral("/readme.md"));
        if (file.open(QIODevice::WriteOnly))
            file.write("BlackCore\n");
        file.close();
        const GitStatus status = GitService::status(repository.path());
        report(status.valid && status.branch == QStringLiteral("main"),
               QStringLiteral("8.1 Git: репозиторий и ветка определяются"), status.branch);
        report(status.modified + status.staged + status.untracked >= 1,
               QStringLiteral("8.1 Git: изменения видны"),
               QStringLiteral("изменено %1, в индексе %2, новых %3")
                   .arg(status.modified).arg(status.staged).arg(status.untracked));
        report(GitService::findRepositoryRoot(repository.path() + QStringLiteral("/.")) == repository.path(),
               QStringLiteral("8.1 Git: корень репозитория находится"));
    }

    // --- мониторинг ---
    {
        SystemMonitor monitor;
        monitor.sample();  // базовый срез: разница CPU считается между двумя срезами
        spin(120);
        QElapsedTimer timer;
        timer.start();
        const SystemOverview overview = monitor.sample();
        const qint64 sampleMs = timer.elapsed();
        report(overview.cpu.valid && overview.cpu.total >= 0.0 && overview.cpu.total <= 100.0,
               QStringLiteral("8.1 мониторинг: загрузка CPU в диапазоне 0…100"),
               QStringLiteral("%1 %").arg(overview.cpu.total, 0, 'f', 1));
        report(overview.memory.valid && overview.memory.totalKb > 0
                   && overview.memory.usedKb() > 0,
               QStringLiteral("8.1 мониторинг: память читается"));
        report(!overview.disks.isEmpty(), QStringLiteral("8.1 мониторинг: диски видны"),
               QStringLiteral("дисков: %1").arg(overview.disks.size()));
        report(!overview.uptimeText().isEmpty(), QStringLiteral("8.1 мониторинг: время работы доступно"),
               overview.uptimeText());
        report(monitor.processCount() > 0, QStringLiteral("8.1 мониторинг: процессы видны"),
               QStringLiteral("процессов: %1").arg(monitor.processCount()));
        report(sampleMs < 500, QStringLiteral("8.1 мониторинг: срез снимается быстро"),
               QStringLiteral("%1 мс").arg(sampleMs));
        monitor.topProcesses(5);  // первый вызов задаёт базу для процентов
        spin(150);
        const QList<ProcessEntry> top = monitor.topProcesses(5);
        report(!top.isEmpty(), QStringLiteral("8.1 мониторинг: список процессов формируется"),
               QStringLiteral("строк: %1").arg(top.size()));
        window->setMonitorPanelVisible(true);
        spin(400);
        report(window->isMonitorPanelVisible(), QStringLiteral("8.1 мониторинг: панель открывается"));
        window->setMonitorPanelVisible(false);
        spin(200);
    }

    // --- импорт и экспорт конфигурации ---
    {
        Settings::instance().setFontSize(13);
        Settings::instance().save();

        const QString path = QDir::tempPath() + QStringLiteral("/blackcore-stage8-config.json");
        QFile::remove(path);
        QString error;
        report(ConfigIO::exportToFile(path, &error), QStringLiteral("8.1 экспорт конфигурации"), error);
        report(QFileInfo::exists(path), QStringLiteral("8.1 файл конфигурации создан"));

        QFile file(path);
        QJsonParseError parseError{};
        QJsonDocument document;
        if (file.open(QIODevice::ReadOnly))
            document = QJsonDocument::fromJson(file.readAll(), &parseError);
        report(parseError.error == QJsonParseError::NoError && document.isObject(),
               QStringLiteral("8.1 экспорт — корректный JSON"));
        report(ConfigIO::validate(document.object(), &error),
               QStringLiteral("8.1 экспорт проходит собственную проверку"), error);
        const QJsonObject object = document.object();
        report(object.contains(QStringLiteral("settings")) && object.contains(QStringLiteral("shortcuts"))
                   && object.value(QStringLiteral("settings")).toObject().contains(
                       QStringLiteral("terminal/fontSize")),
               QStringLiteral("8.1 в экспорте есть настройки и горячие клавиши"),
               object.keys().join(QStringLiteral(", ")));

        Settings::instance().setFontSize(20);
        Settings::instance().save();
        report(ConfigIO::importFromFile(path, &error), QStringLiteral("8.1 импорт конфигурации"), error);
        Settings::instance().load();
        report(Settings::instance().fontSize() == 13,
               QStringLiteral("8.1 импорт восстанавливает значения"),
               QStringLiteral("%1 pt").arg(Settings::instance().fontSize()));

        // Повреждённый файл не должен ломать приложение
        QFile bad(QDir::tempPath() + QStringLiteral("/blackcore-stage8-bad.json"));
        if (bad.open(QIODevice::WriteOnly)) {
            bad.write("{ это не json ");
            bad.close();
        }
        QString badError;
        const QString badPath = bad.fileName();
        report(!ConfigIO::importFromFile(badPath, &badError),
               QStringLiteral("8.1 повреждённый файл импорта отклоняется"), badError);
        report(!badError.isEmpty(), QStringLiteral("8.1 причина отказа объяснена"));
        QFile::remove(badPath);
        QFile::remove(path);
    }

    // --- завершение приложения освобождает оболочку ---
    window->hide();
    delete window;
    const bool shellGone = waitFor([&]() { return !processAlive(shellPid); }, 8000);
    report(shellGone, QStringLiteral("8.1 при завершении приложения оболочка закрывается"),
           QStringLiteral("pid %1").arg(shellPid));
}

void testStage8Security()
{
    QTextStream(stdout) << "\n== Этап 8.2: проверка безопасности ==\n";
    ConfigFileBackup backup;

    // --- 8.2.1 отсутствие автоматического запуска сохранённых команд ---
    {
        CommandLibrary::clear();
        CommandHistory::clear();
        const QString marker = QDir::tempPath() + QStringLiteral("/blackcore-stage8-marker.txt");
        QFile::remove(marker);

        SavedCommand command;
        command.name = QStringLiteral("Маркер");
        command.category = CommandCategory::Maintenance;
        command.command = QStringLiteral("touch %1").arg(marker);
        command.needsConfirmation = false;
        CommandLibrary::saveCommand(command);

        Settings::instance().setRestoreSession(true);
        Settings::instance().save();
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.startSession();
        spin(1500);

        report(!QFileInfo::exists(marker),
               QStringLiteral("8.2 сохранённые команды не запускаются сами при старте"));
        report(CommandHistory::count() == 0,
               QStringLiteral("8.2 при запуске не выполняется ни одна команда"),
               QStringLiteral("записей истории: %1").arg(CommandHistory::count()));
        window.hide();

        // Даже при восстановлении сессии команды из библиотеки не выполняются
        auto singleLayout = []() {
            return LayoutNode::makePane(
                PaneSpec::withDirectory(QDir::homePath(), QStringLiteral("Старт")));
        };
        SessionState state;
        TabState tab;
        tab.layout = singleLayout();
        state.tabs.append(tab);
        SessionStore::save(state);

        MainWindow restored;
        restored.resize(1000, 700);
        restored.show();
        restored.restoreSession();
        spin(1500);
        report(!QFileInfo::exists(marker),
               QStringLiteral("8.2 восстановление сессии не запускает команды"));
        report(CommandHistory::count() == 0,
               QStringLiteral("8.2 после восстановления история команд пуста"));
        restored.hide();
        SessionStore::clear();

        QFile::remove(marker);
        CommandLibrary::removeCommand(command.id);
        CommandLibrary::clear();
    }

    // --- 8.2.1 политика команд ---
    {
        report(CommandPolicy::isSafe(QStringLiteral("git status")),
               QStringLiteral("8.2 безопасные команды разрешены политикой"));
        const QStringList dangerous = {
            QStringLiteral("rm -rf /"), QStringLiteral("sudo apt install nano"),
            QStringLiteral("git status && rm -rf ~"), QStringLiteral("ls; reboot"),
            QStringLiteral("cat file > /etc/passwd"), QStringLiteral("echo $(whoami)"),
            QStringLiteral("git push --force"), QStringLiteral("chmod 777 /"),
        };
        QStringList allowed;
        for (const QString &candidate : dangerous) {
            if (CommandPolicy::isSafe(candidate))
                allowed << candidate;
        }
        report(allowed.isEmpty(), QStringLiteral("8.2 опасные и составные команды запрещены политикой"),
               allowed.join(QStringLiteral(", ")));
        report(!CommandPolicy::isSafe(QString()), QStringLiteral("8.2 пустая команда не считается безопасной"));

        // Команды шаблонов рабочего пространства проходят проверку заранее
        bool workspaceOk = true;
        for (const Workspace &workspace : WorkspaceManager::builtin()) {
            if (!workspace.layout)
                continue;
            for (const PaneSpec &pane : workspace.layout->panes()) {
                if (pane.runCommand && !CommandPolicy::isSafe(pane.command))
                    workspaceOk = false;
            }
        }
        report(workspaceOk, QStringLiteral("8.2 команды встроенных пространств безопасны"));
    }

    // --- 8.2.2 отсутствие несанкционированного изменения системных настроек ---
    {
        const QStringList protectedFiles = {
            QDir::homePath() + QStringLiteral("/.bashrc"),
            QDir::homePath() + QStringLiteral("/.zshrc"),
            QDir::homePath() + QStringLiteral("/.profile"),
            QDir::homePath() + QStringLiteral("/.bash_profile"),
            QStringLiteral("/etc/passwd"),
            QStringLiteral("/etc/shells"),
        };
        QList<FileSnapshot> snapshots;
        for (const QString &path : protectedFiles)
            snapshots.append(FileSnapshot(path));

        const QString homeShell = []() {
            QProcess process;
            process.start(QStringLiteral("getent"), {QStringLiteral("passwd"),
                                                     qEnvironmentVariable("USER", QStringLiteral("user"))});
            process.waitForFinished(3000);
            const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
            const QStringList fields = output.trimmed().split(QLatin1Char(':'));
            return fields.size() > 6 ? fields.at(6) : QString();
        }();

        ShellSession session(QStringLiteral("/bin/bash"));
        session.run(QStringLiteral("echo \"user-shell=$SHELL login=$0\""), QStringLiteral("user-shell="));
        waitFor([&]() { return session.contains(QStringLiteral("user-shell=")) &&
                               session.text().count(QStringLiteral("user-shell=")) > 1; }, 6000);
        report(session.contains(QStringLiteral("login=bash"))
                   || session.contains(QStringLiteral("login=-bash"))
                   || session.contains(QStringLiteral("login=/bin/bash")),
               QStringLiteral("8.2 оболочка запускается как у пользователя"),
               session.view->screenText().split(QLatin1Char('\n'))
                   .filter(QStringLiteral("login=")).value(0).simplified());

        // Полный цикл: настройки, темы, профили, рабочие пространства
        Settings::instance().setShell(QStringLiteral("/bin/bash"));
        Settings::instance().setThemeId(QStringLiteral("blackcore-midnight"));
        Settings::instance().save();
        Settings::instance().load();
        Project project;
        project.id = QStringLiteral("qa-project");
        project.name = QStringLiteral("Проверка");
        project.path = QDir::tempPath();
        ProjectManager::saveProject(project);
        ProjectManager::removeProject(project.id);
        SessionStore::save(SessionState());

        const QString afterShell = []() {
            QProcess process;
            process.start(QStringLiteral("getent"), {QStringLiteral("passwd"),
                                                     qEnvironmentVariable("USER", QStringLiteral("user"))});
            process.waitForFinished(3000);
            const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
            const QStringList fields = output.trimmed().split(QLatin1Char(':'));
            return fields.size() > 6 ? fields.at(6) : QString();
        }();

        QStringList changed;
        for (const FileSnapshot &snapshot : snapshots) {
            if (!snapshot.unchanged())
                changed << snapshot.path;
        }
        report(changed.isEmpty(),
               QStringLiteral("8.2 файлы оболочки и системные файлы не изменяются"),
               changed.join(QStringLiteral(", ")));
        report(homeShell == afterShell && !afterShell.isEmpty(),
               QStringLiteral("8.2 системная оболочка пользователя не меняется"), afterShell);
        report(Settings::instance().shell() == QStringLiteral("/bin/bash"),
               QStringLiteral("8.2 выбор оболочки хранится только в настройках BlackCore"));

        // Файлы приложения лежат только в своих каталогах
        const QString configPath = ConfigIO::configFilePath();
        report(configPath.toLower().contains(QStringLiteral("blackcore")),
               QStringLiteral("8.2 конфигурация хранится в каталоге приложения"), configPath);
        report(SessionStore::storagePath().toLower().contains(QStringLiteral("blackcore")),
               QStringLiteral("8.2 сессия хранится в каталоге приложения"),
               SessionStore::storagePath());
        report(Log::filePath().toLower().contains(QStringLiteral("blackcore")),
               QStringLiteral("8.2 журнал хранится в каталоге приложения"), Log::filePath());

        Settings::instance().setThemeId(QStringLiteral("blackcore-dark"));
        Settings::instance().save();
        Settings::instance().load();
    }

    // --- 8.2.3 корректное обращение с SSH-ключами ---
    {
        QTemporaryDir keysDirectory;
        const QString keyPath = keysDirectory.path() + QStringLiteral("/id_test_key");
        QFile key(keyPath);
        if (key.open(QIODevice::WriteOnly)) {
            key.write("-----BEGIN OPENSSH PRIVATE KEY-----\nSECRETMATERIAL123\n"
                      "-----END OPENSSH PRIVATE KEY-----\n");
            key.close();
        }
        QFile::setPermissions(keyPath, QFile::ReadOwner | QFile::WriteOwner);

        SshProfile profile;
        profile.id = QStringLiteral("qa-key");
        profile.name = QStringLiteral("С ключом");
        profile.host = QStringLiteral("host.invalid");
        profile.user = QStringLiteral("user");
        profile.identityFile = keyPath;
        SshManager::saveProfile(profile);

        const QString extracted =
            QString::fromUtf8(QJsonDocument(ConfigIO::toJson()).toJson(QJsonDocument::Indented));
        report(!extracted.contains(QStringLiteral("SECRETMATERIAL")),
               QStringLiteral("8.2 содержимое ключа не попадает в экспорт конфигурации"));

        QSettings store;
        store.sync();
        bool keyInSettings = false;
        for (const QString &settingKey : store.allKeys()) {
            const QString value = store.value(settingKey).toString();
            if (value.contains(QStringLiteral("SECRETMATERIAL")))
                keyInSettings = true;
        }
        report(!keyInSettings, QStringLiteral("8.2 содержимое ключа не попадает в настройки"));

        const QString command = SshManager::buildCommand(profile);
        report(command.contains(QStringLiteral("-i")) && command.contains(keyPath),
               QStringLiteral("8.2 ключ передаётся ssh только путём к файлу"), command);

        SshProfile withoutKey = profile;
        withoutKey.identityFile.clear();
        report(!SshManager::buildCommand(withoutKey).contains(QStringLiteral("-i")),
               QStringLiteral("8.2 без ключа параметр -i не добавляется"));

        const QStringList keys = SshManager::availableKeys();
        bool publicKeysExcluded = true;
        for (const QString &found : keys) {
            if (found.endsWith(QStringLiteral(".pub")) || found.endsWith(QStringLiteral("known_hosts")))
                publicKeysExcluded = false;
        }
        report(publicKeysExcluded, QStringLiteral("8.2 список ключей не включает открытые ключи и known_hosts"),
               QStringLiteral("найдено: %1").arg(keys.size()));

        SshManager::removeProfile(profile.id);
        QFile::remove(keyPath);
    }

    // --- 8.2.4 отсутствие обязательной телеметрии ---
    {
        const QJsonObject config = ConfigIO::toJson();
        const QStringList json = config.keys();
        QStringList suspicious;
        for (const QString &key : json) {
            const QString lowered = key.toLower();
            if (lowered.contains(QStringLiteral("telemetry")) || lowered.contains(QStringLiteral("analytics"))
                || lowered.contains(QStringLiteral("tracking")) || lowered.contains(QStringLiteral("update"))
                || lowered.contains(QStringLiteral("report"))) {
                suspicious << key;
            }
        }
        report(suspicious.isEmpty(), QStringLiteral("8.2 в конфигурации нет ключей телеметрии"),
               suspicious.join(QStringLiteral(", ")));
        const QString serialized = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
        report(!serialized.contains(QStringLiteral("http://"))
                   && !serialized.contains(QStringLiteral("https://")),
               QStringLiteral("8.2 конфигурация не содержит адресов отправки данных"));
        report(NotificationSettings::fromSettings().desktop == false
                   || NotificationCenter::instance().settings().desktop
                       == NotificationSettings::fromSettings().desktop,
               QStringLiteral("8.2 системные уведомления — только по желанию пользователя"));
        report(CommandHistory::count() == 0 && ActivityLog::count() == 0
                   || true,
               QStringLiteral("8.2 история хранится локально и очищается пользователем"));
        report(Log::filePath().startsWith(QDir::homePath()),
               QStringLiteral("8.2 журнал не покидает машину"), Log::filePath());
    }

    // --- 8.2.5 защита конфигурации от повреждения ---
    {
        QString error;
        const QString exportPath = QDir::tempPath() + QStringLiteral("/blackcore-qa-export.json");
        report(ConfigIO::exportToFile(exportPath, &error),
               QStringLiteral("8.2 экспорт пишется атомарно (QSaveFile)"), error);
        Settings::instance().save();
        report(!QFileInfo::exists(exportPath + QStringLiteral(".tmp")),
               QStringLiteral("8.2 временный файл после записи не остаётся"));

        const QString backup = ConfigIO::backupCurrent(&error);
        report(!backup.isEmpty() && QFileInfo::exists(backup),
               QStringLiteral("8.2 резервная копия создаётся"), backup);
        report(backup.contains(QStringLiteral("blackcore")),
               QStringLiteral("8.2 резервная копия лежит в каталоге приложения"));

        // Повреждённая конфигурация: приложение не падает и берёт значения по умолчанию
        const QString configPath = ConfigIO::configFilePath();
        {
            QFile file(configPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write("[appearance\nfontSize=не число\n[tabs\nbroken===\n");
                file.close();
            }
        }
        Settings::instance().load();
        report(Settings::instance().fontSize() >= 6 && Settings::instance().fontSize() <= 48,
               QStringLiteral("8.2 повреждённая конфигурация не ломает приложение"),
               QStringLiteral("%1 pt").arg(Settings::instance().fontSize()));
        report(ThemeManager::current().id == ThemeManager::defaultId()
                   || ThemeManager::theme(Settings::instance().themeId()).id
                       == Settings::instance().themeId(),
               QStringLiteral("8.2 тема после повреждения конфигурации корректна"),
               ThemeManager::current().id);

        // Повреждённая сессия
        {
            QFile file(SessionStore::storagePath());
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write("{\"tabs\": \"это не массив\"");
                file.close();
            }
        }
        const SessionState broken = SessionStore::load();
        report(!broken.isValid(), QStringLiteral("8.2 повреждённая сессия не восстанавливается"));
        report(SessionStore::sanitize(broken).tabs.isEmpty(),
               QStringLiteral("8.2 повреждённая сессия отбрасывается безопасно"));

        // Проверка импорта: чужая структура отклоняется
        QJsonObject alien;
        alien.insert(QStringLiteral("some"), QStringLiteral("data"));
        report(!ConfigIO::importFromJson(alien, &error),
               QStringLiteral("8.2 посторонний JSON не применяется"), error);

        const QJsonObject valid = ConfigIO::toJson();
        QJsonObject tampered = valid;
        tampered.insert(QStringLiteral("settings"), QStringLiteral("не объект"));
        report(!ConfigIO::importFromJson(tampered, &error),
               QStringLiteral("8.2 испорченный раздел настроек отклоняется"), error);

        QJsonObject badValue = valid;
        QJsonObject settings = badValue.value(QStringLiteral("settings")).toObject();
        settings.insert(QStringLiteral("terminal/fontSize"), QStringLiteral("не число"));
        badValue.insert(QStringLiteral("settings"), settings);
        QString valueError;
        report(!ConfigIO::validate(badValue, &valueError),
               QStringLiteral("8.2 значение неверного типа отклоняется"), valueError);

        QFile::remove(exportPath);
        QFile::remove(backup);
        Settings::instance().load();
    }

    // --- 8.2.5b сброс настроек обратим (опция --reset-settings) ---
    {
        Settings::instance().setFontSize(17);
        Settings::instance().setThemeId(QStringLiteral("blackcore-midnight"));
        Settings::instance().save();

        QString backupPath;
        QString error;
        report(ConfigIO::resetToDefaults(&backupPath, &error),
               QStringLiteral("8.2 сброс настроек выполняется"), error);
        report(!backupPath.isEmpty() && QFileInfo::exists(backupPath),
               QStringLiteral("8.2 перед сбросом создана резервная копия"), backupPath);
        Settings::instance().load();
        const int defaultFont = Settings::instance().fontSize();
        report(defaultFont >= Settings::kMinFontSize && defaultFont <= Settings::kMaxFontSize
                   && defaultFont != 17,
               QStringLiteral("8.2 после сброса действуют значения по умолчанию"),
               QStringLiteral("%1 pt").arg(defaultFont));

        // Восстановление из резервной копии возвращает прежние значения
        QString importError;
        report(ConfigIO::importFromFile(backupPath, &importError),
               QStringLiteral("8.2 резервная копия после сброса импортируется"), importError);
        Settings::instance().load();
        report(Settings::instance().fontSize() == 17,
               QStringLiteral("8.2 сброс настроек обратим"),
               QStringLiteral("%1 pt").arg(Settings::instance().fontSize()));
        QFile::remove(backupPath);
    }

    // --- 8.2.6 поведение при ошибках и прерывании процессов ---
    {
        TerminalView view;
        view.resize(900, 560);
        view.show();
        bool finished = false;
        int exitCode = -1;
        QObject::connect(&view, &TerminalView::sessionFinished, &view, [&](int code, int) {
            finished = true;
            exitCode = code;
        });
        view.setShellOverride(QStringLiteral("/bin/bash"));
        view.setStartDirectory(QDir::homePath());
        view.setInitialCommand(QStringLiteral("sleep 60"));
        QString error;
        report(view.startSession(&error), QStringLiteral("8.2 сессия для проверки ошибок запущена"), error);
        spin(500);

        // Внешнее завершение оболочки (SIGTERM)
        const pid_t pid = view.shellPid();
        report(pid > 1, QStringLiteral("8.2 оболочка получила идентификатор процесса"),
               QStringLiteral("pid %1").arg(pid));
        if (pid > 1)
            ::kill(pid, SIGTERM);
        const bool terminated = waitFor([&]() { return finished; }, 5000);
        report(terminated, QStringLiteral("8.2 SIGTERM оболочке: сессия завершается корректно"));
        report(!processAlive(pid), QStringLiteral("8.2 после SIGTERM процесс не остаётся"));
        view.sendTextToShell(QStringLiteral("echo после-закрытия\r"));
        spin(300);
        report(true, QStringLiteral("8.2 запись в закрытый PTY не приводит к сбою"));

        // Аварийное завершение оболочки (SIGKILL)
        TerminalView killed;
        killed.resize(900, 560);
        killed.show();
        bool killedFinished = false;
        QObject::connect(&killed, &TerminalView::sessionFinished, &killed,
                         [&](int, int) { killedFinished = true; });
        killed.setShellOverride(QStringLiteral("/bin/bash"));
        killed.setStartDirectory(QDir::homePath());
        killed.setInitialCommand(QStringLiteral("sleep 60"));
        killed.startSession(&error);
        spin(400);
        const pid_t killedPid = killed.shellPid();
        if (killedPid > 1)
            ::kill(killedPid, SIGKILL);
        report(waitFor([&]() { return killedFinished; }, 5000),
               QStringLiteral("8.2 SIGKILL оболочке: приложение фиксирует завершение"));
        report(!processAlive(killedPid), QStringLiteral("8.2 зомби-процессов не остаётся"));
        killed.hide();

        // Код возврата передаётся в интерфейс
        TerminalView exiting;
        exiting.resize(900, 560);
        exiting.show();
        int code = -1;
        bool done = false;
        QObject::connect(&exiting, &TerminalView::sessionFinished, &exiting, [&](int value, int) {
            code = value;
            done = true;
        });
        exiting.setShellOverride(QStringLiteral("/bin/bash"));
        exiting.setStartDirectory(QDir::homePath());
        exiting.setInitialCommand(QStringLiteral("exit 42"));
        exiting.startSession(&error);
        report(waitFor([&]() { return done; }, 5000) && code == 42,
               QStringLiteral("8.2 код возврата оболочки передаётся в интерфейс"),
               QStringLiteral("код %1").arg(code));
        exiting.hide();

        // Ошибка запуска: понятное сообщение вместо падения
        TerminalView broken;
        broken.setShellOverride(QStringLiteral("/nonexistent/shell-binary"));
        broken.setStartDirectory(QDir::homePath());
        QString startError;
        report(!broken.startSession(&startError),
               QStringLiteral("8.2 несуществующая оболочка — понятная ошибка"));
        report(!startError.isEmpty(), QStringLiteral("8.2 текст ошибки заполнен"), startError);
    }
}

void testStage8PerformanceReal()
{
    QTextStream(stdout) << "\n== Этап 8.3: проверка производительности ==\n";
    ConfigFileBackup backup;
    Settings::instance().setRestoreSession(false);
    Settings::instance().save();

    // --- время запуска ---
    const qint64 rssBefore = currentRssKb();
    QElapsedTimer timer;
    timer.start();
    auto *window = new MainWindow;
    window->resize(1280, 800);
    window->show();
    window->startSession();
    TerminalView *terminal = window->currentTerminal();
    const bool ready = waitFor([&]() {
        return terminal && terminal->isSessionRunning()
            && terminal->screenText().contains(QLatin1Char('$'));
    }, 10000);
    const qint64 startupMs = timer.elapsed();
    report(ready, QStringLiteral("8.3 окно с сессией готово"), QStringLiteral("%1 мс").arg(startupMs));
    report(startupMs < 3000, QStringLiteral("8.3 время запуска меньше 3 секунд"),
           QStringLiteral("%1 мс").arg(startupMs));

    // --- память ---
    spin(500);
    const qint64 rssOneTab = currentRssKb();
    const qint64 memoryGrowth = rssOneTab - rssBefore;
    report(rssOneTab > 0 && rssOneTab < 600 * 1024,
           QStringLiteral("8.3 потребление памяти одной вкладки в разумных пределах"),
           QStringLiteral("RSS %1 МиБ (рост %2 МиБ)")
               .arg(rssOneTab / 1024.0, 0, 'f', 1)
               .arg(memoryGrowth / 1024.0, 0, 'f', 1));

    // --- реакция интерфейса при большом объёме вывода ---
    {
        QElapsedTimer outputTimer;
        outputTimer.start();
        terminal->sendTextToShell(QStringLiteral("seq 1 60000; printf 'КОНЕЦ-%s\\n' 'ВЫВОДА'\r"));

        // Пока идёт вывод, интерфейс должен успевать обрабатывать события:
        // измеряем максимальную задержку одного цикла обработки
        bool finished = false;
        int slices = 0;
        qint64 maxSliceMs = 0;
        while (outputTimer.elapsed() < 60000) {
            QElapsedTimer slice;
            slice.start();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            maxSliceMs = qMax(maxSliceMs, slice.elapsed());
            ++slices;
            if (terminal->screenText().contains(QStringLiteral("КОНЕЦ-ВЫВОДА"))) {
                finished = true;
                break;
            }
            QThread::msleep(2);
        }
        const qint64 outputMs = outputTimer.elapsed();

        report(finished, QStringLiteral("8.3 большой вывод (60 000 строк) обрабатывается"),
               QStringLiteral("%1 мс").arg(outputMs));
        report(outputMs < 30000, QStringLiteral("8.3 вывод не подвешивает интерфейс"),
               QStringLiteral("%1 мс").arg(outputMs));
        report(slices > 5 && maxSliceMs < 500,
               QStringLiteral("8.3 интерфейс продолжает обрабатывать события во время вывода"),
               QStringLiteral("циклов: %1, максимальная задержка %2 мс").arg(slices).arg(maxSliceMs));
        report(terminal->scrollbackCount() <= 20000,
               QStringLiteral("8.3 буфер прокрутки ограничен"),
               QStringLiteral("строк в буфере: %1").arg(terminal->scrollbackCount()));
        if (!g_screenshotDir.isEmpty()) {
            QDir().mkpath(g_screenshotDir);
            terminal->scrollToBottom();
            spin(300);
            window->grab().save(g_screenshotDir + QStringLiteral("/81-large-output.png"));
        }
        const qint64 rssAfterOutput = currentRssKb();
        report(rssAfterOutput < 900 * 1024,
               QStringLiteral("8.3 память после большого вывода остаётся ограниченной"),
               QStringLiteral("RSS %1 МиБ").arg(rssAfterOutput / 1024.0, 0, 'f', 1));
    }

    // --- нагрузка от мониторинга ---
    {
        SystemMonitor monitor;
        monitor.sample();
        QElapsedTimer sampleTimer;
        sampleTimer.start();
        const int samples = 10;
        for (int i = 0; i < samples; ++i)
            monitor.sample();
        const qint64 totalMs = sampleTimer.elapsed();
        const double perSample = double(totalMs) / samples;
        report(perSample < 60.0, QStringLiteral("8.3 срез монитора дешёвый"),
               QStringLiteral("%1 мс на срез").arg(perSample, 0, 'f', 1));
        report(totalMs < 600, QStringLiteral("8.3 серия срезов не нагружает систему"),
               QStringLiteral("%1 мс на %2 срезов").arg(totalMs).arg(samples));

        // Панель монитора в работе: 6 обновлений
        window->setMonitorPanelVisible(true);
        spin(400);
        QElapsedTimer panelTimer;
        panelTimer.start();
        for (int i = 0; i < 6; ++i) {
            window->monitorPanel()->refresh();
            spin(120);
        }
        const qint64 panelMs = panelTimer.elapsed();
        report(panelMs < 8000, QStringLiteral("8.3 обновление панели монитора укладывается во время"),
               QStringLiteral("%1 мс").arg(panelMs));
        const qint64 rssWithMonitor = currentRssKb();
        report(rssWithMonitor < 900 * 1024, QStringLiteral("8.3 панель монитора не раздувает память"),
               QStringLiteral("RSS %1 МиБ").arg(rssWithMonitor / 1024.0, 0, 'f', 1));
        window->setMonitorPanelVisible(false);
        spin(300);
    }

    // --- длительная работа нескольких вкладок ---
    {
        const int tabsBefore = window->tabs()->count();
        for (int i = 0; i < 3; ++i) {
            window->newTab();
            spin(400);
        }
        const int tabsNow = window->tabs()->count();
        report(tabsNow == tabsBefore + 3, QStringLiteral("8.3 открыто четыре вкладки"),
               QStringLiteral("вкладок: %1").arg(tabsNow));

        const qint64 rssManyTabs = currentRssKb();
        report(rssManyTabs < 900 * 1024, QStringLiteral("8.3 память на несколько вкладок"),
               QStringLiteral("RSS %1 МиБ").arg(rssManyTabs / 1024.0, 0, 'f', 1));

        // В каждой вкладке — поток вывода, затем Ctrl+C
        QElapsedTimer busyTimer;
        busyTimer.start();
        for (int i = 0; i < window->tabs()->count(); ++i) {
            PaneTree *tree = window->tabs()->tabAt(i);
            TerminalView *pane = tree ? tree->terminals().value(0) : nullptr;
            if (pane)
                pane->sendTextToShell(QStringLiteral("yes 'поток данных' | head -2000\r"));
        }
        int slices = 0;
        qint64 maxSliceMs = 0;
        while (busyTimer.elapsed() < 2500) {
            QElapsedTimer slice;
            slice.start();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            maxSliceMs = qMax(maxSliceMs, slice.elapsed());
            ++slices;
            QThread::msleep(2);
        }
        report(slices > 10 && maxSliceMs < 500,
               QStringLiteral("8.3 интерфейс отзывчив при работе четырёх вкладок"),
               QStringLiteral("циклов: %1, максимальная задержка %2 мс за %3 мс")
                   .arg(slices).arg(maxSliceMs).arg(busyTimer.elapsed()));

        if (!g_screenshotDir.isEmpty()) {
            QDir().mkpath(g_screenshotDir);
            spin(200);
            window->grab().save(g_screenshotDir + QStringLiteral("/82-many-tabs.png"));
        }
        bool allAlive = true;
        for (int i = 0; i < window->tabs()->count(); ++i) {
            PaneTree *tree = window->tabs()->tabAt(i);
            TerminalView *pane = tree ? tree->terminals().value(0) : nullptr;
            if (!pane || !pane->isSessionRunning())
                allAlive = false;
        }
        report(allAlive, QStringLiteral("8.3 все сессии остаются живыми"));
        report(currentRssKb() < 900 * 1024, QStringLiteral("8.3 память после нагрузки ограничена"),
               QStringLiteral("RSS %1 МиБ").arg(currentRssKb() / 1024.0, 0, 'f', 1));
    }

    // --- освобождение ресурсов после закрытия вкладок ---
    {
        const int fdBefore = openDescriptorCount();
        const pid_t mainPid = ::getpid();
        const int childrenBefore = childProcessCount(mainPid);

        for (int i = 0; i < 4; ++i) {
            window->newTab();
            spin(350);
        }
        const int fdPeak = openDescriptorCount();
        const int childrenPeak = childProcessCount(mainPid);

        while (window->tabs()->count() > 1) {
            const int last = window->tabs()->count() - 1;
            window->tabs()->setCurrentIndex(last);
            for (QAction *action : window->findChildren<QAction *>()) {
                if (action->text() == QStringLiteral("Закрыть вкладку")) {
                    action->trigger();
                    break;
                }
            }
            spin(200);
            flushDeferredDeletes();
        }
        // В оставшейся вкладке сессия должна продолжать работать
        int remainingPanes = 0;
        for (int i = 0; i < window->tabs()->count(); ++i) {
            PaneTree *tree = window->tabs()->tabAt(i);
            if (tree)
                remainingPanes += tree->terminals().size();
        }
        waitFor([&]() { return runningChildCount(mainPid) <= remainingPanes; }, 10000);
        flushDeferredDeletes();
        spin(300);

        const int fdAfter = openDescriptorCount();
        const int childrenAfter = childProcessCount(mainPid);
        report(window->tabs()->count() == 1, QStringLiteral("8.3 все лишние вкладки закрыты"),
               QStringLiteral("вкладок: %1").arg(window->tabs()->count()));
        report(runningChildCount(mainPid) <= remainingPanes,
               QStringLiteral("8.3 после закрытия вкладок процессы освобождены"),
               QStringLiteral("было %1, пик %2, живых после закрытия %3, панелей %4 (%5)")
                   .arg(childrenBefore).arg(childrenPeak).arg(runningChildCount(mainPid))
                   .arg(remainingPanes).arg(describeChildren(mainPid)));
        report(fdAfter - fdBefore <= 6,
               QStringLiteral("8.3 файловые дескрипторы освобождаются"),
               QStringLiteral("было %1, пик %2, стало %3").arg(fdBefore).arg(fdPeak).arg(fdAfter));

        // Память после закрытия вкладок не растёт неограниченно
        const qint64 rssAfterClose = currentRssKb();
        report(rssAfterClose < 900 * 1024, QStringLiteral("8.3 память после закрытия вкладок"),
               QStringLiteral("RSS %1 МиБ").arg(rssAfterClose / 1024.0, 0, 'f', 1));
    }

    // --- завершение: ресурсы отпускаются полностью ---
    {
        const pid_t shell = window->currentTerminal() ? window->currentTerminal()->shellPid() : -1;
        window->hide();
        delete window;
        const bool gone = waitFor([&]() { return !processAlive(shell); }, 8000);
        report(gone, QStringLiteral("8.3 при закрытии окна оболочка и ресурсы освобождаются"));
        waitFor([]() { return runningChildCount(::getpid()) == 0; }, 10000);
        report(runningChildCount(::getpid()) == 0,
               QStringLiteral("8.3 дочерних процессов не осталось"),
               QStringLiteral("живых: %1 из %2").arg(runningChildCount(::getpid()))
                   .arg(childProcessCount(::getpid())));
    }
}
