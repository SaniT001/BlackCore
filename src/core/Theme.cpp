// SPDX-License-Identifier: MIT
#include "Theme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>

namespace BlackCore {
namespace {

ColorTheme terminalTheme(const QString &name,
                         const QColor &background,
                         const QColor &foreground,
                         const QColor &cursor,
                         const QColor &selection,
                         const std::array<const char *, 16> &palette)
{
    ColorTheme theme;
    theme.name = name;
    theme.background = background;
    theme.foreground = foreground;
    theme.cursor = cursor;
    theme.cursorText = background;
    theme.selection = selection;
    for (int i = 0; i < 16; ++i)
        theme.palette[i] = QColor(QLatin1String(palette[i]));
    return theme;
}

//! Палитра рабочего стола, зафиксированная до применения наших тем
QPalette &systemPaletteRef()
{
    static QPalette palette = QApplication::palette();
    return palette;
}

bool &systemPaletteCaptured()
{
    static bool captured = false;
    return captured;
}

QPalette systemPalette()
{
    return systemPaletteRef();
}

//! Относительная яркость по WCAG (для проверки контраста)
double relativeLuminance(const QColor &color)
{
    auto channel = [](double value) {
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF())
        + 0.0722 * channel(color.blueF());
}

double contrastRatio(const QColor &first, const QColor &second)
{
    const double lighter = std::max(relativeLuminance(first), relativeLuminance(second));
    const double darker = std::min(relativeLuminance(first), relativeLuminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}

//! Подтянуть цвет, пока он не станет различим на фоне (системные схемы бывают бледными)
QColor ensureContrast(QColor color, const QColor &background, double minimum)
{
    const bool backgroundIsDark = background.lightness() < 128;
    for (int step = 0; step < 12 && contrastRatio(color, background) < minimum; ++step)
        color = backgroundIsDark ? color.lighter(118) : color.darker(118);
    return color;
}

QString rgba(const QColor &color, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

} // namespace

Theme ThemeManager::makeDark()
{
    Theme theme;
    theme.id = QStringLiteral("blackcore-dark");
    theme.name = QStringLiteral("BlackCore Dark");
    theme.description = QStringLiteral("Фирменная тёмная тема: графит и золото");
    theme.dark = true;

    BrandPalette &b = theme.brand;
    b.background = QColor(QStringLiteral("#101113"));
    b.panel = QColor(QStringLiteral("#17191C"));
    b.active = QColor(QStringLiteral("#24262A"));
    b.gold = QColor(QStringLiteral("#D9B15C"));
    b.goldLight = QColor(QStringLiteral("#F0D18A"));
    b.textPrimary = QColor(QStringLiteral("#E8E8E8"));
    b.textSecondary = QColor(QStringLiteral("#969BA3"));
    b.error = QColor(QStringLiteral("#EF6464"));
    b.success = QColor(QStringLiteral("#66C58A"));

    theme.terminal = terminalTheme(
        theme.name, b.background, b.textPrimary, b.gold, rgba(b.gold, 80),
        {"#24262A", "#EF6464", "#66C58A", "#D9B15C", "#6BA9DB", "#C08BD6", "#63C7C9", "#C9CED6",
         "#5A6068", "#FF8A8A", "#8FE0AE", "#F0D18A", "#8CC4F0", "#D3A9EC", "#8EDBDC", "#FFFFFF"});
    return theme;
}

Theme ThemeManager::makeMidnight()
{
    Theme theme;
    theme.id = QStringLiteral("blackcore-midnight");
    theme.name = QStringLiteral("BlackCore Midnight");
    theme.description = QStringLiteral("Тёмная синяя тема для работы вечером");
    theme.dark = true;

    BrandPalette &b = theme.brand;
    b.background = QColor(QStringLiteral("#0B1220"));
    b.panel = QColor(QStringLiteral("#101A2B"));
    b.active = QColor(QStringLiteral("#1B2941"));
    b.gold = QColor(QStringLiteral("#7FB3E8"));
    b.goldLight = QColor(QStringLiteral("#A9CCF2"));
    b.textPrimary = QColor(QStringLiteral("#E4EAF3"));
    b.textSecondary = QColor(QStringLiteral("#8B9AB3"));
    b.error = QColor(QStringLiteral("#F27D7D"));
    b.success = QColor(QStringLiteral("#69CFA0"));

    theme.terminal = terminalTheme(
        theme.name, b.background, b.textPrimary, b.goldLight, rgba(b.gold, 90),
        {"#1B2941", "#F27D7D", "#69CFA0", "#E5C07B", "#7FB3E8", "#C79BE8", "#5FD3D3", "#C9D3E2",
         "#5A6A85", "#FF9C9C", "#8FE3BC", "#F2D69C", "#A2CAF5", "#DCBBF5", "#87E5E5", "#FFFFFF"});
    return theme;
}

Theme ThemeManager::makeGraphite()
{
    Theme theme;
    theme.id = QStringLiteral("blackcore-graphite");
    theme.name = QStringLiteral("BlackCore Graphite");
    theme.description = QStringLiteral("Нейтральная серая тёмная тема без тёплых оттенков");
    theme.dark = true;

    BrandPalette &b = theme.brand;
    b.background = QColor(QStringLiteral("#1C1E21"));
    b.panel = QColor(QStringLiteral("#24262A"));
    b.active = QColor(QStringLiteral("#303338"));
    b.gold = QColor(QStringLiteral("#C7CCD4"));
    b.goldLight = QColor(QStringLiteral("#E7EAEF"));
    b.textPrimary = QColor(QStringLiteral("#EDEFF2"));
    b.textSecondary = QColor(QStringLiteral("#9EA4AD"));
    b.error = QColor(QStringLiteral("#E97878"));
    b.success = QColor(QStringLiteral("#79C79A"));

    theme.terminal = terminalTheme(
        theme.name, b.background, b.textPrimary, b.gold, rgba(b.gold, 90),
        {"#2C2F34", "#E97878", "#79C79A", "#D8C07A", "#8AB4E0", "#BE9BD8", "#79C3C3", "#D5DAE1",
         "#6B727B", "#FF9A9A", "#9BDCB4", "#EBD99B", "#A6C9EC", "#D3B7E6", "#9BD6D6", "#FFFFFF"});
    return theme;
}

Theme ThemeManager::makeLight()
{
    Theme theme;
    theme.id = QStringLiteral("blackcore-light");
    theme.name = QStringLiteral("BlackCore Light");
    theme.description = QStringLiteral("Светлая тема с тем же золотым акцентом");
    theme.dark = false;

    BrandPalette &b = theme.brand;
    b.background = QColor(QStringLiteral("#F7F7F5"));
    b.panel = QColor(QStringLiteral("#FFFFFF"));
    b.active = QColor(QStringLiteral("#EFEEE9"));
    b.gold = QColor(QStringLiteral("#B8892F"));
    b.goldLight = QColor(QStringLiteral("#D9B15C"));
    b.textPrimary = QColor(QStringLiteral("#1B1D21"));
    b.textSecondary = QColor(QStringLiteral("#6A7078"));
    b.error = QColor(QStringLiteral("#C0453F"));
    b.success = QColor(QStringLiteral("#2E8B57"));

    theme.terminal = terminalTheme(
        theme.name, QColor(QStringLiteral("#FBFBF9")), b.textPrimary, b.gold, rgba(b.gold, 90),
        {"#1B1D21", "#C0392B", "#2E7D4F", "#9A6B0F", "#2A5DB0", "#7B4FA8", "#0F7C82", "#B9BDC4",
         "#6A7078", "#E05252", "#3FA46A", "#B8892F", "#4C87D8", "#9A6CCB", "#2AA3A8", "#101113"});
    return theme;
}

Theme ThemeManager::makeContrast()
{
    Theme theme;
    theme.id = QStringLiteral("blackcore-contrast");
    theme.name = QStringLiteral("BlackCore Contrast");
    theme.description = QStringLiteral("Максимальный контраст для яркого света и слабого зрения");
    theme.dark = true;

    BrandPalette &b = theme.brand;
    b.background = QColor(QStringLiteral("#000000"));
    b.panel = QColor(QStringLiteral("#0B0C0E"));
    b.active = QColor(QStringLiteral("#22252A"));
    b.gold = QColor(QStringLiteral("#FFD166"));
    b.goldLight = QColor(QStringLiteral("#FFE9A8"));
    b.textPrimary = QColor(QStringLiteral("#FFFFFF"));
    b.textSecondary = QColor(QStringLiteral("#C3CAD2"));
    b.error = QColor(QStringLiteral("#FF7B7B"));
    b.success = QColor(QStringLiteral("#7BE0A0"));

    theme.terminal = terminalTheme(
        theme.name, b.background, b.textPrimary, b.gold, rgba(b.gold, 110),
        {"#2A2D33", "#FF6B6B", "#6BE08A", "#FFD166", "#6BB6FF", "#D08CFF", "#5FE0E0", "#E6E6E6",
         "#7A8290", "#FF9B9B", "#9BF0B6", "#FFE9A8", "#9BD2FF", "#E3B8FF", "#9BF0F0", "#FFFFFF"});
    return theme;
}

const QList<Theme> &ThemeManager::themes()
{
    // Системная тема пересобирается при каждом обращении: она зависит от палитры
    // рабочего стола, зафиксированной при запуске приложения.
    static QList<Theme> all{makeDark(), makeGraphite(), makeMidnight(),
                            makeLight(), makeContrast(), systemTheme()};
    all[5] = systemTheme();
    return all;
}

QString ThemeManager::systemId()
{
    return QStringLiteral("system");
}

void ThemeManager::captureSystemPalette()
{
    captureSystemPalette(QApplication::palette());
}

void ThemeManager::captureSystemPalette(const QPalette &palette)
{
    systemPaletteRef() = palette;
    systemPaletteCaptured() = true;
}

Theme ThemeManager::systemTheme()
{
    // Если приложение не зафиксировало палитру при старте — берём текущую
    if (!systemPaletteCaptured()) {
        systemPaletteRef() = QApplication::palette();
        systemPaletteCaptured() = true;
    }

    const QPalette palette = systemPalette();
    const QColor window = palette.color(QPalette::Window);
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::WindowText);
    const QColor highlight = palette.color(QPalette::Highlight);
    const bool dark = window.lightness() < 128;

    Theme theme;
    theme.id = systemId();
    theme.name = QStringLiteral("Системная");
    theme.description = QStringLiteral("Цвета из системной схемы рабочего стола");
    theme.dark = dark;

    BrandPalette &b = theme.brand;
    b.background = window;
    b.panel = dark ? window.lighter(118) : window.darker(104);
    b.active = dark ? window.lighter(145) : window.darker(110);
    b.gold = highlight.isValid() ? highlight : QColor(QStringLiteral("#D9B15C"));
    b.goldLight = dark ? b.gold.lighter(125) : b.gold.lighter(115);
    b.textPrimary = ensureContrast(text, window, 4.5);
    b.textSecondary = palette.color(QPalette::Disabled, QPalette::WindowText);
    if (b.textSecondary.lightness() == b.textPrimary.lightness())
        b.textSecondary = dark ? text.darker(150) : text.lighter(150);
    b.textSecondary = ensureContrast(b.textSecondary, window, 3.0);

    // Акцент системной схемы может быть бледным — подтягиваем к читаемому виду
    b.gold = ensureContrast(b.gold, window, 2.6);
    b.error = QColor(QStringLiteral("#E05C5C"));
    b.success = QColor(QStringLiteral("#4CAF7D"));

    const std::array<const char *, 16> darkPalette{
        "#2C2F34", "#E97878", "#79C79A", "#D8C07A", "#8AB4E0", "#BE9BD8", "#79C3C3", "#D5DAE1",
        "#6B727B", "#FF9A9A", "#9BDCB4", "#EBD99B", "#A6C9EC", "#D3B7E6", "#9BD6D6", "#FFFFFF"};
    const std::array<const char *, 16> lightPalette{
        "#1B1D21", "#C0392B", "#2E7D4F", "#9A6B0F", "#2A5DB0", "#7B4FA8", "#0F7C82", "#B9BDC4",
        "#6A7078", "#E05252", "#3FA46A", "#B8892F", "#4C87D8", "#9A6CCB", "#2AA3A8", "#101113"};

    const QColor terminalBackground = base.isValid() ? base : window;
    const QColor terminalForeground = palette.color(QPalette::Text);
    theme.terminal = terminalTheme(theme.name, terminalBackground,
                                   terminalForeground.isValid() ? terminalForeground : text,
                                   b.gold, rgba(b.gold, 90),
                                   dark ? darkPalette : lightPalette);
    return theme;
}

bool ThemeManager::isDark(const QString &id)
{
    return theme(id).dark;
}

QString ThemeManager::defaultId()
{
    return themes().first().id;
}

QString ThemeManager::normalizeId(const QString &id)
{
    static const QHash<QString, QString> legacy{
        {QStringLiteral("BlackCore Dark"), QStringLiteral("blackcore-dark")},
        {QStringLiteral("BlackCore Light"), QStringLiteral("blackcore-light")},
        {QStringLiteral("Classic"), QStringLiteral("blackcore-contrast")},
        {QStringLiteral("Системная"), QStringLiteral("system")},
        {QStringLiteral("System"), QStringLiteral("system")},
        {QStringLiteral("Чёрно-золотая"), QStringLiteral("blackcore-dark")},
    };

    if (legacy.contains(id))
        return legacy.value(id);

    for (const Theme &theme : themes()) {
        if (theme.id.compare(id, Qt::CaseInsensitive) == 0
            || theme.name.compare(id, Qt::CaseInsensitive) == 0)
            return theme.id;
    }
    return defaultId();
}

Theme ThemeManager::theme(const QString &id)
{
    const QString normalized = normalizeId(id);
    for (const Theme &theme : themes()) {
        if (theme.id == normalized)
            return theme;
    }
    return themes().first();
}

Theme &ThemeManager::currentRef()
{
    static Theme current = makeDark();
    return current;
}

Theme ThemeManager::current()
{
    return currentRef();
}

BrandPalette ThemeManager::colors()
{
    return currentRef().brand;
}

void ThemeManager::apply(const QString &id)
{
    apply(theme(id));
}

void ThemeManager::apply(const Theme &theme)
{
    currentRef() = theme;

    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, theme.brand.background);
    palette.setColor(QPalette::WindowText, theme.brand.textPrimary);
    palette.setColor(QPalette::Base, theme.brand.panel);
    palette.setColor(QPalette::AlternateBase, theme.brand.active);
    palette.setColor(QPalette::Text, theme.brand.textPrimary);
    palette.setColor(QPalette::Button, theme.brand.panel);
    palette.setColor(QPalette::ButtonText, theme.brand.textPrimary);
    palette.setColor(QPalette::Highlight, theme.brand.gold);
    palette.setColor(QPalette::HighlightedText, theme.brand.background);
    palette.setColor(QPalette::ToolTipBase, theme.brand.panel);
    palette.setColor(QPalette::ToolTipText, theme.brand.textPrimary);
    palette.setColor(QPalette::PlaceholderText, theme.brand.textSecondary);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.brand.textSecondary);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.brand.textSecondary);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.brand.textSecondary);

    QApplication::setPalette(palette);
    qApp->setStyleSheet(styleSheet(theme));
}

