// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace BlackCore {

//! Один пункт командной палитры.
struct PaletteItem
{
    QString id;        //!< идентификатор действия: «tab.new», «theme:blackcore-dark», «cmd:<id>»
    QString title;     //!< основная строка: «Новая вкладка»
    QString subtitle;  //!< пояснение: сочетание клавиш, путь, текст команды
    QString group;     //!< группа: «Вкладки», «Темы», «SSH», «Команды»
    QString keywords;  //!< дополнительные слова для поиска

    bool isValid() const { return !id.isEmpty() && !title.isEmpty(); }
};

//! Командная палитра (`Ctrl+Shift+P`).
/*!
 *  Палитра ничего не выполняет сама: она фильтрует список пунктов и сообщает
 *  идентификатор выбранного через сигнал triggerRequested(). Список пунктов
 *  формирует окно — действия, рабочие пространства, темы, SSH-профили и
 *  сохранённые команды.
 */
class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    explicit CommandPalette(QWidget *parent = nullptr);

    void setItems(const QList<PaletteItem> &items);
    QList<PaletteItem> items() const { return m_items; }

    void setQuery(const QString &query);
    QString query() const;

    //! Отфильтрованный и отсортированный список по текущему запросу
    QList<PaletteItem> filteredItems() const;

    //! Идентификатор текущего выбранного пункта
    QString currentItemId() const;
    int currentRow() const;
    int visibleCount() const;

    //! Выбрать пункт по идентификатору (для меню и тестов)
    bool selectId(const QString &id);
    //! Активировать текущий пункт (как Enter)
    void activateCurrent();

    //! Показать палитру в окне с очищенным запросом и фокусом в поле ввода
    void open_(QWidget *parent);

    //! Оценка совпадения запроса и текста: -1 — не подходит
    static int matchScore(const QString &query, const QString &text);

    //! Подготовленный пункт: заголовок, группа и подпись в одной строке
    static QString displayText(const PaletteItem &item);

signals:
    //! Пользователь выбрал пункт (Enter или щелчок)
    void triggerRequested(const QString &id);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void acceptCurrent();

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
    QList<PaletteItem> m_items;
    QList<PaletteItem> m_filtered;
};

} // namespace BlackCore
