// SPDX-License-Identifier: MIT
#include "DevPanel.h"

#include "../core/Logger.h"
#include "../core/Theme.h"
#include "../core/Workspace.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace BlackCore {
namespace {

QLabel *infoLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", QStringLiteral("secondary"));
    label->setWordWrap(true);
    return label;
}

QLabel *infoLabel(QWidget *parent)
{
    return infoLabel(QString(), parent);
}

} // namespace

DevPanel::DevPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("devPanel"));
    setMinimumWidth(280);
    setMaximumWidth(460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildGitPage(), tr("Git"));
    m_tabs->addTab(buildSshPage(), tr("SSH"));
    m_tabs->addTab(buildProjectsPage(), tr("Проекты"));
    layout->addWidget(m_tabs, 1);

    // Статус Git обновляется не чаще раза в 3 секунды и только когда панель видна
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(3000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (isVisible() && m_tabs->currentIndex() == 0)
            updateGit();
    });
    m_refreshTimer->start();

    refresh();
}

// ---------------------------------------------------------------------------
// Git
// ---------------------------------------------------------------------------

QWidget *DevPanel::buildGitPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 4);
    layout->setSpacing(8);

    auto *infoGroup = new QGroupBox(tr("Репозиторий"), page);
    auto *infoLayout = new QVBoxLayout(infoGroup);
    m_gitRepoLabel = new QLabel(infoGroup);
    m_gitRepoLabel->setWordWrap(true);
    m_gitRepoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_gitBranchLabel = new QLabel(infoGroup);
    m_gitStateLabel = infoLabel(infoGroup);
    m_gitRemoteLabel = infoLabel(infoGroup);
    infoLayout->addWidget(m_gitRepoLabel);
    infoLayout->addWidget(m_gitBranchLabel);
    infoLayout->addWidget(m_gitStateLabel);
    infoLayout->addWidget(m_gitRemoteLabel);
    layout->addWidget(infoGroup);

    auto *buttonsRow = new QHBoxLayout;
    m_gitRootButton = new QPushButton(tr("Перейти в корень"), page);
    m_gitRootButton->setToolTip(tr("Выполнить cd в корень репозитория в активной панели"));
    m_gitPanelButton = new QPushButton(tr("Панель Git"), page);
    m_gitPanelButton->setToolTip(tr("Открыть отдельную панель терминала с git status"));
    buttonsRow->addWidget(m_gitRootButton);
    buttonsRow->addWidget(m_gitPanelButton);
    layout->addLayout(buttonsRow);

    auto *commandsGroup = new QGroupBox(tr("Быстрые команды"), page);
    auto *commandsLayout = new QVBoxLayout(commandsGroup);
    m_gitCommandList = new QListWidget(commandsGroup);
    m_gitCommandList->addItems(GitService::quickCommands());
    m_gitCommandList->setAlternatingRowColors(true);
    commandsLayout->addWidget(m_gitCommandList);
    commandsLayout->addWidget(infoLabel(tr("Команда вставляется в приглашение терминала — "
                                           "вы видите её и подтверждаете Enter."),
                                        commandsGroup));
    layout->addWidget(commandsGroup, 1);

    connect(m_gitRootButton, &QPushButton::clicked, this, [this]() {
        if (m_gitStatus.valid)
            emit changeDirectoryRequested(m_gitStatus.repositoryRoot);
    });
    connect(m_gitPanelButton, &QPushButton::clicked, this, [this]() {
        if (m_gitStatus.valid) {
            emit commandRequested(QStringLiteral("cd %1 && git status -sb")
                                      .arg(m_gitStatus.repositoryRoot));
            emit statusMessage(tr("Открыта команда Git для %1").arg(m_gitStatus.repositoryRoot));
        }
    });
    connect(m_gitCommandList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item && GitService::isAllowedCommand(item->text()))
            emit commandRequested(item->text());
    });
    connect(m_gitCommandList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item && GitService::isAllowedCommand(item->text()))
            emit commandRequested(item->text());
    });

    return page;
}

