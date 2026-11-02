// SPDX-License-Identifier: MIT
#pragma once

#include "../core/GitInfo.h"
#include "../core/Project.h"
#include "../core/SshProfile.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTimer;
class QToolButton;

namespace BlackCore {

//! Панель инструментов разработчика: Git, SSH, проекты.
/*!
 *  Панель ничего не выполняет скрытно: Git-команды вставляются в приглашение
 *  активного терминала, стартовые команды проектов запускаются только после
 *  подтверждения, а SSH-подключение выполняется системным клиентом `ssh`.
 */
class DevPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DevPanel(QWidget *parent = nullptr);

    //! Каталог, для которого показывается Git-статус (обычно активная панель)
    void setWorkingDirectory(const QString &directory);
    QString workingDirectory() const { return m_directory; }

    //! Перечитать Git-статус, список проектов и SSH-профилей
    void refresh();

    //! Текущий статус Git (для тестов)
    GitStatus gitStatus() const { return m_gitStatus; }

    //! Открыть конкретную вкладку панели: 0 — Git, 1 — SSH, 2 — Проекты
    void showSection(int index);
    int currentSection() const;

    //! Выбрать SSH-профиль по идентификатору (для тестов и меню)
    void selectSshProfile(const QString &id);
    //! Выбрать проект по идентификатору
    void selectProject(const QString &id);

signals:
    //! Вставить команду в приглашение активного терминала (без Enter)
    void commandRequested(const QString &command);
    //! Выполнить команду сразу (после подтверждения в панели)
    void commandExecuted(const QString &command);
    //! Открыть каталог в новой вкладке
    void openDirectoryRequested(const QString &directory);
    //! Открыть каталог в активной панели (cd)
    void changeDirectoryRequested(const QString &directory);
    //! Подключиться по SSH-профилю
    void sshRequested(const QString &profileId);
    //! Открыть проект (раскладка или новая вкладка)
    void openProjectRequested(const QString &projectId);
    //! Уведомление для строки состояния
    void statusMessage(const QString &text);

private:
    QWidget *buildGitPage();
    QWidget *buildSshPage();
    QWidget *buildProjectsPage();

    void updateGit();
    void refreshSshList();
    void refreshProjectList();
    void fillSshForm(const SshProfile &profile);
    SshProfile profileFromForm() const;
    void saveSshProfile();
    void deleteSshProfile();
    void connectSsh(const QString &id);
    void openSelectedProject();
    void projectAction();

    QString m_directory;
    GitStatus m_gitStatus;

    QTabWidget *m_tabs = nullptr;

    // --- Git ---
    QLabel *m_gitRepoLabel = nullptr;
    QLabel *m_gitBranchLabel = nullptr;
    QLabel *m_gitStateLabel = nullptr;
    QLabel *m_gitRemoteLabel = nullptr;
    QListWidget *m_gitCommandList = nullptr;
    QPushButton *m_gitRootButton = nullptr;
    QPushButton *m_gitPanelButton = nullptr;

    // --- SSH ---
    QListWidget *m_sshList = nullptr;
    QLineEdit *m_sshName = nullptr;
    QLineEdit *m_sshHost = nullptr;
    QSpinBox *m_sshPort = nullptr;
    QLineEdit *m_sshUser = nullptr;
    QComboBox *m_sshKey = nullptr;
    QLabel *m_sshHint = nullptr;
    QPushButton *m_sshConnectButton = nullptr;
    QPushButton *m_sshSaveButton = nullptr;
    QPushButton *m_sshDeleteButton = nullptr;
    QString m_sshEditingId;

    // --- Проекты ---
    QListWidget *m_projectList = nullptr;
    QLabel *m_projectInfo = nullptr;
    QListWidget *m_projectCommands = nullptr;
    QPushButton *m_projectOpenButton = nullptr;
    QPushButton *m_projectTabButton = nullptr;
    QPushButton *m_projectRunButton = nullptr;

    QTimer *m_refreshTimer = nullptr;
};

} // namespace BlackCore
