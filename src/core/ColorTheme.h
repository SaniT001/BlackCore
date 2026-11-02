// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QList>
#include <QString>

namespace BlackCore {

//! Цветовая схема терминала: 16 базовых цветов + цвета по умолчанию.
struct ColorTheme
{
    QString name;
    QColor background;
    QColor foreground;
    QColor cursor;
    QColor cursorText;
    QColor selection;
    QColor palette[16];

    //! Все встроенные темы
    static QList<ColorTheme> builtinThemes();

    //! Тема по имени; если имя неизвестно — первая встроенная
    static ColorTheme byName(const QString &name);

    //! Индекс 0..255 → RGB (16 базовых из темы + куб 6x6x6 + градации серого)
    QColor colorByIndex(int index) const;
};

} // namespace BlackCore
