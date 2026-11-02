// SPDX-License-Identifier: MIT
#pragma once

#include "../core/ColorTheme.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include <deque>

#include <vterm.h>

namespace BlackCore {

//! Флаги атрибутов ячейки
enum CellFlag : quint8 {
    CellBold = 0x01,
    CellUnderline = 0x02,
    CellItalic = 0x04,
    CellReverse = 0x08,
    CellStrike = 0x10,
    CellBlink = 0x20,
    CellConceal = 0x40,
};

//! Компактное представление цвета: старший байт — вид, младшие 24 бита — значение
enum PackedColorKind : quint32 {
    ColorKindRgb = 0x00000000u,
    ColorKindIndexed = 0x01000000u,
    ColorKindDefaultFg = 0x02000000u,
    ColorKindDefaultBg = 0x03000000u,
    ColorKindMask = 0xFF000000u,
    ColorValueMask = 0x00FFFFFFu,
};

//! Ячейка экрана/скроллбэка (12 байт)
struct FlatCell
{
    char32_t ch = U' ';
    quint32 fg = ColorKindDefaultFg;
    quint32 bg = ColorKindDefaultBg;
    quint8 flags = 0;
    quint8 width = 1;
};

//! Слой эмуляции терминала на основе libvterm.
/*!
 *  Отвечает за разбор escape-последовательностей, состояние экрана,
 *  скроллбэк и преобразование клавиатурного ввода в байты для PTY.
 *  Не знает ничего о виджетах — можно тестировать отдельно.
 */
class VTermEngine : public QObject
{
    Q_OBJECT

public:
    explicit VTermEngine(QObject *parent = nullptr);
    ~VTermEngine() override;

    void setTheme(const ColorTheme &theme);
    const ColorTheme &theme() const { return m_theme; }

    void setScrollbackLimit(int lines);
    int scrollbackLimit() const { return m_scrollbackLimit; }

    void resize(int rows, int cols);
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    //! Подать байты, полученные от PTY
    void feed(const QByteArray &data);

    // --- ввод ---
    void sendText(const QByteArray &bytes);
    void sendKey(int vtermKey, int modifiers);
    void sendUnicode(uint codepoint, int modifiers);
    void setPasteMode(bool enabled);
    void sendMouseMove(int row, int col, int modifiers);
    void sendMouseButton(int button, bool pressed, int modifiers);

    // --- модель ---
    int scrollbackCount() const { return static_cast<int>(m_scrollback.size()); }
    int totalLines() const { return scrollbackCount() + m_rows; }
    FlatCell cellAt(int line, int column) const;
    QString lineText(int line, bool trimRight = true) const;
    //! Истинно, если строка (включая историю) является автоматическим продолжением предыдущей.
    //! Для истории признак доступен при сборке с расширенным sb_pushline4.
    bool lineContinuesPrevious(int line) const;

    int cursorLine() const { return m_cursor.row + scrollbackCount(); }
    int cursorColumn() const { return m_cursor.col; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool cursorBlink() const { return m_cursorBlink; }
    int cursorShape() const { return m_cursorShape; }

    bool isAlternateScreen() const { return m_alternateScreen; }
    int mouseTrackingMode() const { return m_mouseMode; }
    QString title() const { return m_title; }

    //! Забрать накопленные «грязные» строки экрана (индексы 0..rows-1)
    QVector<int> takeDirtyRows();
    bool hasDamage() const { return m_fullDamage || !m_dirtyRows.empty(); }

    QRgb resolveColor(quint32 packed, bool foreground) const;
    static quint32 packColor(quint32 r, quint32 g, quint32 b) { return ColorKindRgb | ((r << 16) | (g << 8) | b); }

signals:
    void dataToSend(const QByteArray &data);
    void damage();
    void cursorChanged();
    void titleChanged(const QString &title);
    void bell();
    void alternateScreenChanged(bool enabled);
    void scrollbackChanged();

private slots:
    void flushPendingOutput();

private:
    // Колбэки libvterm
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int onSetTermProp(VTermProp prop, VTermValue *value, void *user);
    static int onBell(void *user);
    static int onResize(int rows, int cols, void *user);
    static int onPushLine(int cols, const VTermScreenCell *cells, void *user);
#ifdef BLACKCORE_VTERM_PUSHLINE4
    static int onPushLine4(int cols, const VTermScreenCell *cells, bool continuation, void *user);
#endif
    static int onPopLine(int cols, VTermScreenCell *cells, void *user);
    static int onSbClear(void *user);
    static void onOutput(const char *bytes, size_t len, void *user);

    void markAllDirty();
    FlatCell fromScreenCell(const VTermScreenCell &cell) const;
    quint32 packScreenColor(const VTermColor &color, bool foreground) const;
    void applyThemeToVTerm();
    void trimScrollback();
    int appendScrollbackLine(int cols, const VTermScreenCell *cells, bool continuation);
    void emitDamageIfNeeded();

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    VTermState *m_state = nullptr;
    //! libvterm хранит указатель на структуру колбэков, поэтому она должна
    //! жить не меньше самого VTermScreen (локальная переменная недопустима).
    VTermScreenCallbacks m_screenCallbacks{};

    ColorTheme m_theme;
    int m_rows = 24;
    int m_cols = 80;
    int m_scrollbackLimit = 3000;

    struct ScrollbackLine
    {
        QVector<FlatCell> cells;
        bool continuation = false;
    };
    std::deque<ScrollbackLine> m_scrollback;

    struct CursorPos
    {
        int row = 0;
        int col = 0;
    } m_cursor;
    bool m_cursorVisible = true;
    bool m_cursorBlink = true;
    int m_cursorShape = 1; // 1 — блок, 2 — подчёркивание, 3 — вертикальная черта
    bool m_alternateScreen = false;
    int m_mouseMode = 0;
    QString m_title;
    QByteArray m_titleBuffer;
    QByteArray m_pendingOutput;

    QList<int> m_dirtyRows;
    bool m_fullDamage = true;
    bool m_emittingData = false;
};

} // namespace BlackCore