QString ThemeManager::styleSheet(const Theme &theme)
{
    const BrandPalette &b = theme.brand;

    // Токены вместо позиционных %N: так порядок подстановок не влияет на результат
    QString sheet = QStringLiteral(R"(
/* ---------- база ---------- */
QMainWindow, QDialog {
    background-color: @bg;
    color: @text;
}
QWidget { color: @text; }
QToolTip {
    background-color: @panel;
    color: @text;
    border: 1px solid @gold;
    padding: 4px 6px;
}
QLabel { background: transparent; }
QLabel[role="secondary"] { color: @textDim; }
QLabel[role="header"] {
    color: @gold;
    font-weight: 600;
    font-size: 10px;
    letter-spacing: 1px;
    padding: 6px 0 2px 0;
}
QLabel[role="success"] { color: @success; }
QLabel[role="error"] { color: @error; }

/* ---------- меню ---------- */
QMenuBar {
    background-color: @panel;
    color: @textDim;
    border: none;
    border-bottom: 1px solid @active;
    padding: 1px 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 5px 10px;
    border-radius: 4px;
}
QMenuBar::item:selected { background: @active; color: @goldLight; }
QMenuBar::item:pressed  { background: @active; color: @goldLight; }

QMenu {
    background-color: @panel;
    border: 1px solid @active;
    padding: 4px;
}
QMenu::item {
    padding: 6px 26px 6px 22px;
    border-radius: 4px;
    color: @text;
}
QMenu::item:selected { background-color: @active; color: @goldLight; }
QMenu::item:disabled { color: @textDim; }
QMenu::separator { height: 1px; background: @active; margin: 4px 8px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }

/* ---------- статус-строка ---------- */
QStatusBar {
    background-color: @panel;
    color: @textDim;
    border-top: 1px solid @active;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @textDim; padding: 1px 6px; }

/* ---------- вкладки ---------- */
QTabWidget::pane {
    border: 1px solid @active;
    border-radius: 6px;
    top: -1px;
}
QTabWidget::tab-bar { left: 2px; }
QTabBar { background: transparent; }
QTabBar::tab {
    background-color: @panel;
    color: @textDim;
    border: 1px solid @active;
    border-bottom: 2px solid transparent;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 5px 12px;
    margin: 4px 3px 0 0;
    min-width: 90px;
}
QTabBar::tab:hover { color: @text; background-color: @hover; }
QTabBar::tab:selected {
    background-color: @bg;
    color: @gold;
    border-bottom: 2px solid @gold;
}

/* ---------- кнопки ---------- */
QPushButton, QToolButton {
    background-color: @panel;
    color: @text;
    border: 1px solid @active;
    border-radius: 6px;
    padding: 6px 14px;
}
QPushButton:hover, QToolButton:hover {
    background-color: @hover;
    border-color: @gold;
    color: @goldLight;
}
QPushButton:pressed, QToolButton:pressed { background-color: @active; }
QPushButton:default {
    background-color: @gold;
    color: @bg;
    border-color: @gold;
    font-weight: 600;
}
QPushButton:default:hover { background-color: @goldLight; border-color: @goldLight; }
QPushButton:disabled, QToolButton:disabled { color: @textDim; border-color: @active; }

/* кнопки управления окном */
QToolButton#windowButton {
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 0px;
    margin: 0px;
    color: @textDim;
}
QToolButton#windowButton:hover { background-color: @active; color: @goldLight; }
QToolButton#closeButton {
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 0px;
    margin: 0px;
    color: @textDim;
}
QToolButton#closeButton:hover { background-color: @error; color: @bg; }
QToolButton#sidebarToggle {
    background: transparent;
    border: none;
    border-radius: 4px;
    color: @textDim;
    padding: 2px;
}
QToolButton#sidebarToggle:hover { background-color: @active; color: @goldLight; }
QToolButton#sidebarToggle:checked { color: @gold; }

