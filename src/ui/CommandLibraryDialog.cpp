// SPDX-License-Identifier: MIT
#include "CommandLibraryDialog.h"

#include "DialogHeader.h"

#include "../core/Workspace.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <algorithm>
#include <QVBoxLayout>

namespace BlackCore {

CommandLibraryDialog::CommandLibraryDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("commandLibraryDialog"));
    setWindowTitle(tr("Библиотека команд"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    resize(880, 600);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(new DialogHeader(tr("Библиотека команд — BlackCore"), this));

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    outer->addLayout(layout, 1);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // --- левая часть: поиск, категория, список ---
    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(tr("Поиск по названию, описанию и команде"));
    m_search->setClearButtonEnabled(true);
    leftLayout->addWidget(m_search);

    m_categoryFilter = new QComboBox(left);
    m_categoryFilter->addItem(tr("Все категории"), QString());
    for (CommandCategory category : CommandLibrary::categories())
        m_categoryFilter->addItem(CommandLibrary::categoryName(category),
                                  CommandLibrary::categoryId(category));
    leftLayout->addWidget(m_categoryFilter);

    m_list = new QListWidget(left);
    m_list->setObjectName(QStringLiteral("libraryList"));
    m_list->setAlternatingRowColors(true);
    leftLayout->addWidget(m_list, 1);

    m_countLabel = new QLabel(left);
    m_countLabel->setProperty("role", QStringLiteral("secondary"));
    leftLayout->addWidget(m_countLabel);

    splitter->addWidget(left);

    // --- правая часть: редактор ---
    splitter->addWidget(buildEditor());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    m_list->setMinimumWidth(280);
    layout->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (QPushButton *closeButton = buttons->button(QDialogButtonBox::Close)) {
        closeButton->setText(tr("Закрыть"));
        closeButton->setIcon(QIcon());
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { rebuildList(); });
    connect(m_categoryFilter, &QComboBox::currentIndexChanged, this, [this](int) { rebuildList(); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { loadSelection(); });

    reload();
    clearForm();
}

QWidget *CommandLibraryDialog::buildEditor()
{
    auto *editor = new QWidget(this);
    auto *layout = new QVBoxLayout(editor);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(8);

    auto *formGroup = new QGroupBox(tr("Команда"), editor);
    auto *form = new QFormLayout(formGroup);

    m_name = new QLineEdit(formGroup);
    m_name->setObjectName(QStringLiteral("libraryName"));
    m_name->setPlaceholderText(tr("Например: Показать занятое место"));
    form->addRow(tr("Название:"), m_name);

    m_description = new QLineEdit(formGroup);
    m_description->setObjectName(QStringLiteral("libraryDescription"));
    m_description->setPlaceholderText(tr("Короткое описание, зачем эта команда"));
    form->addRow(tr("Описание:"), m_description);

    m_category = new QComboBox(formGroup);
    m_category->setObjectName(QStringLiteral("libraryCategory"));
    for (CommandCategory category : CommandLibrary::categories())
        m_category->addItem(CommandLibrary::categoryName(category), CommandLibrary::categoryId(category));
    form->addRow(tr("Категория:"), m_category);

    m_command = new QPlainTextEdit(formGroup);
    m_command->setObjectName(QStringLiteral("libraryCommand"));
    m_command->setPlaceholderText(tr("Команда или несколько строк сценария"));
    m_command->setMinimumHeight(110);
    form->addRow(tr("Команда:"), m_command);

    auto *dirRow = new QWidget(formGroup);
    auto *dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    m_directory = new QLineEdit(dirRow);
    m_directory->setObjectName(QStringLiteral("libraryDirectory"));
    m_directory->setPlaceholderText(tr("пусто — каталог активной панели терминала"));
    auto *browse = new QPushButton(tr("Обзор…"), dirRow);
    dirLayout->addWidget(m_directory, 1);
    dirLayout->addWidget(browse);
    form->addRow(tr("Рабочий каталог:"), dirRow);

    m_confirm = new QCheckBox(tr("Спрашивать подтверждение перед выполнением"), formGroup);
    m_confirm->setObjectName(QStringLiteral("libraryConfirm"));
    m_confirm->setChecked(true);
    form->addRow(QString(), m_confirm);

    m_dangerLabel = new QLabel(formGroup);
    m_dangerLabel->setWordWrap(true);
    m_dangerLabel->setProperty("role", QStringLiteral("error"));
    m_dangerLabel->setVisible(false);
    form->addRow(QString(), m_dangerLabel);

    layout->addWidget(formGroup);

    auto *buttons = new QHBoxLayout;
    auto *newButton = new QPushButton(tr("Новая"), editor);
    m_saveButton = new QPushButton(tr("Сохранить"), editor);
    m_deleteButton = new QPushButton(tr("Удалить"), editor);
    m_runButton = new QPushButton(tr("Выполнить"), editor);
    m_runButton->setObjectName(QStringLiteral("libraryRun"));
    buttons->addWidget(newButton);
    buttons->addWidget(m_saveButton);
    buttons->addWidget(m_deleteButton);
    buttons->addStretch(1);
    buttons->addWidget(m_runButton);
    layout->addLayout(buttons);

    auto *hint = new QLabel(tr("Сохранённые команды хранятся в настройках BlackCore и попадают "
                               "в экспорт конфигурации. Пароли в командах не хранятся: при "
                               "записи в журнал они маскируются. Изменяющие команды выполняются "
                               "только после подтверждения с показом текста."),
                            editor);
    hint->setWordWrap(true);
    hint->setProperty("role", QStringLiteral("secondary"));
    layout->addWidget(hint);
    layout->addStretch(1);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString directory =
            QFileDialog::getExistingDirectory(this, tr("Рабочий каталог"), m_directory->text());
        if (!directory.isEmpty())
            m_directory->setText(directory);
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        m_list->setCurrentRow(-1);
        clearForm();
        m_name->setFocus();
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this]() { saveCurrent(); });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() { deleteCurrent(); });
    connect(m_runButton, &QPushButton::clicked, this, [this]() {
        if (!saveCurrent())
            return;
        emit runRequested(m_editing);
    });
    connect(m_command, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updating)
            return;
        const bool danger = CommandLibrary::isDestructive(m_command->toPlainText());
        m_dangerLabel->setVisible(danger);
        if (danger)
            m_dangerLabel->setText(tr("Команда может изменить систему или удалить данные — "
                                       "перед запуском будет показан её текст."));
    });

    return editor;
}