void DevPanel::updateGit()
{
    m_gitStatus = GitService::status(m_directory);

    if (m_directory.isEmpty()) {
        m_gitRepoLabel->setText(tr("Каталог не определён"));
        m_gitBranchLabel->clear();
        m_gitStateLabel->clear();
        m_gitRemoteLabel->clear();
        m_gitRootButton->setEnabled(false);
        m_gitPanelButton->setEnabled(false);
        m_gitCommandList->setEnabled(false);
        return;
    }

    if (!m_gitStatus.valid) {
        m_gitRepoLabel->setText(tr("Не Git-репозиторий"));
        m_gitBranchLabel->setText(QDir::toNativeSeparators(m_directory));
        m_gitStateLabel->setText(GitService::isGitAvailable()
                                     ? tr("Git установлен — можно инициализировать репозиторий")
                                     : tr("Git не установлен в системе"));
        m_gitRemoteLabel->clear();
        m_gitRootButton->setEnabled(false);
        m_gitPanelButton->setEnabled(true);
        m_gitCommandList->setEnabled(false);
        return;
    }

    m_gitRepoLabel->setText(QDir::toNativeSeparators(m_gitStatus.repositoryRoot));
    m_gitRepoLabel->setToolTip(m_gitStatus.repositoryRoot);

    QString branchText = tr("Ветка: %1").arg(m_gitStatus.branch);
    if (m_gitStatus.isDetached())
        branchText = tr("Отделённое состояние (detached HEAD)");
    if (!m_gitStatus.lastCommit.isEmpty())
        branchText += QStringLiteral("  ·  %1").arg(m_gitStatus.lastCommit);
    m_gitBranchLabel->setText(branchText);

    const BrandPalette colors = ThemeManager::colors();
    if (m_gitStatus.isClean()) {
        m_gitStateLabel->setText(tr("✓ Нет незакоммиченных изменений"));
        m_gitStateLabel->setStyleSheet(QStringLiteral("color: %1;").arg(colors.success.name()));
    } else {
        m_gitStateLabel->setText(tr("● Изменения: %1 (индекс %2, рабочая копия %3, новых %4, "
                                    "конфликтов %5)")
                                     .arg(m_gitStatus.changedFiles())
                                     .arg(m_gitStatus.staged)
                                     .arg(m_gitStatus.modified)
                                     .arg(m_gitStatus.untracked)
                                     .arg(m_gitStatus.conflicted));
        m_gitStateLabel->setStyleSheet(QStringLiteral("color: %1;").arg(colors.goldLight.name()));
    }

    if (m_gitStatus.ahead > 0 || m_gitStatus.behind > 0) {
        m_gitRemoteLabel->setText(tr("Относительно upstream: впереди %1, позади %2")
                                      .arg(m_gitStatus.ahead)
                                      .arg(m_gitStatus.behind));
    } else {
        m_gitRemoteLabel->clear();
    }

    m_gitRootButton->setEnabled(true);
    m_gitPanelButton->setEnabled(true);
    m_gitCommandList->setEnabled(true);
}

// ---------------------------------------------------------------------------
// SSH
// ---------------------------------------------------------------------------

QWidget *DevPanel::buildSshPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 4);
    layout->setSpacing(8);

    auto *listGroup = new QGroupBox(tr("Подключения"), page);
    auto *listLayout = new QVBoxLayout(listGroup);
    m_sshList = new QListWidget(listGroup);
    m_sshList->setMaximumHeight(120);
    listLayout->addWidget(m_sshList);
    layout->addWidget(listGroup);

    auto *formGroup = new QGroupBox(tr("Профиль"), page);
    auto *form = new QFormLayout(formGroup);
    m_sshName = new QLineEdit(formGroup);
    m_sshName->setPlaceholderText(tr("Рабочий сервер"));
    form->addRow(tr("Имя:"), m_sshName);

    m_sshHost = new QLineEdit(formGroup);
    m_sshHost->setPlaceholderText(QStringLiteral("example.com или 192.0.2.10"));
    form->addRow(tr("Адрес:"), m_sshHost);

    m_sshPort = new QSpinBox(formGroup);
    m_sshPort->setRange(1, 65535);
    m_sshPort->setValue(22);
    form->addRow(tr("Порт:"), m_sshPort);

    m_sshUser = new QLineEdit(formGroup);
    m_sshUser->setPlaceholderText(tr("имя пользователя"));
    form->addRow(tr("Пользователь:"), m_sshUser);

    m_sshKey = new QComboBox(formGroup);
    m_sshKey->setEditable(false);
    form->addRow(tr("Ключ:"), m_sshKey);

    m_sshHint = infoLabel(tr("Пароли и приватные ключи BlackCore не хранит: подключение "
                             "выполняет системный ssh, ключи и ssh-agent берутся из ~/.ssh. "
                             "Пароль спрашивает сам ssh в терминале."),
                          formGroup);
    form->addRow(QString(), m_sshHint);
    layout->addWidget(formGroup);

    auto *buttonsRow = new QHBoxLayout;
    m_sshConnectButton = new QPushButton(tr("Подключиться"), page);
    m_sshSaveButton = new QPushButton(tr("Сохранить"), page);
    m_sshDeleteButton = new QPushButton(tr("Удалить"), page);
    buttonsRow->addWidget(m_sshConnectButton);
    buttonsRow->addWidget(m_sshSaveButton);
    buttonsRow->addWidget(m_sshDeleteButton);
    layout->addLayout(buttonsRow);
    layout->addStretch(1);

    connect(m_sshList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        const SshProfile profile = SshManager::profile(item->data(Qt::UserRole).toString());
        if (profile.isValid())
            fillSshForm(profile);
    });
    connect(m_sshList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item)
            connectSsh(item->data(Qt::UserRole).toString());
    });
    connect(m_sshConnectButton, &QPushButton::clicked, this, [this]() {
        const QString id = m_sshEditingId;
        if (!id.isEmpty()) {
            connectSsh(id);
            return;
        }
        const SshProfile profile = profileFromForm();
        QString error;
        const QString command = SshManager::buildCommand(profile, &error);
        if (command.isEmpty()) {
            QMessageBox::warning(this, tr("SSH"), error);
            return;
        }
        emit commandRequested(command);
    });
    connect(m_sshSaveButton, &QPushButton::clicked, this, &DevPanel::saveSshProfile);
    connect(m_sshDeleteButton, &QPushButton::clicked, this, &DevPanel::deleteSshProfile);

    return page;
}