/* ---------- боковая панель ---------- */
QWidget#sideBar {
    background-color: @panel;
    border-right: 1px solid @active;
}
QWidget#sideBar QPushButton {
    text-align: left;
    padding: 7px 10px;
    background-color: transparent;
    border: 1px solid transparent;
}
QWidget#sideBar QPushButton:hover {
    background-color: @active;
    border-color: @gold;
    color: @goldLight;
}
QListWidget#sessionList {
    background: transparent;
    border: none;
    outline: none;
}
QListWidget#sessionList::item {
    padding: 7px 10px;
    border-radius: 6px;
    color: @textDim;
    margin: 1px 4px;
}
QListWidget#sessionList::item:hover { background-color: @active; color: @text; }
QListWidget#sessionList::item:selected {
    background-color: @active;
    color: @goldLight;
}

/* ---------- поля ввода ---------- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox {
    background-color: @panel;
    color: @text;
    border: 1px solid @active;
    border-radius: 6px;
    padding: 5px 8px;
    selection-background-color: @gold;
    selection-color: @bg;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QFontComboBox:focus {
    border-color: @gold;
}
QComboBox::drop-down, QFontComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background-color: @panel;
    color: @text;
    border: 1px solid @active;
    selection-background-color: @active;
    selection-color: @goldLight;
    outline: none;
}
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    background-color: @active;
    border: none;
    width: 14px;
}

/* ---------- флажки и переключатели ---------- */
QCheckBox, QRadioButton { spacing: 8px; color: @text; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 15px;
    height: 15px;
    border: 1px solid @active;
    border-radius: 4px;
    background: @panel;
}
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background-color: @gold;
    border-color: @gold;
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: @gold; }

