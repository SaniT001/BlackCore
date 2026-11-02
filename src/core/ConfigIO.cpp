// SPDX-License-Identifier: MIT
#include "ConfigIO.h"

#include "Notifications.h"

#include "AppInfo.h"
#include "Logger.h"
#include "Settings.h"
#include "Shortcuts.h"
#include "Theme.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace BlackCore {
namespace {

constexpr const char *kAppTag = "BlackCore";
constexpr const char *kSettingsGroup = "settings";
constexpr const char *kShortcutsGroup = "shortcuts";

//! Ожидаемый тип значения в файле конфигурации
enum class RuleType { Integer, Real, Boolean, Text };

struct KeyRule
{
    QString key;
    RuleType type;
    double min = 0;
    double max = 0;
    bool clamped = false; //!< числовое значение ограничивается диапазоном
};

QList<KeyRule> keyRules()
{
    const auto integer = [](const QString &key, int min, int max) {
        return KeyRule{key, RuleType::Integer, double(min), double(max), true};
    };
    const auto real = [](const QString &key, double min, double max) {
        return KeyRule{key, RuleType::Real, min, max, true};
    };
    const auto boolean = [](const QString &key) {
        return KeyRule{key, RuleType::Boolean, 0, 0, false};
    };
    const auto text = [](const QString &key) {
        return KeyRule{key, RuleType::Text, 0, 0, false};
    };

    return {
        // --- внешний вид ---
        text(QStringLiteral("terminal/fontFamily")),
        integer(QStringLiteral("terminal/fontSize"), Settings::kMinFontSize, Settings::kMaxFontSize),
        real(QStringLiteral("terminal/lineSpacing"), Settings::kMinLineSpacing, Settings::kMaxLineSpacing),
        integer(QStringLiteral("terminal/padding"), Settings::kMinPadding, Settings::kMaxPadding),
        text(QStringLiteral("appearance/themeId")),
        integer(QStringLiteral("appearance/cursorStyle"), 0, 3),
        boolean(QStringLiteral("appearance/cursorBlink")),
        integer(QStringLiteral("appearance/windowOpacity"), Settings::kMinOpacity, Settings::kMaxOpacity),
        boolean(QStringLiteral("appearance/useSystemTitleBar")),
        boolean(QStringLiteral("appearance/sidebarVisible")),
        boolean(QStringLiteral("appearance/statusBarVisible")),
        boolean(QStringLiteral("appearance/menuBarVisible")),
        boolean(QStringLiteral("appearance/tabBarVisible")),
        boolean(QStringLiteral("appearance/devPanelVisible")),
        boolean(QStringLiteral("appearance/monitorPanelVisible")),
        integer(QStringLiteral("monitor/intervalMs"), Settings::kMinMonitorInterval,
                Settings::kMaxMonitorInterval),
        real(QStringLiteral("appearance/uiScale"), Settings::kMinUiScale, Settings::kMaxUiScale),
        // --- прокрутка ---
        integer(QStringLiteral("terminal/scrollbackLines"), Settings::kMinScrollback, Settings::kMaxScrollback),
        integer(QStringLiteral("terminal/wheelScrollLines"), Settings::kMinWheelLines, Settings::kMaxWheelLines),
        boolean(QStringLiteral("terminal/scrollOnOutput")),
        boolean(QStringLiteral("terminal/scrollOnKeystroke")),
        // --- поведение ---
        text(QStringLiteral("terminal/shell")),
        text(QStringLiteral("behavior/startDirectory")),
        boolean(QStringLiteral("session/restoreOnStartup")),
        boolean(QStringLiteral("behavior/newTabInCurrentDirectory")),
        boolean(QStringLiteral("behavior/warnOnCloseRunning")),
        boolean(QStringLiteral("behavior/confirmCloseWindow")),
        boolean(QStringLiteral("terminal/confirmMultilinePaste")),
        boolean(QStringLiteral("terminal/bellAudible")),
        boolean(QStringLiteral("behavior/autoSave")),
        text(QStringLiteral("session/lastWorkingDirectory")),
        // --- этап 7: уведомления, история, библиотека команд ---
        boolean(QStringLiteral("notifications/enabled")),
        boolean(QStringLiteral("notifications/longCommand")),
        boolean(QStringLiteral("notifications/script")),
        boolean(QStringLiteral("notifications/sshDisconnected")),
        boolean(QStringLiteral("notifications/errors")),
        boolean(QStringLiteral("notifications/desktop")),
        integer(QStringLiteral("notifications/longCommandSeconds"),
                NotificationSettings::kMinSeconds, NotificationSettings::kMaxSeconds),
        boolean(QStringLiteral("history/recordCommands")),
        // --- прочие разделы ---
        text(QStringLiteral("commands/list")),
        text(QStringLiteral("history/commands")),
        text(QStringLiteral("history/activity")),
        text(QStringLiteral("profiles/list")),
        text(QStringLiteral("workspaces/list")),
        text(QStringLiteral("projects/list")),
        text(QStringLiteral("ssh/profiles")),
        text(QStringLiteral("window/geometry")),
    };
}

QJsonObject dumpSettings()
{
    QSettings store;
    QJsonObject settings;

    for (const QString &key : store.allKeys()) {
        // Горячие клавиши хранятся отдельным разделом
        if (key.startsWith(QStringLiteral("shortcuts/")))
            continue;
        settings.insert(key, QJsonValue::fromVariant(store.value(key)));
    }
    return settings;
}

QJsonObject dumpShortcuts()
{
    QSettings store;
    QJsonObject shortcuts;
    store.beginGroup(QStringLiteral("shortcuts/overrides"));
    for (const QString &key : store.childKeys())
        shortcuts.insert(key, store.value(key).toString());
    store.endGroup();
    return shortcuts;
}

bool isManagedKey(const QString &key)
{
    for (const QString &prefix : ConfigIO::managedKeyPrefixes()) {
        if (key.startsWith(prefix))
            return true;
    }
    return false;
}

const KeyRule *ruleFor(const QString &key, const QList<KeyRule> &rules)
{
    for (const KeyRule &rule : rules) {
        if (rule.key == key)
            return &rule;
    }
    return nullptr;
}

//! Значение приводим к типу правила; при неверном типе — ошибка
bool coerce(const QJsonValue &value, const KeyRule &rule, QVariant *result, QString *error,
            QStringList *warnings)
{
    switch (rule.type) {
    case RuleType::Integer:
    case RuleType::Real: {
        if (!value.isDouble()) {
            *error = QObject::tr("параметр «%1» должен быть числом").arg(rule.key);
            return false;
        }
        double number = value.toDouble();
        if (rule.clamped && (number < rule.min || number > rule.max)) {
            const double fixed = qBound(rule.min, number, rule.max);
            warnings->append(QObject::tr("%1: значение %2 заменено на %3 (допустимо %4…%5)")
                                 .arg(rule.key)
                                 .arg(number)
                                 .arg(fixed)
                                 .arg(rule.min)
                                 .arg(rule.max));
            number = fixed;
        }
        *result = rule.type == RuleType::Integer ? QVariant(qRound(number)) : QVariant(number);
        return true;
    }
    case RuleType::Boolean:
        if (!value.isBool()) {
            *error = QObject::tr("параметр «%1» должен быть true или false").arg(rule.key);
            return false;
        }
        *result = value.toBool();
        return true;
    case RuleType::Text:
        if (!value.isString()) {
            *error = QObject::tr("параметр «%1» должен быть строкой").arg(rule.key);
            return false;
        }
        *result = value.toString();
        return true;
    }
    return false;
}

//! Дополнительные проверки смысла (существование пути, шрифта, JSON-разделов)
void checkSemantics(const QString &key, QVariant *value, QStringList *warnings)
{
    if (key == QStringLiteral("terminal/shell")) {
        const QString shell = value->toString();
        if (!shell.isEmpty() && !QFileInfo(shell).isExecutable()) {
            warnings->append(QObject::tr("оболочка «%1» не найдена или не исполняемая — "
                                         "используется системная")
                                 .arg(shell));
            value->clear();
        }
        return;
    }

    if (key == QStringLiteral("behavior/startDirectory")) {
        const QString directory = value->toString();
        if (!directory.isEmpty() && !QFileInfo(directory).isDir()) {
            warnings->append(QObject::tr("стартовый каталог «%1» недоступен — "
                                         "будет использоваться домашний")
                                 .arg(directory));
            value->clear();
        }
        return;
    }

    if (key == QStringLiteral("terminal/fontFamily")) {
        const QString family = value->toString();
        if (!family.isEmpty() && !QFontDatabase::families().contains(family)) {
            warnings->append(QObject::tr("шрифт «%1» не найден — оставлен текущий").arg(family));
            value->clear();
        }
        return;
    }

    if (key == QStringLiteral("appearance/themeId")) {
        const QString id = value->toString();
        const QString normalized = ThemeManager::normalizeId(id);
        if (!id.isEmpty() && normalized != id)
            warnings->append(QObject::tr("неизвестная тема «%1» заменена на «%2»")
                                 .arg(id, normalized));
        *value = normalized;
        return;
    }

    if (key == QStringLiteral("profiles/list") || key == QStringLiteral("workspaces/list")) {
        const QByteArray raw = value->toString().toUtf8();
        if (!raw.trimmed().isEmpty()) {
            QJsonParseError parseError{};
            QJsonDocument::fromJson(raw, &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                warnings->append(QObject::tr("раздел «%1» повреждён и пропущен").arg(key));
                value->clear();
            }
        }
    }
}

} // namespace