void DevPanel::refreshSshList()
{
    const QString selected = m_sshEditingId;
    m_sshList->clear();

    for (const SshProfile &profile : SshManager::profiles()) {
        const QString label = profile.user.isEmpty()
            ? QStringLiteral("%1 — %2:%3").arg(profile.name, profile.host).arg(profile.port)
            : QStringLiteral("%1 — %2@%3:%4")
                  .arg(profile.name, profile.user, profile.host)
                  .arg(profile.port);
        auto *item = new QListWidgetItem(label, m_sshList);
        item->setData(Qt::UserRole, profile.id);
        item->setToolTip(profile.identityFile.isEmpty()
                             ? tr("Ключ по умолчанию из ~/.ssh/config")
                             : profile.identityFile);
        if (profile.id == selected)
            m_sshList->setCurrentItem(item);
    }

    const QStringList keys = SshManager::availableKeys();
    const QString currentKey = m_sshKey->currentData().toString();
    m_sshKey->clear();
    m_sshKey->addItem(tr("Из ~/.ssh/config (по умолчанию)"), QString());
    for (const QString &key : keys)
        m_sshKey->addItem(QFileInfo(key).fileName(), key);
    const int keyIndex = m_sshKey->findData(currentKey);
    if (keyIndex >= 0)
        m_sshKey->setCurrentIndex(keyIndex);

    if (keys.isEmpty()) {
        m_sshKey->setToolTip(tr("В ~/.ssh нет файлов ключей — ssh спросит пароль или "
                                "использует ssh-agent"));
    }
}

void DevPanel::fillSshForm(const SshProfile &profile)
{
    m_sshEditingId = profile.id;
    m_sshName->setText(profile.name);
    m_sshHost->setText(profile.host);
    m_sshPort->setValue(profile.port);
    m_sshUser->setText(profile.user);

    int keyIndex = m_sshKey->findData(profile.identityFile);
    if (keyIndex < 0 && !profile.identityFile.isEmpty()) {
        m_sshKey->addItem(QFileInfo(profile.identityFile).fileName(), profile.identityFile);
        keyIndex = m_sshKey->count() - 1;
    }
    m_sshKey->setCurrentIndex(qMax(0, keyIndex));
    m_sshConnectButton->setText(tr("Подключиться"));
}

SshProfile DevPanel::profileFromForm() const
{
    SshProfile profile;
    profile.id = m_sshEditingId;
    profile.name = m_sshName->text().trimmed();
    profile.host = m_sshHost->text().trimmed();
    profile.port = m_sshPort->value();
    profile.user = m_sshUser->text().trimmed();
    profile.identityFile = m_sshKey->currentData().toString();
    return profile;
}