/* ---------- слайдеры ---------- */
QSlider::groove:horizontal {
    height: 4px;
    background: @active;
    border-radius: 2px;
}
QSlider::sub-page:horizontal { background: @gold; border-radius: 2px; }
QSlider::handle:horizontal {
    background: @gold;
    width: 14px;
    height: 14px;
    margin: -6px 0;
    border-radius: 7px;
}
QSlider::handle:horizontal:hover { background: @goldLight; }

/* ---------- группы ---------- */
QGroupBox {
    border: 1px solid @active;
    border-radius: 8px;
    margin-top: 14px;
    padding-top: 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 6px;
    color: @gold;
    font-weight: 600;
}

/* ---------- прокрутка ---------- */
QScrollBar:vertical {
    background: transparent;
    width: 11px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: @active;
    min-height: 24px;
    border-radius: 5px;
    margin: 2px;
}
QScrollBar::handle:vertical:hover { background: @gold; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 11px; }
QScrollBar::handle:horizontal { background: @active; min-width: 24px; border-radius: 5px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: @gold; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

/* ---------- разделитель ---------- */
QSplitter::handle:horizontal { background: @active; width: 1px; }
QSplitter::handle:horizontal:hover { background: @gold; }
QFrame[role="separator"] { background: @active; max-height: 1px; border: none; }

/* ---------- компактные вкладки правых панелей ---------- */
#devPanel QTabBar::tab, #monitorPanel QTabBar::tab {
    min-width: 52px;
    padding: 5px 8px;
    margin: 4px 2px 0 0;
}

