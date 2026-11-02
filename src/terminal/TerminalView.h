// SPDX-License-Identifier: MIT
#pragma once

#include "../core/ColorTheme.h"
#include "../core/Settings.h"
#include "../core/Theme.h"

#include <QFont>
#include <QPoint>
#include <QString>
#include <QVector>
#include <QWidget>

class QScrollBar;
class QTimer;

namespace BlackCore {

class PtyProcess;
class VTermEngine;

//! Виджет терминала: отрисовка, ввод, выделение, прокрутка, масштаб.
class TerminalView : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget *parent = nullptr);
    ~TerminalView() override;

    //! Запустить оболочку в PTY
    bool startSession(QString *errorMessage = nullptr);
    bool isSessionRunning() const;
    //! Идентификатор процесса оболочки (-1, если сессия не запущена)
    qint64 shellProcessId() const;
    pid_t shellPid() const;

    //! Команда, выполняемая вместо интерактивной оболочки (используется для тестов)
    void setInitialCommand(const QString &command) { m_initialCommand = command; }

    //! Рабочий каталог, в котором будет запущена оболочка (пусто — из настроек)
    void setStartDirectory(const QString &directory) { m_startDirectory = directory; }
    //! Оболочка из профиля панели (пусто — из глобальных настроек)
    void setShellOverride(const QString &shell) { m_shellOverride = shell; }
    QString startDirectory() const { return m_startDirectory; }

    //! Фактический каталог процесса оболочки (через /proc/<pid>/cwd)
    QString workingDirectory() const;

    //! Профиль терминала: локальные шрифт/курсор/прокрутка без изменения глобальных настроек
    void applyProfileAppearance(const QString &fontFamily, int fontSize,
                                CursorStyle cursorStyle, int scrollback);

    // --- поведение прокрутки и курсора (этап 4) ---
    void setWheelScrollLines(int lines);
    int wheelScrollLines() const { return m_wheelScrollLines; }

    void setScrollOnOutput(bool enabled) { m_scrollOnOutput = enabled; }
    bool scrollOnOutput() const { return m_scrollOnOutput; }

    void setScrollOnKeystroke(bool enabled) { m_scrollOnKeystroke = enabled; }
    bool scrollOnKeystroke() const { return m_scrollOnKeystroke; }

    void setCursorBlink(bool enabled);
    bool cursorBlink() const { return m_cursorBlink; }

    //! Глубина буфера прокрутки (строк)
    void setScrollbackLimit(int lines);
    int scrollbackLimit() const;

    QString terminalTitle() const;
    QString shellTitle() const;

    // --- доступ к модели экрана (диагностика и автотесты) ---
    QString screenText() const;
    QString bufferText() const;
    int scrollbackCount() const;
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    //! Цвета ячейки на видимом экране (строка 0 — верхняя)
    QPair<QRgb, QRgb> colorsAt(int row, int column) const;
    bool isAlternateScreen() const;

    // --- операции ---
    void copySelection();
    void pasteClipboard();
    void pasteText(const QString &text);
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void setFontSize(int pointSize);
    int fontSize() const;
    void setFontFamily(const QString &family);
    QString fontFamily() const;

    //! Множитель высоты строки (1.0 … 1.6)
    void setLineSpacing(qreal spacing);
    qreal lineSpacing() const { return m_lineSpacing; }

    //! Отступ содержимого от краёв виджета, px
    void setPadding(int pixels);
    int padding() const { return m_padding; }

    //! Форма курсора: FollowApplication — как просит программа
    void setCursorStyle(CursorStyle style);
    CursorStyle cursorStyle() const { return m_cursorStyle; }

    void setTheme(const ColorTheme &theme);
    void applyTheme(const Theme &theme);
    void clearScreen();
    void clearScrollback();
    void selectAll();
    void scrollToBottom();
    void scrollPageUp();
    void scrollPageDown();
    void scrollLines(int lines);
    bool hasSelection() const { return m_hasSelection; }

    //! Отправить текст как если бы его напечатали
    void sendTextToShell(const QString &text);

    //! Прокрутить буфер так, чтобы указанная строка оказалась видимой (для поиска)
    void scrollToLine(int line);

signals:
    void titleChanged(const QString &title);
    void focusChanged(bool focused);
    void sessionFinished(int exitCode, int exitSignal);
    void sessionStarted();
    void fontSizeChanged(int pointSize);
    void copyAvailable(bool available);
    void selectionTextChanged(const QString &text);
    //! Получены данные от оболочки (используется отслеживанием задач)
    void outputReceived();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onPtyData(const QByteArray &data);
    void onPtyFinished(int exitCode, int exitSignal);
    void onEngineDamage();
    void onCursorBlinkTick();
    void onScrollBarValueChanged(int value);
    void onCursorChanged();

private:
    struct Run
    {
        int startColumn = 0;
        int columns = 0;
        QString text;
        QRgb fg = 0;
        QRgb bg = 0;
        quint8 flags = 0;
    };

    struct SelectionPoint
    {
        int line = 0;
        int column = 0;
    };

    void rebuildFont();
    void updateGridFromSize();
    void applyDamage();
    void scheduleRepaint();
    QVector<Run> buildRuns(int line) const;
    void drawRow(QPainter &painter, int line, int top, int selectionFrom, int selectionTo,
                 bool backgroundPass = true);
    void drawCursor(QPainter &painter);
    void updateScrollBar();
    void repositionScrollBar();
    int visibleFirstLine() const;
    SelectionPoint pointAt(const QPoint &position) const;
    void selectionRange(int line, int *from, int *to) const;
    void setSelection(SelectionPoint anchor, SelectionPoint cursor);
    void clearSelection();
    QString selectionText() const;
    void selectWordAt(SelectionPoint point);
    void selectLineAt(SelectionPoint point);
    void copyToClipboard(const QString &text);
    bool handleSpecialKey(QKeyEvent *event);
    void sendKeyBytes(const QByteArray &bytes);
    int terminalWidth() const;
    int terminalHeight() const;
    int leftMargin() const { return m_padding; }
    int topMargin() const { return m_padding; }
    int scrollBarWidth() const;
    void scrollToOffset(int offset);

    PtyProcess *m_pty = nullptr;
    VTermEngine *m_engine = nullptr;
    QScrollBar *m_scrollBar = nullptr;
    QTimer *m_blinkTimer = nullptr;

    QFont m_font;
    qreal m_cellWidth = 8.0;
    qreal m_cellHeight = 16.0;
    int m_ascent = 12;
    int m_rows = 24;
    int m_cols = 80;

    ColorTheme m_theme;
    qreal m_lineSpacing = 1.0;
    int m_padding = 6;
    CursorStyle m_cursorStyle = CursorStyle::FollowApplication;
    int m_scrollOffset = 0;
    bool m_cursorBlinkOn = true;
    bool m_cursorBlink = true;
    int m_wheelScrollLines = 3;
    bool m_scrollOnOutput = true;
    int m_lastScrollbackCount = 0;
    bool m_scrollOnKeystroke = true;
    bool m_repaintScheduled = false;
    QString m_preedit;

    bool m_hasSelection = false;
    SelectionPoint m_selectionAnchor;
    SelectionPoint m_selectionCursor;
    bool m_selecting = false;
    int m_selectionGranularity = 0; // 0 — символ, 1 — слово, 2 — строка

    QString m_initialCommand;
    QString m_startDirectory;
    QString m_shellOverride;
    bool m_autoScrollPending = false;
    bool m_updatingLayout = false;
};

} // namespace BlackCore