void DevPanel::saveSshProfile()
{
    SshProfile profile = profileFromForm();
    if (profile.id.isEmpty())
        profile.id = SshManager::makeId(profile.name);

    const QString problem = SshManager::validate(profile);
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, tr("SSH-профиль"), problem);
        return;
    }

    SshManager::saveProfile(profile);
    m_sshEditingId = profile.id;
    refreshSshList();
    emit statusMessage(tr("Профиль «%1» сохранён").arg(profile.name));
}

void DevPanel::deleteSshProfile()
{
    if (m_sshEditingId.isEmpty())
        return;

    const SshProfile profile = SshManager::profile(m_sshEditingId);
    const auto answer = QMessageBox::question(
        this, tr("Удалить подключение?"),
        tr("Удалить профиль «%1»? Ключи и настройки в ~/.ssh не затрагиваются.")
            .arg(profile.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    SshManager::removeProfile(m_sshEditingId);
    m_sshEditingId.clear();
    m_sshName->clear();
    m_sshHost->clear();
    m_sshPort->setValue(22);
    m_sshUser->clear();
    m_sshKey->setCurrentIndex(0);
    refreshSshList();
    emit statusMessage(tr("Профиль «%1» удалён").arg(profile.name));
}

void DevPanel::selectSshProfile(const QString &id)
{
    for (int i = 0; i < m_sshList->count(); ++i) {
        if (m_sshList->item(i)->data(Qt::UserRole).toString() == id) {
            m_sshList->setCurrentRow(i);
            break;
        }
    }
    const SshProfile profile = SshManager::profile(id);
    if (profile.isValid())
        fillSshForm(profile);
    m_tabs->setCurrentIndex(1);
}

void DevPanel::connectSsh(const QString &id)
{
    const SshProfile profile = SshManager::profile(id);
    if (!profile.isValid()) {
        QMessageBox::warning(this, tr("SSH"), tr("Профиль не найден."));
        return;
    }

    QString error;
    const QString command = SshManager::buildCommand(profile, &error);
    if (command.isEmpty()) {
        QMessageBox::warning(this, tr("SSH"), error);
        return;
    }

    emit sshRequested(profile.id);
}

// ---------------------------------------------------------------------------
// Проекты
// ---------------------------------------------------------------------------

QWidget *DevPanel::buildProjectsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 8, 4, 4);
    layout->setSpacing(8);

    auto *listGroup = new QGroupBox(tr("Избранные проекты"), page);
    auto *listLayout = new QVBoxLayout(listGroup);
    m_projectList = new QListWidget(listGroup);
    m_projectList->setMaximumHeight(140);
    m_projectInfo = infoLabel(QString(), listGroup);
    listLayout->addWidget(m_projectList);
    listLayout->addWidget(m_projectInfo);
    layout->addWidget(listGroup);

    auto *actionsRow = new QHBoxLayout;
    m_projectOpenButton = new QPushButton(tr("Открыть"), page);
    m_projectOpenButton->setToolTip(tr("Открыть рабочее пространство проекта"));
    m_projectTabButton = new QPushButton(tr("В новой вкладке"), page);
    m_projectTabButton->setToolTip(tr("Открыть терминал в каталоге проекта"));
    auto *addButton = new QPushButton(tr("Добавить…"), page);
    actionsRow->addWidget(m_projectOpenButton);
    actionsRow->addWidget(m_projectTabButton);
    actionsRow->addWidget(addButton);
    layout->addLayout(actionsRow);

    auto *commandsGroup = new QGroupBox(tr("Стартовые команды"), page);
    auto *commandsLayout = new QVBoxLayout(commandsGroup);
    m_projectCommands = new QListWidget(commandsGroup);
    commandsLayout->addWidget(m_projectCommands);
    m_projectRunButton = new QPushButton(tr("Выполнить команду"), commandsGroup);
    m_projectRunButton->setToolTip(tr("Команда выполняется в терминале после подтверждения"));
    commandsLayout->addWidget(m_projectRunButton);
    layout->addWidget(commandsGroup, 1);

    connect(m_projectList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item) {
                m_projectCommands->clear();
                if (!item)
                    return;
                const Project project = ProjectManager::project(item->data(Qt::UserRole).toString());
                const QString stack = ProjectManager::detectStack(project.path);
                m_projectInfo->setText(tr("%1\nСтек: %2")
                                           .arg(QDir::toNativeSeparators(project.path),
                                                stack.isEmpty() ? tr("не определён") : stack));

                // Сохранённые команды + подсказки для распознанного стека
                QList<ProjectCommand> commands = project.commands;
                if (commands.isEmpty())
                    commands = ProjectManager::suggestedCommands(project.path);
                for (const ProjectCommand &command : commands) {
                    auto *commandItem = new QListWidgetItem(
                        QStringLiteral("%1 —  %2").arg(command.name, command.command),
                        m_projectCommands);
                    commandItem->setData(Qt::UserRole, command.command);
                    commandItem->setData(Qt::UserRole + 1, command.needsConfirmation);
                    commandItem->setToolTip(command.command);
                }
            });
    connect(m_projectList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        openSelectedProject();
    });
    connect(m_projectOpenButton, &QPushButton::clicked, this, &DevPanel::openSelectedProject);
    connect(m_projectTabButton, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = m_projectList->currentItem();
        if (!item)
            return;
        const Project project = ProjectManager::project(item->data(Qt::UserRole).toString());
        if (project.isValid())
            emit openDirectoryRequested(project.path);
    });
    connect(m_projectRunButton, &QPushButton::clicked, this, &DevPanel::projectAction);
    connect(m_projectCommands, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        projectAction();
    });
    connect(addButton, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Каталог проекта"), QDir::homePath());
        if (directory.isEmpty())
            return;

        const QFileInfo info(directory);
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, tr("Новый проект"), tr("Название проекта:"), QLineEdit::Normal,
            info.fileName().isEmpty() ? directory : info.fileName(), &accepted);
        if (!accepted || name.trimmed().isEmpty())
            return;

        Project project;
        project.id = ProjectManager::makeId(name);
        project.name = name.trimmed();
        project.path = directory;
        project.layout = ProjectManager::defaultLayout(directory);
        project.commands = ProjectManager::suggestedCommands(directory);

        const QString problem = ProjectManager::validate(project);
        if (!problem.isEmpty()) {
            QMessageBox::warning(this, tr("Проект"), problem);
            return;
        }

        ProjectManager::saveProject(project);
        refreshProjectList();
        emit statusMessage(tr("Проект «%1» добавлен").arg(project.name));
    });

    return page;
}