/* ---------- вкладки окна настроек ---------- */
#settingsTabs QTabBar::tab {
    padding: 6px 10px;
    margin: 4px 2px 0 0;
}

/* ---------- командная палитра ---------- */
#commandPalette {
    background-color: @panel;
    border: 1px solid @gold;
    border-radius: 8px;
}
#commandPalette #paletteSearch {
    padding: 8px 10px;
    font-size: 13px;
}
#commandPalette QListWidget {
    background-color: @bg;
    border: 1px solid @active;
    border-radius: 6px;
}
#commandPalette QListWidget::item {
    padding: 6px 8px;
    border-radius: 4px;
}
#commandPalette QListWidget::item:selected {
    background-color: @active;
    color: @goldLight;
}

/* ---------- уведомления ---------- */
#notificationToast QLabel { background: transparent; }

/* ---------- таблицы ---------- */
QTableWidget, QTableView {
    background-color: @panel;
    alternate-background-color: @hover;
    border: 1px solid @active;
    border-radius: 6px;
    gridline-color: @active;
    selection-background-color: @active;
    selection-color: @goldLight;
}
QTableWidget::item, QTableView::item { padding: 2px 4px; }
QTableCornerButton::section { background-color: @panel; border: none; }
QHeaderView::section {
    background-color: @panel;
    color: @textDim;
    border: none;
    border-bottom: 1px solid @active;
    padding: 4px;
    font-weight: 600;
}

