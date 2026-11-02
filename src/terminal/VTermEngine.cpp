// SPDX-License-Identifier: MIT
#include "VTermEngine.h"

#include "../core/Logger.h"

#include <QByteArray>
#include <QRegularExpression>

#include <cstring>

namespace BlackCore {

// ---------------------------------------------------------------------------
// Колбэки libvterm (static) → перенаправление в экземпляр
// ---------------------------------------------------------------------------

int VTermEngine::onDamage(VTermRect rect, void *user)
{
    auto *self = static_cast<VTermEngine *>(user);
    const int first = qBound(0, rect.start_row, self->m_rows);
    const int last = qBound(0, rect.end_row, self->m_rows);
    for (int row = first; row < last; ++row) {
        if (!self->m_dirtyRows.contains(row))
            self->m_dirtyRows.append(row);
    }
    return 1;
}

int VTermEngine::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    auto *self = static_cast<VTermEngine *>(user);
    auto addRow = [self](int row) {
        if (row >= 0 && row < self->m_rows && !self->m_dirtyRows.contains(row))
            self->m_dirtyRows.append(row);
    };

    self->m_cursor.row = pos.row;
    self->m_cursor.col = pos.col;
    if (visible >= 0)
        self->m_cursorVisible = (visible != 0);

    addRow(oldpos.row);
    addRow(pos.row);
    emit self->cursorChanged();
    return 1;
}

int VTermEngine::onSetTermProp(VTermProp prop, VTermValue *value, void *user)
{
    auto *self = static_cast<VTermEngine *>(user);
    if (!value)
        return 1;

    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        self->m_cursorVisible = (value->boolean != 0);
        emit self->cursorChanged();
        break;
    case VTERM_PROP_CURSORBLINK:
        self->m_cursorBlink = (value->boolean != 0);
        emit self->cursorChanged();
        break;
    case VTERM_PROP_CURSORSHAPE:
        self->m_cursorShape = value->number;
        emit self->cursorChanged();
        break;
    case VTERM_PROP_ALTSCREEN: {
        const bool enabled = (value->boolean != 0);
        if (self->m_alternateScreen != enabled) {
            self->m_alternateScreen = enabled;
            self->markAllDirty();
            emit self->alternateScreenChanged(enabled);
        }
        break;
    }
    case VTERM_PROP_TITLE: {
        if (value->string.initial)
            self->m_titleBuffer.clear();
        if (value->string.str && value->string.len > 0)
            self->m_titleBuffer.append(value->string.str, static_cast<int>(value->string.len));
        if (value->string.final) {
            const QString title = QString::fromUtf8(self->m_titleBuffer).trimmed();
            self->m_titleBuffer.clear();
            if (self->m_title != title) {
                self->m_title = title;
                emit self->titleChanged(title);
            }
        }
        break;
    }
    case VTERM_PROP_MOUSE:
        self->m_mouseMode = value->number;
        break;
    default:
        break;
    }
    return 1;
}

int VTermEngine::onBell(void *user)
{
    auto *self = static_cast<VTermEngine *>(user);
    emit self->bell();
    return 1;
}

int VTermEngine::onResize(int rows, int cols, void *user)
{
    Q_UNUSED(rows);
    Q_UNUSED(cols);
    auto *self = static_cast<VTermEngine *>(user);
    self->markAllDirty();
    return 1;
}

int VTermEngine::appendScrollbackLine(int cols, const VTermScreenCell *cells, bool continuation)
{
    if (!cells || cols <= 0 || m_scrollbackLimit <= 0)
        return 1;

    ScrollbackLine line;
    line.cells.resize(cols);
    line.continuation = continuation;
    for (int i = 0; i < cols; ++i)
        line.cells[i] = fromScreenCell(cells[i]);

    m_scrollback.push_back(std::move(line));
    trimScrollback();
    emit scrollbackChanged();
    return 1;
}

int VTermEngine::onPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    // Совместимость с системной libvterm, где sb_pushline4 отсутствует.
    return static_cast<VTermEngine *>(user)->appendScrollbackLine(cols, cells, false);
}

#ifdef BLACKCORE_VTERM_PUSHLINE4
int VTermEngine::onPushLine4(int cols, const VTermScreenCell *cells,
                             bool continuation, void *user)
{
    // Расширенный callback сообщает continuation вытесняемой, а не следующей строки.
    return static_cast<VTermEngine *>(user)->appendScrollbackLine(cols, cells, continuation);
}
#endif