QStringList ConfigIO::managedKeyPrefixes()
{
    return {QStringLiteral("terminal/"), QStringLiteral("appearance/"),
            QStringLiteral("behavior/"),     QStringLiteral("session/"),
            QStringLiteral("shortcuts/"),    QStringLiteral("profiles/"),
            QStringLiteral("workspaces/"),   QStringLiteral("window/")};
}

QString ConfigIO::configFilePath()
{
    QSettings store;
    return store.fileName();
}

QString ConfigIO::backupDirectory()
{
    QString directory = stateDirectory();
    directory += QStringLiteral("/backups");
    QDir().mkpath(directory);
    return directory;
}

QJsonObject ConfigIO::toJson()
{
    QJsonObject object;
    object.insert(QStringLiteral("app"), QString::fromLatin1(kAppTag));
    object.insert(QStringLiteral("format"), kFormatVersion);
    object.insert(QStringLiteral("version"), versionString());
    object.insert(QStringLiteral("created"),
                  QDateTime::currentDateTime().toString(Qt::ISODate));
    object.insert(QString::fromLatin1(kSettingsGroup), dumpSettings());
    object.insert(QString::fromLatin1(kShortcutsGroup), dumpShortcuts());
    return object;
}

bool ConfigIO::exportToFile(const QString &path, QString *error)
{
    if (path.isEmpty()) {
        if (error)
            *error = QObject::tr("не указан путь для экспорта");
        return false;
    }

    const QFileInfo info(path);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QObject::tr("каталог «%1» недоступен для записи").arg(info.absolutePath());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QObject::tr("не удалось открыть «%1» для записи: %2").arg(path, file.errorString());
        return false;
    }

    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QObject::tr("не удалось записать «%1»: %2").arg(path, file.errorString());
        return false;
    }

    Log::info(QObject::tr("Конфигурация экспортирована: %1").arg(path));
    return true;
}