/* ---------- индикаторы ---------- */
QProgressBar {
    background-color: @panel;
    border: 1px solid @active;
    border-radius: 5px;
    max-height: 8px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk { background-color: @gold; border-radius: 4px; }
QProgressBar[role="warn"]::chunk { background-color: @error; }

/* ---------- журнал и текст ---------- */
QPlainTextEdit, QTextEdit {
    background-color: @panel;
    color: @text;
    border: 1px solid @active;
    border-radius: 6px;
    selection-background-color: @gold;
    selection-color: @bg;
}

/* ---------- прочее ---------- */
QHeaderView::section {
    background-color: @panel;
    color: @textDim;
    border: none;
    padding: 4px;
}
QToolBar { background: @panel; border: none; }
)");

    sheet.replace(QStringLiteral("@bg"), b.background.name());
    sheet.replace(QStringLiteral("@panel"), b.panel.name());
    sheet.replace(QStringLiteral("@active"), b.active.name());
    sheet.replace(QStringLiteral("@goldLight"), b.goldLight.name());
    sheet.replace(QStringLiteral("@gold"), b.gold.name());
    sheet.replace(QStringLiteral("@textDim"), b.textSecondary.name());
    sheet.replace(QStringLiteral("@text"), b.textPrimary.name());
    sheet.replace(QStringLiteral("@success"), b.success.name());
    sheet.replace(QStringLiteral("@error"), b.error.name());
    sheet.replace(QStringLiteral("@hover"), b.active.lighter(theme.dark ? 118 : 104).name());
    return sheet;
}

} // namespace BlackCore