void DevPanel::refreshProjectList()
{
    const QString selected = m_projectList->currentItem()
        ? m_projectList->currentItem()->data(Qt::UserRole).toString()
        : QString();
    m_projectList->clear();

    for (const Project &project : ProjectManager::favorites()) {
        auto *item = new QListWidgetItem(project.name, m_projectList);
        item->setData(Qt::UserRole, project.id);
        item->setToolTip(project.path);
        if (project.id == selected)
            m_projectList->setCurrentItem(item);
    }
}

void DevPanel::selectProject(const QString &id)
{
    for (int i = 0; i < m_projectList->count(); ++i) {
        if (m_projectList->item(i)->data(Qt::UserRole).toString() == id) {
            m_projectList->setCurrentRow(i);
            break;
        }
    }
    m_tabs->setCurrentIndex(2);
}

void DevPanel::openSelectedProject()
{
    QListWidgetItem *item = m_projectList->currentItem();
    if (!item)
        return;
    emit openProjectRequested(item->data(Qt::UserRole).toString());
}

void DevPanel::projectAction()
{
    QListWidgetItem *item = m_projectCommands->currentItem();
    if (!item)
        return;

    const QString command = item->data(Qt::UserRole).toString();
    const bool needsConfirmation = item->data(Qt::UserRole + 1).toBool();

    if (needsConfirmation) {
        const auto answer = QMessageBox::question(
            this, tr("Выполнить команду?"),
            tr("В терминале проекта будет выполнено:\n\n%1\n\nПродолжить?").arg(command),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    emit commandExecuted(command);
}

// ---------------------------------------------------------------------------
// Общее
// ---------------------------------------------------------------------------

void DevPanel::setWorkingDirectory(const QString &directory)
{
    if (m_directory == directory)
        return;
    m_directory = directory;
    if (m_tabs->currentIndex() == 0)
        updateGit();
}

void DevPanel::refresh()
{
    updateGit();
    refreshSshList();
    refreshProjectList();
}

void DevPanel::showSection(int index)
{
    if (index < 0 || index >= m_tabs->count())
        return;
    m_tabs->setCurrentIndex(index);
    if (index == 0)
        updateGit();
    else if (index == 1)
        refreshSshList();
    else
        refreshProjectList();
}

int DevPanel::currentSection() const
{
    return m_tabs->currentIndex();
}

} // namespace BlackCore