bool ConfigIO::validate(const QJsonObject &object, QString *error, QStringList *warnings)
{
    QStringList localWarnings;
    if (!warnings)
        warnings = &localWarnings;

    if (object.isEmpty()) {
        if (error)
            *error = QObject::tr("файл пуст или не является корректным JSON");
        return false;
    }

    if (object.value(QStringLiteral("app")).toString() != QString::fromLatin1(kAppTag)) {
        if (error)
            *error = QObject::tr("это не файл конфигурации BlackCore");
        return false;
    }

    const QJsonValue formatValue = object.value(QStringLiteral("format"));
    if (!formatValue.isDouble()) {
        if (error)
            *error = QObject::tr("в файле отсутствует номер формата");
        return false;
    }
    const int format = formatValue.toInt();
    if (format < 1 || format > kFormatVersion) {
        if (error)
            *error = QObject::tr("формат файла %1 не поддерживается (ожидается до %2)")
                         .arg(format)
                         .arg(kFormatVersion);
        return false;
    }

    const QJsonValue settingsValue = object.value(QString::fromLatin1(kSettingsGroup));
    if (!settingsValue.isObject()) {
        if (error)
            *error = QObject::tr("раздел «settings» отсутствует или повреждён");
        return false;
    }

    const QList<KeyRule> rules = keyRules();
    const QJsonObject settings = settingsValue.toObject();
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        const KeyRule *rule = ruleFor(it.key(), rules);
        if (!rule) {
            warnings->append(QObject::tr("неизвестный параметр «%1» пропущен").arg(it.key()));
            continue;
        }
        QVariant value;
        QString typeError;
        if (!coerce(it.value(), *rule, &value, &typeError, warnings)) {
            if (error)
                *error = typeError;
            return false;
        }
    }

    const QJsonValue shortcutsValue = object.value(QString::fromLatin1(kShortcutsGroup));
    if (!shortcutsValue.isUndefined() && !shortcutsValue.isObject()) {
        if (error)
            *error = QObject::tr("раздел «shortcuts» повреждён");
        return false;
    }

    const QJsonObject shortcuts = shortcutsValue.toObject();
    for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it) {
        if (!it.value().isString()) {
            if (error)
                *error = QObject::tr("горячая клавиша «%1» задана не строкой").arg(it.key());
            return false;
        }
        QKeySequence parsed;
        const QString text = it.value().toString();
        if (!text.isEmpty() && !ShortcutManager::parse(text, &parsed)) {
            warnings->append(QObject::tr("горячая клавиша «%1» для «%2» некорректна — пропущена")
                                 .arg(text, it.key()));
        }
    }

    return true;
}

