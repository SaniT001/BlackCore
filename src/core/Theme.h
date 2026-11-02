// SPDX-License-Identifier: MIT
#pragma once

#include "ColorTheme.h"

#include <QColor>
#include <QPalette>
#include <QList>
#include <QString>

namespace BlackCore {

//! Фирменная палитра интерфейса BlackCore.
/*!
 *  Все цвета интерфейса хранятся только здесь (Theme.cpp): виджеты не задают
 *  цвета «на месте», а берут их из активной темы. Новая тема = новая запись в
 *  ThemeManager::themes().
 */
struct BrandPalette
{
    QColor background;    //!< основной фон окна
    QColor panel;         //!< панели (заголовок, боковая панель, статус-строка)
    QColor active;        //!< активные элементы, hover, выделенные строки
    QColor gold;          //!< золотой акцент (кнопки, активная вкладка, фокус)
    QColor goldLight;     //!< светлый золотой (hover, подсветка текста)
    QColor textPrimary;   //!< основной текст
    QColor textSecondary; //!< вторичный текст, подписи
    QColor error;         //!< ошибки, опасные действия
    QColor success;       //!< успешные операции
};

//! Тема целиком: фирменная палитра + цветовая схема терминала.
struct Theme
{
    QString id;
    QString name;
    QString description;
    bool dark = true;
    BrandPalette brand;
    ColorTheme terminal;
};

//! Реестр тем и применение стилей ко всему приложению.
class ThemeManager
{
public:
    static const QList<Theme> &themes();

    //! Тема по идентификатору. Поддерживаются и старые имена («BlackCore Dark»).
    static Theme theme(const QString &id);

    static QString defaultId();

    //! Привести сохранённый идентификатор к актуальному
    static QString normalizeId(const QString &id);

    //! Запомнить системную палитру (вызывается при старте, до применения темы)
    static void captureSystemPalette();
    //! Запомнить конкретную палитру (например, полученную от рабочего стола)
    static void captureSystemPalette(const QPalette &palette);

    //! Тема, построенная по системной цветовой схеме рабочего стола
    static Theme systemTheme();

    //! Идентификатор «системной» темы
    static QString systemId();

    //! Тёмная ли тема (для выбора контрастных значков)
    static bool isDark(const QString &id);

    //! Применить палитру и таблицу стилей к QApplication
    static void apply(const Theme &theme);
    static void apply(const QString &id);

    //! Таблица стилей, собранная из палитры темы
    static QString styleSheet(const Theme &theme);

    //! Активная тема (последняя применённая)
    static Theme current();
    static BrandPalette colors();

private:
    static Theme makeDark();
    static Theme makeMidnight();
    static Theme makeGraphite();
    static Theme makeLight();
    static Theme makeContrast();

    static Theme &currentRef();
};

} // namespace BlackCore