int VTermEngine::onPopLine(int cols, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<VTermEngine *>(user);
    if (self->m_scrollback.empty() || !cells)
        return 0;

    const QVector<FlatCell> line = std::move(self->m_scrollback.back().cells);
    self->m_scrollback.pop_back();

    auto unpack = [](quint32 packed, VTermColor *color, bool foreground) {
        Q_UNUSED(foreground);
        switch (packed & ColorKindMask) {
        case ColorKindIndexed:
            vterm_color_indexed(color, static_cast<uint8_t>(packed & ColorValueMask));
            break;
        case ColorKindDefaultFg:
        case ColorKindDefaultBg:
            vterm_color_rgb(color, 230, 230, 230);
            color->type |= ((packed & ColorKindMask) == ColorKindDefaultFg) ? VTERM_COLOR_DEFAULT_FG
                                                                           : VTERM_COLOR_DEFAULT_BG;
            break;
        default:
            vterm_color_rgb(color,
                            static_cast<uint8_t>((packed >> 16) & 0xFF),
                            static_cast<uint8_t>((packed >> 8) & 0xFF),
                            static_cast<uint8_t>(packed & 0xFF));
            break;
        }
    };

    for (int i = 0; i < cols; ++i) {
        VTermScreenCell &cell = cells[i];
        std::memset(&cell, 0, sizeof(VTermScreenCell));
        const FlatCell src = i < line.size() ? line[i] : FlatCell{};
        cell.chars[0] = src.ch != 0 ? static_cast<uint32_t>(src.ch) : ' ';
        cell.width = 1;
        cell.attrs.bold = (src.flags & CellBold) ? 1 : 0;
        cell.attrs.underline = (src.flags & CellUnderline) ? VTERM_UNDERLINE_SINGLE : 0;
        cell.attrs.italic = (src.flags & CellItalic) ? 1 : 0;
        cell.attrs.blink = (src.flags & CellBlink) ? 1 : 0;
        cell.attrs.reverse = (src.flags & CellReverse) ? 1 : 0;
        cell.attrs.conceal = (src.flags & CellConceal) ? 1 : 0;
        cell.attrs.strike = (src.flags & CellStrike) ? 1 : 0;
        unpack(src.fg, &cell.fg, true);
        unpack(src.bg, &cell.bg, false);
    }

    emit self->scrollbackChanged();
    return 1;
}

int VTermEngine::onSbClear(void *user)
{
    auto *self = static_cast<VTermEngine *>(user);
    if (!self->m_scrollback.empty()) {
        self->m_scrollback.clear();
        emit self->scrollbackChanged();
    }
    return 1;
}

void VTermEngine::onOutput(const char *bytes, size_t len, void *user)
{
    auto *self = static_cast<VTermEngine *>(user);
    if (!bytes || len == 0)
        return;
    // Ответы эмулятора (DSR, DA, OSC-ответы) отправляем в PTY пакетом
    self->m_pendingOutput.append(bytes, static_cast<int>(len));
}

// ---------------------------------------------------------------------------
// Инициализация
// ---------------------------------------------------------------------------

VTermEngine::VTermEngine(QObject *parent)
    : QObject(parent)
{
    m_theme = ColorTheme::byName(QStringLiteral("BlackCore Dark"));

    m_vt = vterm_new(m_rows, m_cols);
    if (!m_vt) {
        Log::error(tr("Не удалось создать экземпляр libvterm"));
        return;
    }
    vterm_set_utf8(m_vt, 1);

    m_state = vterm_obtain_state(m_vt);
    m_screen = vterm_obtain_screen(m_vt);

    m_screenCallbacks.damage = &VTermEngine::onDamage;
    m_screenCallbacks.moverect = nullptr;
    m_screenCallbacks.movecursor = &VTermEngine::onMoveCursor;
    m_screenCallbacks.settermprop = &VTermEngine::onSetTermProp;
    m_screenCallbacks.bell = &VTermEngine::onBell;
    m_screenCallbacks.resize = &VTermEngine::onResize;
    m_screenCallbacks.sb_pushline = &VTermEngine::onPushLine;
#ifdef BLACKCORE_VTERM_PUSHLINE4
    m_screenCallbacks.sb_pushline4 = &VTermEngine::onPushLine4;
#endif
    m_screenCallbacks.sb_popline = &VTermEngine::onPopLine;
    m_screenCallbacks.sb_clear = &VTermEngine::onSbClear;
    vterm_screen_set_callbacks(m_screen, &m_screenCallbacks, this);
#ifdef BLACKCORE_VTERM_PUSHLINE4
    vterm_screen_callbacks_has_pushline4(m_screen);
#endif

    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_ROW);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_reset(m_screen, 1);

    vterm_output_set_callback(m_vt, &VTermEngine::onOutput, this);
    vterm_state_set_bold_highbright(m_state, 1);

    applyThemeToVTerm();
    markAllDirty();
}

VTermEngine::~VTermEngine()
{
    m_screen = nullptr;
    m_state = nullptr;
    if (m_vt)
        vterm_free(m_vt);
    m_vt = nullptr;
}