bool ConfigIO::apply(const QJsonObject &object, QStringList *warnings)
{
    QSettings store;
    const QJsonObject settings = object.value(QString::fromLatin1(kSettingsGroup)).toObject();
    const QList<KeyRule> rules = keyRules();

    // Сбрасываем прежние значения управляемых ключей, чтобы не смешивать наборы
    for (const QString &key : store.allKeys()) {
        if (isManagedKey(key) && !key.startsWith(QStringLiteral("shortcuts/")))
            store.remove(key);
    }

    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        const KeyRule *rule = ruleFor(it.key(), rules);
        if (!rule)
            continue;

        QVariant value;
        QString ignored;
        if (!coerce(it.value(), *rule, &value, &ignored, warnings))
            continue;
        checkSemantics(it.key(), &value, warnings);
        if (!value.isValid())
            continue;
        store.setValue(it.key(), value);
    }

    const QJsonObject shortcuts = object.value(QString::fromLatin1(kShortcutsGroup)).toObject();
    store.remove(QStringLiteral("shortcuts/overrides"));
    for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it) {
        if (!ShortcutManager::catalog().isEmpty() && ShortcutManager::defaultKey(it.key()).isEmpty()
            && !ShortcutManager::hasOverride(it.key())) {
            // Действие неизвестно текущей версии — пропускаем без ошибки
            continue;
        }
        QKeySequence parsed;
        const QString text = it.value().toString();
        if (!text.isEmpty() && !ShortcutManager::parse(text, &parsed))
            continue;
        if (ShortcutManager::defaultKey(it.key()) == parsed)
            continue;
        store.beginGroup(QStringLiteral("shortcuts/overrides"));
        store.setValue(it.key(), text);
        store.endGroup();
    }

    store.sync();

    // Перечитываем настройки в память приложения
    Settings::instance().load();
    return true;
}

bool ConfigIO::importFromJson(const QJsonObject &object, QString *error, QStringList *warnings)
{
    QStringList localWarnings;
    if (!warnings)
        warnings = &localWarnings;

    if (!validate(object, error, warnings))
        return false;

    // Резервная копия текущих настроек перед заменой
    const QString backup = backupCurrent();
    if (backup.isEmpty()) {
        if (error)
            *error = QObject::tr("не удалось создать резервную копию текущих настроек");
        return false;
    }

    if (!apply(object, warnings)) {
        if (error)
            *error = QObject::tr("не удалось применить настройки");
        return false;
    }

    warnings->append(QObject::tr("резервная копия: %1").arg(backup));
    Log::info(QObject::tr("Конфигурация импортирована (резервная копия: %1)").arg(backup));
    return true;
}

bool ConfigIO::importFromFile(const QString &path, QString *error, QStringList *warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("не удалось открыть «%1»: %2").arg(path, file.errorString());
        return false;
    }

    if (file.size() > 4 * 1024 * 1024) {
        if (error)
            *error = QObject::tr("файл «%1» слишком большой для конфигурации").arg(path);
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QObject::tr("файл «%1» не является корректным JSON: %2")
                         .arg(path, parseError.errorString());
        return false;
    }

    return importFromJson(document.object(), error, warnings);
}

QString ConfigIO::backupCurrent(QString *error)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString path = backupDirectory() + QStringLiteral("/config-%1.json").arg(stamp);

    QString localError;
    if (!exportToFile(path, &localError)) {
        if (error)
            *error = localError;
        Log::error(QObject::tr("Не удалось создать резервную копию: %1").arg(localError));
        return QString();
    }
    return path;
}

bool ConfigIO::resetToDefaults(QString *backupPath, QString *error)
{
    const QString backup = backupCurrent();
    if (backup.isEmpty()) {
        if (error)
            *error = QObject::tr("не удалось создать резервную копию текущих настроек");
        return false;
    }
    if (backupPath)
        *backupPath = backup;

    QSettings store;
    for (const QString &key : store.allKeys()) {
        if (isManagedKey(key))
            store.remove(key);
    }
    store.sync();

    ShortcutManager::resetAll();
    Settings::instance().load();
    Log::info(QObject::tr("Настройки сброшены к значениям по умолчанию (копия: %1)").arg(backup));
    return true;
}

} // namespace BlackCore
