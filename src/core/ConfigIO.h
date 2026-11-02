// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace BlackCore {

//! Экспорт, импорт, проверка и резервное копирование конфигурации.
/*!
 *  Конфигурация — это файл QSettings (~/.config/BlackCore/BlackCore.conf):
 *  настройки внешнего вида и поведения, горячие клавиши, профили терминала и
 *  рабочие пространства. Файлы оболочки (.bashrc, .zshrc) и системная оболочка
 *  пользователя никогда не изменяются — BlackCore только читает путь к оболочке.
 */
class ConfigIO
{
public:
    //! Версия формата файла конфигурации
    static constexpr int kFormatVersion = 1;

    //! Путь к файлу конфигурации (INI)
    static QString configFilePath();

    //! Каталог резервных копий: ~/.local/state/blackcore/backups
    static QString backupDirectory();

    //! Снимок всей конфигурации
    static QJsonObject toJson();

    //! Экспорт в файл (атомарная запись)
    static bool exportToFile(const QString &path, QString *error = nullptr);

    //! Импорт из файла: проверка → резервная копия → применение
    static bool importFromFile(const QString &path, QString *error = nullptr,
                               QStringList *warnings = nullptr);

    //! Импорт готового объекта (используется тестами и импортом из файла)
    static bool importFromJson(const QJsonObject &object, QString *error = nullptr,
                               QStringList *warnings = nullptr);

    //! Проверка объекта без применения: ошибки и предупреждения
    static bool validate(const QJsonObject &object, QString *error = nullptr,
                         QStringList *warnings = nullptr);

    //! Резервная копия текущей конфигурации; возвращает путь к файлу
    static QString backupCurrent(QString *error = nullptr);

    //! Сброс к значениям по умолчанию (с резервной копией)
    static bool resetToDefaults(QString *backupPath = nullptr, QString *error = nullptr);

    //! Ключи, которыми управляет BlackCore (для сброса и проверки)
    static QStringList managedKeyPrefixes();

private:
    static bool apply(const QJsonObject &object, QStringList *warnings);
};

} // namespace BlackCore