void VTermEngine::setTheme(const ColorTheme &theme)
{
    m_theme = theme;
    applyThemeToVTerm();
    markAllDirty();
    emit damage();
}

void VTermEngine::applyThemeToVTerm()
{
    if (!m_state || !m_screen)
        return;

    VTermColor fg;
    VTermColor bg;
    vterm_color_rgb(&fg, static_cast<uint8_t>(m_theme.foreground.red()),
                    static_cast<uint8_t>(m_theme.foreground.green()),
                    static_cast<uint8_t>(m_theme.foreground.blue()));
    vterm_color_rgb(&bg, static_cast<uint8_t>(m_theme.background.red()),
                    static_cast<uint8_t>(m_theme.background.green()),
                    static_cast<uint8_t>(m_theme.background.blue()));
    vterm_state_set_default_colors(m_state, &fg, &bg);
    vterm_screen_set_default_colors(m_screen, &fg, &bg);

    for (int i = 0; i < 256; ++i) {
        const QColor color = m_theme.colorByIndex(i);
        VTermColor vtermColor;
        vterm_color_rgb(&vtermColor, static_cast<uint8_t>(color.red()),
                        static_cast<uint8_t>(color.green()), static_cast<uint8_t>(color.blue()));
        vterm_state_set_palette_color(m_state, i, &vtermColor);
    }
}

void VTermEngine::setScrollbackLimit(int lines)
{
    m_scrollbackLimit = qMax(0, lines);
    trimScrollback();
}

void VTermEngine::trimScrollback()
{
    bool trimmed = false;
    while (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit) {
        m_scrollback.pop_front();
        trimmed = true;
    }
    if (trimmed)
        emit scrollbackChanged();
}

void VTermEngine::resize(int rows, int cols)
{
    rows = qMax(1, rows);
    cols = qMax(1, cols);
    if (rows == m_rows && cols == m_cols)
        return;

    m_rows = rows;
    m_cols = cols;
    if (m_vt)
        vterm_set_size(m_vt, rows, cols);

    markAllDirty();
    emit damage();
}

void VTermEngine::feed(const QByteArray &data)
{
    if (!m_vt || data.isEmpty())
        return;

    vterm_input_write(m_vt, data.constData(), static_cast<size_t>(data.size()));
    vterm_screen_flush_damage(m_screen);
    flushPendingOutput();
    emitDamageIfNeeded();
}

void VTermEngine::flushPendingOutput()
{
    if (m_pendingOutput.isEmpty())
        return;
    const QByteArray payload = m_pendingOutput;
    m_pendingOutput.clear();
    emit dataToSend(payload);
}

void VTermEngine::markAllDirty()
{
    m_fullDamage = true;
    m_dirtyRows.clear();
    for (int row = 0; row < m_rows; ++row)
        m_dirtyRows.append(row);
}

void VTermEngine::emitDamageIfNeeded()
{
    if (m_fullDamage || !m_dirtyRows.isEmpty())
        emit damage();
}

QVector<int> VTermEngine::takeDirtyRows()
{
    const QVector<int> rows = QVector<int>::fromList(m_dirtyRows);
    m_dirtyRows.clear();
    m_fullDamage = false;
    return rows;
}

QRgb VTermEngine::resolveColor(quint32 packed, bool foreground) const
{
    Q_UNUSED(foreground);
    switch (packed & ColorKindMask) {
    case ColorKindIndexed:
        return m_theme.colorByIndex(static_cast<int>(packed & ColorValueMask)).rgb();
    case ColorKindDefaultFg:
        return m_theme.foreground.rgb();
    case ColorKindDefaultBg:
        return m_theme.background.rgb();
    default:
        return 0xFF000000u | (packed & ColorValueMask);
    }
}

quint32 VTermEngine::packScreenColor(const VTermColor &color, bool foreground) const
{
    if (foreground ? VTERM_COLOR_IS_DEFAULT_FG(&color) : VTERM_COLOR_IS_DEFAULT_BG(&color))
        return foreground ? ColorKindDefaultFg : ColorKindDefaultBg;
    if (VTERM_COLOR_IS_INDEXED(&color))
        return ColorKindIndexed | color.indexed.idx;
    return packColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}

