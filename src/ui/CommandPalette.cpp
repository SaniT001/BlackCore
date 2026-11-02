// SPDX-License-Identifier: MIT
#include "CommandPalette.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace BlackCore {
namespace {

constexpr int kMaxVisible = 200;

QString normalize(const QString &text)
{
    return text.toCaseFolded().simplified();
}

//! Позиция вхождения подстроки: -1, если нет
int indexOf(const QString &haystack, const QString &needle)
{
    return haystack.indexOf(needle);
}

//! Совпадение по первым буквам слов («нв» → «Новая вкладка»)
bool matchesInitials(const QString &query, const QString &text)
{
    if (query.size() < 2)
        return false;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < query.size())
        return false;
    int word = 0;
    for (const QChar &character : query) {
        bool found = false;
        while (word < words.size()) {
            if (words.at(word).startsWith(character)) {
                found = true;
                ++word;
                break;
            }
            ++word;
        }
        if (!found)
            return false;
    }
    return true;
}

} // namespace

CommandPalette::CommandPalette(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("commandPalette"));
    setWindowTitle(tr("Палитра команд"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    resize(620, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("paletteSearch"));
    m_search->setPlaceholderText(tr("Что сделать? Вкладки, панели, темы, SSH, команды…"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("paletteList"));
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->setWordWrap(false);
    layout->addWidget(m_list, 1);

    m_hint = new QLabel(tr("↑ ↓ — выбор, Enter — выполнить, Esc — закрыть"), this);
    m_hint->setProperty("role", QStringLiteral("secondary"));
    layout->addWidget(m_hint);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { rebuild(); });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() { acceptCurrent(); });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { acceptCurrent(); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { acceptCurrent(); });
}

void CommandPalette::setItems(const QList<PaletteItem> &items)
{
    m_items.clear();
    for (const PaletteItem &item : items) {
        if (item.isValid())
            m_items.append(item);
    }
    rebuild();
}

void CommandPalette::setQuery(const QString &query)
{
    m_search->setText(query);
    rebuild();
}

QString CommandPalette::query() const
{
    return m_search->text();
}

int CommandPalette::matchScore(const QString &query, const QString &text)
{
    const QString needle = normalize(query);
    const QString haystack = normalize(text);
    if (needle.isEmpty())
        return 0;
    if (haystack.isEmpty())
        return -1;

    if (haystack == needle)
        return 1000;
    if (haystack.startsWith(needle))
        return 800 - qMin(200, haystack.size());

    const int position = indexOf(haystack, needle);
    if (position >= 0)
        return 600 - qMin(200, position) - qMin(100, haystack.size() - needle.size());

    // Совпадение по началу слов
    const QStringList haystackWords = haystack.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &word : haystackWords) {
        if (word.startsWith(needle))
            return 400;
    }

    if (matchesInitials(needle, haystack))
        return 200;

    // Все слова запроса встречаются в тексте в любом порядке
    bool all = true;
    for (const QString &word : needle.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!haystack.contains(word))
            all = false;
    }
    return all ? 100 : -1;
}

QString CommandPalette::displayText(const PaletteItem &item)
{
    if (item.subtitle.isEmpty() && item.group.isEmpty())
        return item.title;
    if (item.subtitle.isEmpty())
        return QStringLiteral("%1   ·   %2").arg(item.title, item.group);
    return QStringLiteral("%1   —   %2   ·   %3").arg(item.title, item.subtitle, item.group);
}

QList<PaletteItem> CommandPalette::filteredItems() const
{
    return m_filtered;
}

void CommandPalette::rebuild()
{
    const QString query = m_search->text().trimmed();
    QList<QPair<int, PaletteItem>> scored;
    for (const PaletteItem &item : m_items) {
        const QString haystack = QStringLiteral("%1 %2 %3 %4")
                                     .arg(item.title, item.subtitle, item.group, item.keywords);
        if (query.isEmpty()) {
            // Без запроса показываем пункты в заданном порядке (важные — сверху)
            scored.append({0, item});
            continue;
        }
        const int score = matchScore(query, haystack);
        if (score < 0)
            continue;
        // Заголовок важнее подробностей: даём бонус за совпадение по нему
        const int titleScore = matchScore(query, item.title);
        scored.append({titleScore >= 0 ? score + titleScore / 2 : score, item});
    }

    if (!query.isEmpty())
    std::stable_sort(scored.begin(), scored.end(),
                     [](const QPair<int, PaletteItem> &a, const QPair<int, PaletteItem> &b) {
                         if (a.first != b.first)
                             return a.first > b.first;
                         if (a.second.group != b.second.group)
                             return a.second.group < b.second.group;
                         return a.second.title < b.second.title;
                     });

    m_filtered.clear();
    m_list->clear();
    for (const QPair<int, PaletteItem> &entry : scored) {
        if (m_filtered.size() >= kMaxVisible)
            break;
        m_filtered.append(entry.second);
        auto *item = new QListWidgetItem(displayText(entry.second), m_list);
        item->setData(Qt::UserRole, entry.second.id);
        item->setToolTip(QStringLiteral("%1\n%2").arg(entry.second.title, entry.second.group));
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    if (m_filtered.isEmpty()) {
        m_hint->setText(tr("Ничего не найдено. Пунктов всего: %1").arg(m_items.size()));
    } else if (query.isEmpty()) {
        m_hint->setText(tr("Пунктов: %1 · ↑ ↓ — выбор, Enter — выполнить, Esc — закрыть")
                            .arg(m_filtered.size()));
    } else {
        m_hint->setText(tr("Найдено: %1 · Enter — выполнить").arg(m_filtered.size()));
    }
}

QString CommandPalette::currentItemId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

int CommandPalette::currentRow() const
{
    return m_list->currentRow();
}

int CommandPalette::visibleCount() const
{
    return m_list->count();
}

bool CommandPalette::selectId(const QString &id)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toString() == id) {
            m_list->setCurrentRow(row);
            return true;
        }
    }
    return false;
}

void CommandPalette::activateCurrent()
{
    acceptCurrent();
}

void CommandPalette::acceptCurrent()
{
    const QString id = currentItemId();
    if (id.isEmpty())
        return;
    emit triggerRequested(id);
    accept();
}

void CommandPalette::open_(QWidget *parent)
{
    if (parent) {
        const QRect area = parent->geometry();
        const QSize size = this->size();
        move(area.center().x() - size.width() / 2, area.top() + qMax(60, area.height() / 6));
    }
    m_search->clear();
    rebuild();
    show();
    raise();
    activateWindow();
    m_search->setFocus();
}

void CommandPalette::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    m_search->setFocus();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Down:
            if (m_list->count() > 0) {
                const int row = qMin(m_list->count() - 1, m_list->currentRow() + 1);
                m_list->setCurrentRow(row);
            }
            return true;
        case Qt::Key_Up:
            if (m_list->count() > 0) {
                const int row = qMax(0, m_list->currentRow() - 1);
                m_list->setCurrentRow(row);
            }
            return true;
        case Qt::Key_Escape:
            reject();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace BlackCore
