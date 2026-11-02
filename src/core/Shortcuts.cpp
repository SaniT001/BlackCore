// SPDX-License-Identifier: MIT
#include "Shortcuts.h"

#include "Logger.h"

#include <QSettings>
#include <QStringList>

namespace BlackCore {
namespace {

constexpr const char *kOverridesKey = "shortcuts/overrides";

QString sequenceToString(const QKeySequence &key)
{
    return key.toString(QKeySequence::PortableText);
}

QStringList defaultGroupOrder()
{
    return {QStringLiteral("Приложение"), QStringLiteral("Вкладки"), QStringLiteral("Панели"),
            QStringLiteral("Терминал"), QStringLiteral("Вид")};
}

} // namespace

QList<ShortcutSpec> ShortcutManager::buildCatalog()
{
    QList<ShortcutSpec> list;

    auto add = [&list](const QString &id, const QString &title, const QString &group,
                       const QKeySequence &key) {
        list.append(ShortcutSpec{id, title, group, key});
    };

    const QString app = QStringLiteral("Приложение");
    add(QStringLiteral("app.newWindow"), QObject::tr("Новое окно"), app,
        QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    add(QStringLiteral("app.settings"), QObject::tr("Настройки"), app,
        QKeySequence(QStringLiteral("Ctrl+,")));
    add(QStringLiteral("app.quit"), QObject::tr("Выход"), app,
        QKeySequence(QStringLiteral("Ctrl+Shift+Q")));

    const QString tabs = QStringLiteral("Вкладки");
    add(QStringLiteral("tab.new"), QObject::tr("Новая вкладка"), tabs,
        QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    add(QStringLiteral("tab.close"), QObject::tr("Закрыть вкладку"), tabs,
        QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    add(QStringLiteral("tab.rename"), QObject::tr("Переименовать вкладку"), tabs,
        QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    add(QStringLiteral("tab.duplicate"), QObject::tr("Дублировать вкладку"), tabs,
        QKeySequence(QStringLiteral("Ctrl+Shift+U")));
    add(QStringLiteral("tab.next"), QObject::tr("Следующая вкладка"), tabs,
        QKeySequence(QStringLiteral("Ctrl+Tab")));
    add(QStringLiteral("tab.previous"), QObject::tr("Предыдущая вкладка"), tabs,
        QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));

    for (int i = 1; i <= 9; ++i) {
        add(QStringLiteral("tab.index%1").arg(i), QObject::tr("Вкладка %1").arg(i), tabs,
            QKeySequence(QStringLiteral("Ctrl+%1").arg(i)));
    }

    const QString panes = QStringLiteral("Панели");
    add(QStringLiteral("pane.splitVertical"), QObject::tr("Разделить вертикально"), panes,
        QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    add(QStringLiteral("pane.splitHorizontal"), QObject::tr("Разделить горизонтально"), panes,
        QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    add(QStringLiteral("pane.close"), QObject::tr("Закрыть панель"), panes,
        QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    add(QStringLiteral("pane.next"), QObject::tr("Следующая панель"), panes,
        QKeySequence(QStringLiteral("F6")));
    add(QStringLiteral("pane.previous"), QObject::tr("Предыдущая панель"), panes,
        QKeySequence(QStringLiteral("Shift+F6")));
    add(QStringLiteral("pane.left"), QObject::tr("Панель слева"), panes,
        QKeySequence(QStringLiteral("Ctrl+Shift+Left")));
    add(QStringLiteral("pane.right"), QObject::tr("Панель справа"), panes,
        QKeySequence(QStringLiteral("Ctrl+Shift+Right")));
    add(QStringLiteral("pane.up"), QObject::tr("Панель сверху"), panes,
        QKeySequence(QStringLiteral("Ctrl+Shift+Up")));
    add(QStringLiteral("pane.down"), QObject::tr("Панель снизу"), panes,
        QKeySequence(QStringLiteral("Ctrl+Shift+Down")));

    const QString terminal = QStringLiteral("Терминал");
    add(QStringLiteral("edit.copy"), QObject::tr("Копировать"), terminal,
        QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    add(QStringLiteral("edit.paste"), QObject::tr("Вставить"), terminal,
        QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    add(QStringLiteral("edit.selectAll"), QObject::tr("Выделить всё"), terminal,
        QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    add(QStringLiteral("edit.clear"), QObject::tr("Очистить экран"), terminal,
        QKeySequence(QStringLiteral("Ctrl+Shift+K")));

    const QString view = QStringLiteral("Вид");
    add(QStringLiteral("view.sidebar"), QObject::tr("Боковая панель"), view,
        QKeySequence(QStringLiteral("F9")));
    add(QStringLiteral("view.menuBar"), QObject::tr("Главное меню"), view,
        QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    add(QStringLiteral("view.devPanel"), QObject::tr("Панель разработчика (Git, SSH, проекты)"),
        view, QKeySequence(QStringLiteral("F8")));
    add(QStringLiteral("view.palette"), QObject::tr("Командная палитра"), view,
        QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    add(QStringLiteral("view.history"), QObject::tr("История и поиск"), view,
        QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    add(QStringLiteral("view.commandLibrary"), QObject::tr("Библиотека команд"), view,
        QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    add(QStringLiteral("view.monitorPanel"), QObject::tr("Панель монитора (система и диагностика)"),
        view, QKeySequence(QStringLiteral("F7")));
    add(QStringLiteral("view.fullScreen"), QObject::tr("Полноэкранный режим"), view,
        QKeySequence(QStringLiteral("F11")));
    add(QStringLiteral("view.zoomIn"), QObject::tr("Увеличить шрифт"), view,
        QKeySequence(QStringLiteral("Ctrl++")));
    add(QStringLiteral("view.zoomOut"), QObject::tr("Уменьшить шрифт"), view,
        QKeySequence(QStringLiteral("Ctrl+-")));
    add(QStringLiteral("view.zoomReset"), QObject::tr("Сбросить масштаб"), view,
        QKeySequence(QStringLiteral("Ctrl+0")));

    // Группы в стабильном порядке
    QList<ShortcutSpec> ordered;
    for (const QString &group : defaultGroupOrder()) {
        for (const ShortcutSpec &spec : list) {
            if (spec.group == group)
                ordered.append(spec);
        }
    }
    return ordered;
}

const QList<ShortcutSpec> &ShortcutManager::catalog()
{
    static const QList<ShortcutSpec> list = buildCatalog();
    return list;
}

QKeySequence ShortcutManager::defaultKey(const QString &id)
{
    for (const ShortcutSpec &spec : catalog()) {
        if (spec.id == id)
            return spec.defaultKey;
    }
    return QKeySequence();
}

QKeySequence ShortcutManager::key(const QString &id)
{
    QSettings store;
    store.beginGroup(QLatin1String(kOverridesKey));
    if (store.childKeys().contains(id)) {
        const QString text = store.value(id).toString();
        store.endGroup();
        if (text.isEmpty())
            return QKeySequence();
        const QKeySequence parsed(text, QKeySequence::PortableText);
        return parsed.isEmpty() ? defaultKey(id) : parsed;
    }
    store.endGroup();
    return defaultKey(id);
}

void ShortcutManager::setKey(const QString &id, const QKeySequence &key)
{
    if (defaultKey(id) == key && !hasOverride(id))
        return;

    QSettings store;
    store.beginGroup(QLatin1String(kOverridesKey));
    if (defaultKey(id) == key)
        store.remove(id);
    else
        store.setValue(id, sequenceToString(key));
    store.endGroup();
    store.sync();
}

bool ShortcutManager::hasOverride(const QString &id)
{
    QSettings store;
    store.beginGroup(QLatin1String(kOverridesKey));
    const bool present = store.childKeys().contains(id);
    store.endGroup();
    return present;
}

void ShortcutManager::resetKey(const QString &id)
{
    QSettings store;
    store.beginGroup(QLatin1String(kOverridesKey));
    store.remove(id);
    store.endGroup();
    store.sync();
}

void ShortcutManager::resetAll()
{
    QSettings store;
    store.remove(QLatin1String(kOverridesKey));
    store.sync();
}

int ShortcutManager::overrideCount()
{
    QSettings store;
    store.beginGroup(QLatin1String(kOverridesKey));
    const int count = store.childKeys().size();
    store.endGroup();
    return count;
}

bool ShortcutManager::parse(const QString &text, QKeySequence *key)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (key)
            *key = QKeySequence();
        return true;
    }

    const QKeySequence parsed(trimmed, QKeySequence::PortableText);
    if (parsed.isEmpty())
        return false;

    // Отклоняем «одиночные» буквы и цифры без модификатора — они мешают вводу
    const int modifiers = parsed[0].keyboardModifiers();
    const bool functionKey = parsed[0].key() >= Qt::Key_F1 && parsed[0].key() <= Qt::Key_F35;
    if (!functionKey && (modifiers & ~Qt::KeypadModifier) == Qt::NoModifier)
        return false;

    if (key)
        *key = parsed;
    return true;
}

QString ShortcutManager::conflict(const QString &keyText, const QString &exceptId)
{
    if (keyText.trimmed().isEmpty())
        return QString();

    const QKeySequence wanted(keyText.trimmed(), QKeySequence::PortableText);
    if (wanted.isEmpty())
        return QString();

    for (const ShortcutSpec &spec : catalog()) {
        if (!exceptId.isEmpty() && spec.id == exceptId)
            continue;
        const QKeySequence current = key(spec.id);
        if (current.isEmpty())
            continue;
        if (current == wanted)
            return spec.id;
    }
    return QString();
}

QStringList ShortcutManager::conflicts()
{
    QStringList result;
    const QList<ShortcutSpec> &list = catalog();

    for (int i = 0; i < list.size(); ++i) {
        const QKeySequence first = key(list.at(i).id);
        if (first.isEmpty())
            continue;
        for (int j = i + 1; j < list.size(); ++j) {
            const QKeySequence second = key(list.at(j).id);
            if (second.isEmpty() || first != second)
                continue;
            result.append(QStringLiteral("%1 ↔ %2").arg(list.at(i).id, list.at(j).id));
        }
    }
    return result;
}

} // namespace BlackCore