void CommandLibraryDialog::reload()
{
    rebuildList();
}

void CommandLibraryDialog::rebuildList()
{
    const QString query = m_search->text();
    const QString categoryId = m_categoryFilter->currentData().toString();

    QList<SavedCommand> commands = CommandLibrary::commands();
    // Свежие и часто используемые — выше
    std::stable_sort(commands.begin(), commands.end(), [](const SavedCommand &a, const SavedCommand &b) {
        if (a.useCount != b.useCount)
            return a.useCount > b.useCount;
        return a.lastUsedAt > b.lastUsedAt;
    });

    m_visible.clear();
    m_list->blockSignals(true);
    m_list->clear();
    const QString needle = query.trimmed().toCaseFolded();
    for (const SavedCommand &command : commands) {
        if (!categoryId.isEmpty() && command.categoryId() != categoryId)
            continue;
        if (!needle.isEmpty()) {
            const QString haystack = (command.name + QLatin1Char(' ') + command.description
                                      + QLatin1Char(' ') + command.command)
                                         .toCaseFolded();
            if (!haystack.contains(needle))
                continue;
        }
        m_visible.append(command);
        auto *item = new QListWidgetItem(QStringLiteral("%1   ·   %2")
                                            .arg(command.name, command.categoryName()),
                                        m_list);
        item->setData(Qt::UserRole, command.id);
        item->setToolTip(command.description.isEmpty() ? command.command
                                                       : command.description + QLatin1Char('\n') + command.command);
    }
    m_list->blockSignals(false);
    if (m_list->count() > 0 && m_list->currentRow() < 0)
        m_list->setCurrentRow(0);
    m_countLabel->setText(tr("Команд: %1 · всего в библиотеке: %2")
                              .arg(m_visible.size())
                              .arg(CommandLibrary::commands().size()));
}