FlatCell VTermEngine::fromScreenCell(const VTermScreenCell &cell) const
{
    FlatCell flat;

    // libvterm помечает вторую половину широкого символа значением (uint32_t)-1
    if (cell.chars[0] == static_cast<uint32_t>(-1)) {
        flat.ch = U' ';
        flat.width = 0;
        flat.fg = packScreenColor(cell.fg, true);
        flat.bg = packScreenColor(cell.bg, false);
        if (cell.attrs.reverse)
            std::swap(flat.fg, flat.bg);
        return flat;
    }

    flat.ch = cell.chars[0] ? static_cast<char32_t>(cell.chars[0]) : U' ';
    flat.width = cell.width > 0 ? static_cast<quint8>(cell.width) : 1;
    flat.fg = packScreenColor(cell.fg, true);
    flat.bg = packScreenColor(cell.bg, false);
    if (cell.attrs.bold)
        flat.flags |= CellBold;
    if (cell.attrs.underline)
        flat.flags |= CellUnderline;
    if (cell.attrs.italic)
        flat.flags |= CellItalic;
    if (cell.attrs.blink)
        flat.flags |= CellBlink;
    if (cell.attrs.conceal)
        flat.flags |= CellConceal;
    if (cell.attrs.strike)
        flat.flags |= CellStrike;

    // Инверсию применяем сразу к цветам: модель отдаёт уже «эффективные» цвета,
    // поэтому экран и скроллбэк выглядят одинаково.
    if (cell.attrs.reverse)
        std::swap(flat.fg, flat.bg);

    return flat;
}

FlatCell VTermEngine::cellAt(int line, int column) const
{
    FlatCell result;
    if (line < 0 || line >= totalLines() || column < 0 || column >= m_cols)
        return result;

    const int scrollback = scrollbackCount();
    if (line < scrollback) {
        const QVector<FlatCell> &row = m_scrollback[static_cast<size_t>(line)].cells;
        if (column < row.size())
            return row[column];
        return result;
    }

    if (!m_screen)
        return result;

    VTermScreenCell cell;
    std::memset(&cell, 0, sizeof(cell));
    const VTermPos pos{line - scrollback, column};
    if (!vterm_screen_get_cell(m_screen, pos, &cell))
        return result;

    return fromScreenCell(cell);
}

QString VTermEngine::lineText(int line, bool trimRight) const
{
    QString text;
    text.reserve(m_cols);
    for (int column = 0; column < m_cols; ++column) {
        const FlatCell cell = cellAt(line, column);
        if (cell.width == 0) {
            // продолжение широкого символа — сохраняем однозначное соответствие колонкам
            text.append(QLatin1Char(' '));
            continue;
        }
        const uint codepoint = static_cast<uint>(cell.ch);
        const bool validCodepoint = (codepoint < 0xD800u) || (codepoint > 0xDFFFu && codepoint < 0x110000u);
        if (validCodepoint)
            text.append(QChar::fromUcs4(codepoint));
        else
            text.append(QLatin1Char(' '));
    }
    if (trimRight) {
        int end = text.size();
        while (end > 0 && (text.at(end - 1) == QLatin1Char(' ') || text.at(end - 1) == QLatin1Char('\t')))
            --end;
        text.truncate(end);
    }
    return text;
}

bool VTermEngine::lineContinuesPrevious(int line) const
{
    if (line <= 0 || line >= totalLines())
        return false;

    if (line < scrollbackCount())
        return m_scrollback[static_cast<size_t>(line)].continuation;

    if (!m_state)
        return false;

    const int screenRow = line - scrollbackCount();
    const VTermLineInfo *info = vterm_state_get_lineinfo(m_state, screenRow);
    return info && info->continuation;
}

// ---------------------------------------------------------------------------
// Ввод
// ---------------------------------------------------------------------------

void VTermEngine::sendText(const QByteArray &bytes)
{
    if (!bytes.isEmpty())
        emit dataToSend(bytes);
}

void VTermEngine::sendKey(int vtermKey, int modifiers)
{
    if (!m_vt)
        return;
    vterm_keyboard_key(m_vt, static_cast<VTermKey>(vtermKey), static_cast<VTermModifier>(modifiers));
    flushPendingOutput();
}

void VTermEngine::sendUnicode(uint codepoint, int modifiers)
{
    if (!m_vt)
        return;
    vterm_keyboard_unichar(m_vt, codepoint, static_cast<VTermModifier>(modifiers));
    flushPendingOutput();
}

void VTermEngine::setPasteMode(bool enabled)
{
    if (!m_vt)
        return;
    if (enabled)
        vterm_keyboard_start_paste(m_vt);
    else
        vterm_keyboard_end_paste(m_vt);
    flushPendingOutput();
}

void VTermEngine::sendMouseMove(int row, int col, int modifiers)
{
    if (!m_vt)
        return;
    vterm_mouse_move(m_vt, row, col, static_cast<VTermModifier>(modifiers));
    flushPendingOutput();
}

void VTermEngine::sendMouseButton(int button, bool pressed, int modifiers)
{
    if (!m_vt)
        return;
    vterm_mouse_button(m_vt, button, pressed, static_cast<VTermModifier>(modifiers));
    flushPendingOutput();
}

} // namespace BlackCore
