// SPDX-License-Identifier: MIT
#pragma once

#include "../core/CommandLibrary.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace BlackCore {

//! Библиотека команд: создание, редактирование и запуск сохранённых команд.
/*!
 *  Диалог только редактирует записи и сообщает о желании выполнить команду
 *  (runRequested). Подтверждение и выполнение — задача окна, поэтому диалог
 *  можно безопасно использовать и в тестах.
 */
class CommandLibraryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CommandLibraryDialog(QWidget *parent = nullptr);

    //! Перечитать список из настроек
    void reload();

    //! Выбрать команду по идентификатору
    bool selectCommand(const QString &id);
    QString currentCommandId() const;

    //! Показать в списке только выбранную категорию/запрос
    void setFilter(const QString &query, int categoryIndex = 0);

    //! Заполнить форму значениями (для тестов и палитры)
    void fillForm(const SavedCommand &command);
    SavedCommand formCommand() const;

    //! Сохранить текущую форму; возвращает false и показывает ошибку
    bool saveCurrent();

    int visibleCount() const;
    QList<SavedCommand> visibleCommands() const { return m_visible; }

signals:
    //! Пользователь просит выполнить команду (подтверждение — в окне)
    void runRequested(const BlackCore::SavedCommand &command);
    //! Библиотека изменилась
    void libraryChanged();

private:
    QWidget *buildEditor();
    void rebuildList();
    void loadSelection();
    void clearForm();
    void deleteCurrent();

    QLineEdit *m_search = nullptr;
    QComboBox *m_categoryFilter = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_countLabel = nullptr;

    QLineEdit *m_name = nullptr;
    QLineEdit *m_description = nullptr;
    QComboBox *m_category = nullptr;
    QPlainTextEdit *m_command = nullptr;
    QLineEdit *m_directory = nullptr;
    QCheckBox *m_confirm = nullptr;
    QLabel *m_dangerLabel = nullptr;

    QPushButton *m_saveButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_runButton = nullptr;

    QList<SavedCommand> m_visible;
    SavedCommand m_editing;
    bool m_updating = false;
};

} // namespace BlackCore