int CommandLibraryDialog::visibleCount() const
{
    return m_visible.size();
}

void CommandLibraryDialog::setFilter(const QString &query, int categoryIndex)
{
    m_search->setText(query);
    if (categoryIndex >= 0 && categoryIndex < m_categoryFilter->count())
        m_categoryFilter->setCurrentIndex(categoryIndex);
    rebuildList();
}

QString CommandLibraryDialog::currentCommandId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool CommandLibraryDialog::selectCommand(const QString &id)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toString() == id) {
            m_list->setCurrentRow(row);
            return true;
        }
    }
    return false;
}

void CommandLibraryDialog::loadSelection()
{
    const QString id = currentCommandId();
    if (id.isEmpty()) {
        return;
    }
    const SavedCommand command = CommandLibrary::command(id);
    if (!command.isValid())
        return;
    fillForm(command);
}

void CommandLibraryDialog::fillForm(const SavedCommand &command)
{
    m_updating = true;
    m_editing = command;
    m_name->setText(command.name);
    m_description->setText(command.description);
    const int index = m_category->findData(CommandLibrary::categoryId(command.category));
    m_category->setCurrentIndex(index >= 0 ? index : 0);
    m_command->setPlainText(command.command);
    m_directory->setText(command.workingDirectory);
    m_confirm->setChecked(command.needsConfirmation);
    m_updating = false;

    const bool danger = CommandLibrary::isDestructive(command.command);
    m_dangerLabel->setVisible(danger);
    if (danger)
        m_dangerLabel->setText(tr("Команда может изменить систему или удалить данные — "
                                   "перед запуском будет показан её текст."));
}

SavedCommand CommandLibraryDialog::formCommand() const
{
    SavedCommand command = m_editing;
    command.name = m_name->text().trimmed();
    command.description = m_description->text().trimmed();
    command.category = CommandLibrary::categoryFromId(m_category->currentData().toString());
    command.command = m_command->toPlainText().trimmed();
    command.workingDirectory = m_directory->text().trimmed();
    command.needsConfirmation = m_confirm->isChecked();
    return command;
}

bool CommandLibraryDialog::saveCurrent()
{
    SavedCommand command = formCommand();
    const QString problem = CommandLibrary::validate(command);
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, tr("Библиотека команд"), problem);
        return false;
    }
    if (!CommandLibrary::saveCommand(command))
        return false;
    m_editing = command;
    rebuildList();
    selectCommand(command.id);
    emit libraryChanged();
    return true;
}

void CommandLibraryDialog::deleteCurrent()
{
    const QString id = currentCommandId();
    if (id.isEmpty())
        return;
    const SavedCommand command = CommandLibrary::command(id);
    const auto answer = QMessageBox::question(
        this, tr("Удалить команду?"),
        tr("Удалить «%1» из библиотеки?\n\nКоманда в терминале и ваши файлы не затрагиваются.")
            .arg(command.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    CommandLibrary::removeCommand(id);
    rebuildList();
    clearForm();
    emit libraryChanged();
}

void CommandLibraryDialog::clearForm()
{
    m_updating = true;
    m_editing = SavedCommand();
    m_name->clear();
    m_description->clear();
    m_category->setCurrentIndex(0);
    m_command->clear();
    m_directory->clear();
    m_confirm->setChecked(true);
    m_dangerLabel->setVisible(false);
    m_updating = false;
}

} // namespace BlackCore
