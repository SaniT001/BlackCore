// SPDX-License-Identifier: MIT
#include "ColorTheme.h"

#include <QRgb>

#include <array>

namespace BlackCore {
namespace {

ColorTheme makeTheme(const QString &name,
                     const QColor &bg,
                     const QColor &fg,
                     const std::array<const char *, 16> &palette)
{
    ColorTheme theme;
    theme.name = name;
    theme.background = bg;
    theme.foreground = fg;
    theme.cursor = fg;
    theme.cursorText = bg;
    theme.selection = QColor(fg.red(), fg.green(), fg.blue(), 70);
    for (int i = 0; i < 16; ++i)
        theme.palette[i] = QColor(QLatin1String(palette[i]));
    return theme;
}

} // namespace

QList<ColorTheme> ColorTheme::builtinThemes()
{
    QList<ColorTheme> themes;

    // Основная тёмная тема BlackCore
    themes.append(makeTheme(
        QStringLiteral("BlackCore Dark"),
        QColor(QStringLiteral("#101216")),
        QColor(QStringLiteral("#d6dbe5")),
        {"#1d2027", "#e05561", "#7ec96f", "#e2b93d", "#5aa9e6", "#c678dd", "#4fc1c8", "#b7bec9",
         "#4a5160", "#ff6b76", "#9be08a", "#ffd166", "#79c2ff", "#dd8df0", "#6fe3e8", "#eef2f8"}));

    // Светлая тема
    themes.append(makeTheme(
        QStringLiteral("BlackCore Light"),
        QColor(QStringLiteral("#fbfbfb")),
        QColor(QStringLiteral("#24292f")),
        {"#24292f", "#c0392b", "#1f7a34", "#9a6700", "#0b5cad", "#8250df", "#0d7d84", "#5b6573",
         "#6b7280", "#d1242f", "#2da44e", "#bf8700", "#218bff", "#a475f9", "#1b9aa3", "#3d444d"}));

    // Классическая xterm-подобная
    themes.append(makeTheme(
        QStringLiteral("Classic"),
        QColor(0, 0, 0),
        QColor(229, 229, 229),
        {"#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5",
         "#7f7f7f", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"}));

    return themes;
}

ColorTheme ColorTheme::byName(const QString &name)
{
    const QList<ColorTheme> themes = builtinThemes();
    for (const ColorTheme &theme : themes) {
        if (theme.name.compare(name, Qt::CaseInsensitive) == 0)
            return theme;
    }
    return themes.first();
}

QColor ColorTheme::colorByIndex(int index) const
{
    index = qBound(0, index, 255);
    if (index < 16)
        return palette[index];

    // Стандартная xterm-палитра 256 цветов
    if (index < 232) {
        const int i = index - 16;
        const int r = i / 36;
        const int g = (i % 36) / 6;
        const int b = i % 6;
        auto component = [](int value) {
            return value == 0 ? 0 : 55 + value * 40;
        };
        return QColor(component(r), component(g), component(b));
    }

    const int level = 8 + (index - 232) * 10;
    return QColor(level, level, level);
}

} // namespace BlackCore
