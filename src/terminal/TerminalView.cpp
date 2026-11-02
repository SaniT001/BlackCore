// SPDX-License-Identifier: MIT
#include "TerminalView.h"

#include "../core/Logger.h"
#include "../core/Settings.h"
#include "../core/Theme.h"
#include "PtyProcess.h"
#include "VTermEngine.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace BlackCore {
namespace {

//! Кириллическая буква ЙЦУКЕН → соответствующая латинская клавиша.
QChar cyrillicToLatin(QChar ch)
{
    switch (ch.unicode()) {
    case 0x0439: return QLatin1Char('q'); // й
    case 0x0446: return QLatin1Char('w'); // ц
    case 0x0443: return QLatin1Char('e'); // у
    case 0x043A: return QLatin1Char('r'); // к
    case 0x0435: return QLatin1Char('t'); // е
    case 0x043D: return QLatin1Char('y'); // н
    case 0x0433: return QLatin1Char('u'); // г
    case 0x0448: return QLatin1Char('i'); // ш
    case 0x0449: return QLatin1Char('o'); // щ
    case 0x0437: return QLatin1Char('p'); // з
    case 0x0445: return QLatin1Char('['); // х
    case 0x044A: return QLatin1Char(']'); // ъ
    case 0x0444: return QLatin1Char('a'); // ф
    case 0x044B: return QLatin1Char('s'); // ы
    case 0x0432: return QLatin1Char('d'); // в
    case 0x0430: return QLatin1Char('f'); // а
    case 0x043F: return QLatin1Char('g'); // п
    case 0x0440: return QLatin1Char('h'); // р
    case 0x043E: return QLatin1Char('j'); // о
    case 0x043B: return QLatin1Char('k'); // л
    case 0x0434: return QLatin1Char('l'); // д
    case 0x0436: return QLatin1Char(';'); // ж
    case 0x044D: return QLatin1Char('\''); // э
    case 0x044F: return QLatin1Char('z'); // я
    case 0x0447: return QLatin1Char('x'); // ч
    case 0x0441: return QLatin1Char('c'); // с
    case 0x043C: return QLatin1Char('v'); // м
    case 0x0438: return QLatin1Char('b'); // и
    case 0x0442: return QLatin1Char('n'); // т
    case 0x044C: return QLatin1Char('m'); // ь
    case 0x0431: return QLatin1Char(','); // б
    case 0x044E: return QLatin1Char('.'); // ю
    default:
        return QChar();
    }
}

QChar upperLatin(QChar ch)
{
    if (ch.isNull())
        return ch;
    if (ch.unicode() >= 'a' && ch.unicode() <= 'z')
        return QChar(ch.unicode() - 'a' + 'A');
    return ch;
}

// Цвет выделения должен быть заметен и на тёмной, и на светлой теме.
// Полупрозрачный цвет сначала смешивается с основным фоном, после чего
// проверяется его фактическая яркость. При недостаточном контрасте
// используется контрастный оттенок, не меняющий настройки самой темы.
QColor visibleSelectionColor(const QColor &requested, const QColor &background)
{
    QColor blended = background;
    if (requested.isValid()) {
        const int alpha = requested.alpha();
        blended.setRgb((requested.red() * alpha + background.red() * (255 - alpha)) / 255,
                       (requested.green() * alpha + background.green() * (255 - alpha)) / 255,
                       (requested.blue() * alpha + background.blue() * (255 - alpha)) / 255);
    }

    if (std::abs(qGray(blended.rgb()) - qGray(background.rgb())) < 75) {
        return qGray(background.rgb()) < 128
            ? QColor(170, 198, 237) // светло-синий на тёмном фоне
            : QColor(53, 89, 146); // тёмно-синий на светлом фоне
    }
    return blended;
}

QColor selectionTextColor(const QColor &selectionBackground)
{
    return qGray(selectionBackground.rgb()) < 140 ? QColor(Qt::white) : QColor(Qt::black);
}

} // namespace

TerminalView::TerminalView(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);

    const Settings &settings = Settings::instance();
    m_theme = ThemeManager::theme(settings.themeId()).terminal;
    m_lineSpacing = settings.lineSpacing();
    m_padding = settings.padding();
    m_cursorStyle = settings.cursorStyle();
    m_cursorBlink = settings.cursorBlink();
    m_wheelScrollLines = settings.wheelScrollLines();
    m_scrollOnOutput = settings.scrollOnOutput();
    m_scrollOnKeystroke = settings.scrollOnKeystroke();

    m_engine = new VTermEngine(this);
    m_engine->setTheme(m_theme);
    m_engine->setScrollbackLimit(Settings::instance().scrollbackLines());

    m_pty = new PtyProcess(this);

    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setCursor(Qt::ArrowCursor);
    m_scrollBar->hide();
    connect(m_scrollBar, &QScrollBar::valueChanged, this, &TerminalView::onScrollBarValueChanged);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(530);
    connect(m_blinkTimer, &QTimer::timeout, this, &TerminalView::onCursorBlinkTick);

    connect(m_pty, &PtyProcess::dataReceived, this, &TerminalView::onPtyData);
    connect(m_pty, &PtyProcess::finished, this, &TerminalView::onPtyFinished);
    connect(m_pty, &PtyProcess::errorOccurred, this, [this](const QString &message) {
        Log::warn(tr("PTY: %1").arg(message));
    });

    connect(m_engine, &VTermEngine::damage, this, &TerminalView::onEngineDamage);
    connect(m_engine, &VTermEngine::cursorChanged, this, &TerminalView::onCursorChanged);
    connect(m_engine, &VTermEngine::scrollbackChanged, this, [this]() {
        updateScrollBar();
        scheduleRepaint();
    });
    connect(m_engine, &VTermEngine::alternateScreenChanged, this, [this](bool enabled) {
        Q_UNUSED(enabled);
        m_scrollOffset = 0;
        updateScrollBar();
        scheduleRepaint();
    });
    connect(m_engine, &VTermEngine::titleChanged, this, &TerminalView::titleChanged);
    connect(m_engine, &VTermEngine::bell, this, [this]() {
        if (Settings::instance().bellAudible())
            QApplication::beep();
    });
    connect(m_engine, &VTermEngine::dataToSend, this, [this](const QByteArray &data) {
        if (m_pty->isRunning())
            m_pty->writeData(data);
    });

    rebuildFont();
    updateGridFromSize();
}

TerminalView::~TerminalView() = default;

// ---------------------------------------------------------------------------
// Сессия
// ---------------------------------------------------------------------------

bool TerminalView::startSession(QString *errorMessage)
{
    const Settings &settings = Settings::instance();

    QString workingDirectory = m_startDirectory;
    if (workingDirectory.isEmpty())
        workingDirectory = settings.startDirectory();     // «Стартовый каталог» из настроек
    if (workingDirectory.isEmpty())
        workingDirectory = settings.lastWorkingDirectory();
    if (workingDirectory.isEmpty() || !QFileInfo(workingDirectory).isDir()) {
        if (!m_startDirectory.isEmpty())
            Log::warn(tr("Каталог %1 недоступен — используется домашний").arg(m_startDirectory));
        workingDirectory = QDir::homePath();
    }

    QStringList arguments;
    if (!m_initialCommand.isEmpty())
        arguments << QStringLiteral("-c") << m_initialCommand;

    QString error;
    const QString shell = m_shellOverride.isEmpty() ? settings.shell() : m_shellOverride;
    if (!m_pty->start(shell, arguments, workingDirectory,
                      QStringLiteral("xterm-256color"), &error)) {
        if (errorMessage)
            *errorMessage = error;
        else
            Log::error(tr("Не удалось запустить оболочку: %1").arg(error));
        return false;
    }

    updateGridFromSize();
    m_pty->setWindowSize(m_rows, m_cols);
    m_scrollOffset = 0;
    updateScrollBar();
    update();
    setFocus();
    emit sessionStarted();
    return true;
}

qint64 TerminalView::shellProcessId() const
{
    if (!m_pty || !m_pty->isRunning())
        return -1;
    return static_cast<qint64>(m_pty->pid());
}

bool TerminalView::isSessionRunning() const
{
    return m_pty && m_pty->isRunning();
}

pid_t TerminalView::shellPid() const
{
    return m_pty ? m_pty->pid() : -1;
}

QString TerminalView::workingDirectory() const
{
    const pid_t pid = shellPid();
    if (pid <= 0)
        return m_startDirectory.isEmpty() ? QDir::homePath() : m_startDirectory;

    const QString link = QStringLiteral("/proc/%1/cwd").arg(pid);
    const QString path = QFileInfo(link).isSymLink() ? QFile::symLinkTarget(link) : QString();
    const QFileInfo info(path);
    if (info.exists() && info.isDir())
        return info.absoluteFilePath();
    return m_startDirectory.isEmpty() ? QDir::homePath() : m_startDirectory;
}

void TerminalView::applyProfileAppearance(const QString &fontFamily, int fontSize,
                                          CursorStyle cursorStyle, int scrollback)
{
    bool changed = false;

    if (!fontFamily.isEmpty() && fontFamily != m_font.family()) {
        m_font = QFont(fontFamily);
        m_font.setStyleHint(QFont::Monospace);
        m_font.setFixedPitch(true);
        m_font.setKerning(false);
        changed = true;
    }
    if (fontSize >= Settings::kMinFontSize && fontSize <= Settings::kMaxFontSize
        && fontSize != m_font.pointSize()) {
        m_font.setPointSize(fontSize);
        changed = true;
    }
    if (changed) {
        rebuildFont();
        updateGridFromSize();
    }

    m_cursorStyle = cursorStyle;
    if (scrollback > 0 && m_engine)
        m_engine->setScrollbackLimit(scrollback);

    update();
}

void TerminalView::setScrollbackLimit(int lines)
{
    if (!m_engine)
        return;
    const int bounded = qBound(Settings::kMinScrollback, lines, Settings::kMaxScrollback);
    if (m_engine->scrollbackLimit() == bounded)
        return;
    m_engine->setScrollbackLimit(bounded);
    m_scrollOffset = qBound(0, m_scrollOffset, m_engine->scrollbackCount());
    updateScrollBar();
    update();
}

int TerminalView::scrollbackLimit() const
{
    return m_engine ? m_engine->scrollbackLimit() : Settings::instance().scrollbackLines();
}

void TerminalView::setWheelScrollLines(int lines)
{
    m_wheelScrollLines = qBound(Settings::kMinWheelLines, lines, Settings::kMaxWheelLines);
}

void TerminalView::setCursorBlink(bool enabled)
{
    if (m_cursorBlink == enabled)
        return;
    m_cursorBlink = enabled;
    if (!enabled) {
        m_blinkTimer->stop();
        m_cursorBlinkOn = true;
    } else if (hasFocus()) {
        m_cursorBlinkOn = true;
        m_blinkTimer->start();
    }
    update();
}

QString TerminalView::terminalTitle() const
{
    return m_engine ? m_engine->title() : QString();
}

QString TerminalView::shellTitle() const
{
    const QString shell = m_shellOverride.isEmpty() ? Settings::instance().shell() : m_shellOverride;
    return QFileInfo(shell).fileName();
}

QString TerminalView::screenText() const
{
    if (!m_engine)
        return QString();
    const int firstLine = visibleFirstLine();
    QStringList lines;
    for (int row = 0; row < m_rows; ++row)
        lines.append(m_engine->lineText(firstLine + row));
    return lines.join(QLatin1Char('\n'));
}

QString TerminalView::bufferText() const
{
    if (!m_engine)
        return QString();
    QStringList lines;
    for (int line = 0; line < m_engine->totalLines(); ++line)
        lines.append(m_engine->lineText(line));
    return lines.join(QLatin1Char('\n'));
}

int TerminalView::scrollbackCount() const
{
    return m_engine ? m_engine->scrollbackCount() : 0;
}

QPair<QRgb, QRgb> TerminalView::colorsAt(int row, int column) const
{
    if (!m_engine)
        return {0, 0};
    const FlatCell cell = m_engine->cellAt(visibleFirstLine() + row, column);
    return {m_engine->resolveColor(cell.fg, true), m_engine->resolveColor(cell.bg, false)};
}

bool TerminalView::isAlternateScreen() const
{
    return m_engine && m_engine->isAlternateScreen();
}

void TerminalView::onPtyData(const QByteArray &data)
{
    m_engine->feed(data);
    emit outputReceived();
}

void TerminalView::onPtyFinished(int exitCode, int exitSignal)
{
    // Показываем итог завершения, чтобы окно не закрывалось «молча»
    QString message;
    if (exitSignal != 0)
        message = tr("\n[Процесс завершён сигналом %1]\n").arg(exitSignal);
    else
        message = tr("\n[Процесс завершён, код возврата %1]\n").arg(exitCode);
    m_engine->feed(message.toUtf8());

    emit sessionFinished(exitCode, exitSignal);
    updateScrollBar();
    update();
}

// ---------------------------------------------------------------------------
// Шрифт и сетка
// ---------------------------------------------------------------------------

void TerminalView::rebuildFont()
{
    const Settings &settings = Settings::instance();
    m_font = QFont(settings.fontFamily());
    m_font.setPointSize(settings.fontSize());
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    m_font.setKerning(false);

    const QFontMetricsF metrics(m_font);
    qreal widest = metrics.horizontalAdvance(QLatin1Char('M'));
    widest = qMax(widest, metrics.horizontalAdvance(QChar(0x0416))); // Ж
    widest = qMax(widest, metrics.horizontalAdvance(QLatin1Char('0')));
    // Округление до целых пикселей: при HiDPI (1.25x, 1.5x) сетка остаётся ровной
    m_cellWidth = std::ceil(widest);
    if (m_cellWidth < 1.0)
        m_cellWidth = 8.0;
    m_cellHeight = std::ceil(metrics.height() * m_lineSpacing);
    if (m_cellHeight < 1.0)
        m_cellHeight = 16.0;
    // Базовая линия по центру строки — при увеличенном интервале текст не «прилипает» кверху
    m_ascent = static_cast<int>(std::ceil((m_cellHeight - metrics.height()) / 2.0 + metrics.ascent()));
}

int TerminalView::terminalWidth() const
{
    const int scrollBar = (m_scrollBar && m_scrollBar->isVisible()) ? scrollBarWidth() : 0;
    return width() - scrollBar;
}

int TerminalView::terminalHeight() const
{
    return height();
}

int TerminalView::scrollBarWidth() const
{
    return m_scrollBar ? qMax(10, m_scrollBar->sizeHint().width()) : 0;
}

void TerminalView::updateGridFromSize()
{
    if (!m_engine || m_updatingLayout)
        return;

    const int availableWidth = terminalWidth() - 2 * leftMargin();
    const int availableHeight = terminalHeight() - 2 * topMargin();
    const int cols = qMax(1, static_cast<int>(availableWidth / m_cellWidth));
    const int rows = qMax(1, static_cast<int>(availableHeight / m_cellHeight));

    if (cols == m_cols && rows == m_rows && m_engine->rows() == rows && m_engine->cols() == cols)
        return;

    m_cols = cols;
    m_rows = rows;
    m_engine->resize(rows, cols);
    if (m_pty)
        m_pty->setWindowSize(rows, cols);

    m_scrollOffset = qBound(0, m_scrollOffset, m_engine->scrollbackCount());
    updateScrollBar();
    update();
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    repositionScrollBar();
    updateGridFromSize();
    update();
}

void TerminalView::repositionScrollBar()
{
    if (!m_scrollBar || !m_engine)
        return;

    const bool needScrollBar = m_engine->scrollbackCount() > 0;
    if (m_scrollBar->isVisible() != needScrollBar) {
        m_scrollBar->setVisible(needScrollBar);
        // Ширина полосы влияет на число колонок — пересчитываем сетку
        m_updatingLayout = true;
        const int barWidth = needScrollBar ? scrollBarWidth() : 0;
        m_scrollBar->setGeometry(width() - barWidth, 0, barWidth, height());
        updateGridFromSize();
        m_updatingLayout = false;
        m_scrollBar->raise();
        return;
    }

    if (needScrollBar) {
        m_scrollBar->setGeometry(width() - scrollBarWidth(), 0, scrollBarWidth(), height());
        m_scrollBar->raise();
    }
}

void TerminalView::setFontSize(int pointSize)
{
    pointSize = qBound(Settings::kMinFontSize, pointSize, Settings::kMaxFontSize);
    Settings::instance().setFontSize(pointSize);
    rebuildFont();
    updateGridFromSize();
    emit fontSizeChanged(pointSize);
    update();
}

int TerminalView::fontSize() const
{
    return m_font.pointSize();
}

void TerminalView::setFontFamily(const QString &family)
{
    if (family.isEmpty() || family == m_font.family())
        return;
    Settings::instance().setFontFamily(family);
    rebuildFont();
    updateGridFromSize();
    update();
}

QString TerminalView::fontFamily() const
{
    return m_font.family();
}

void TerminalView::setLineSpacing(qreal spacing)
{
    spacing = qBound(Settings::kMinLineSpacing, spacing, Settings::kMaxLineSpacing);
    if (qFuzzyCompare(spacing, m_lineSpacing))
        return;
    m_lineSpacing = spacing;
    Settings::instance().setLineSpacing(spacing);
    rebuildFont();
    updateGridFromSize();
    update();
}

void TerminalView::setPadding(int pixels)
{
    pixels = qBound(Settings::kMinPadding, pixels, Settings::kMaxPadding);
    if (pixels == m_padding)
        return;
    m_padding = pixels;
    Settings::instance().setPadding(pixels);
    updateGridFromSize();
    update();
}

void TerminalView::setCursorStyle(CursorStyle style)
{
    m_cursorStyle = style;
    Settings::instance().setCursorStyle(style);
    update();
}

void TerminalView::zoomIn()
{
    setFontSize(fontSize() + 1);
}

void TerminalView::zoomOut()
{
    setFontSize(fontSize() - 1);
}

void TerminalView::zoomReset()
{
    setFontSize(11);
}

void TerminalView::setTheme(const ColorTheme &theme)
{
    m_theme = theme;
    if (m_engine)
        m_engine->setTheme(theme);
    update();
}

void TerminalView::applyTheme(const Theme &theme)
{
    setTheme(theme.terminal);
}

// ---------------------------------------------------------------------------
// Отрисовка
// ---------------------------------------------------------------------------

int TerminalView::visibleFirstLine() const
{
    if (!m_engine)
        return 0;
    return qMax(0, m_engine->scrollbackCount() - m_scrollOffset);
}

void TerminalView::scheduleRepaint()
{
    if (m_repaintScheduled)
        return;
    m_repaintScheduled = true;
    QTimer::singleShot(0, this, &TerminalView::applyDamage);
}

void TerminalView::applyDamage()
{
    m_repaintScheduled = false;
    if (!m_engine)
        return;

    const QVector<int> rows = m_engine->takeDirtyRows();
    if (rows.isEmpty()) {
        update();
        return;
    }

    const int firstLine = visibleFirstLine();
    QRegion region;
    for (const int row : rows) {
        const int line = firstLine + row;
        const int screenRow = line - firstLine;
        if (screenRow < 0 || screenRow >= m_rows)
            continue;
        region += QRect(leftMargin(),
                        topMargin() + static_cast<int>(screenRow * m_cellHeight),
                        static_cast<int>(m_cols * m_cellWidth) + 1,
                        static_cast<int>(m_cellHeight) + 1);
    }
    if (region.isEmpty())
        update(QRect(leftMargin(), topMargin(),
                     static_cast<int>(m_cols * m_cellWidth) + 1,
                     static_cast<int>(m_rows * m_cellHeight) + 1));
    else
        update(region);
}

QVector<TerminalView::Run> TerminalView::buildRuns(int line) const
{
    QVector<Run> runs;
    Run current;
    bool hasCurrent = false;

    for (int column = 0; column < m_cols; ++column) {
        FlatCell cell = m_engine->cellAt(line, column);
        if (cell.width == 0)
            continue; // продолжение широкого символа

        QRgb fg = m_engine->resolveColor(cell.fg, true);
        QRgb bg = m_engine->resolveColor(cell.bg, false);
        quint8 flags = cell.flags & (CellBold | CellItalic | CellUnderline | CellStrike);
        if (cell.flags & CellConceal)
            fg = bg;

        const int cellColumns = cell.width > 1 ? cell.width : 1;

        if (!hasCurrent || current.fg != fg || current.bg != bg || current.flags != flags) {
            if (hasCurrent && !current.text.isEmpty())
                runs.append(current);
            current = Run();
            current.startColumn = column;
            current.fg = fg;
            current.bg = bg;
            current.flags = flags;
            hasCurrent = true;
        }

        current.text.append(QChar::fromUcs4(static_cast<uint>(cell.ch)));
        current.columns += cellColumns;
        column += cellColumns - 1;
    }

    if (hasCurrent && !current.text.isEmpty())
        runs.append(current);
    return runs;
}

void TerminalView::drawRow(QPainter &painter, int line, int top, int selectionFrom, int selectionTo,
                           bool backgroundPass)
{
    const QVector<Run> runs = buildRuns(line);
    const int rowHeight = static_cast<int>(m_cellHeight);
    const QColor highlight = visibleSelectionColor(m_theme.selection, m_theme.background);

    if (!backgroundPass) {
        // Позиции глифов вычисляются по ячейкам, а не по накопленной ширине
        // строки QPainter::drawText(). Иначе даже доли пикселя на символ
        // постепенно уводят текст от курсора и сетки терминала.
        const QColor selectedForeground = selectionTextColor(highlight);
        for (const Run &run : runs) {
            if (run.text.trimmed().isEmpty())
                continue;

            QFont font = m_font;
            if (run.flags & CellBold)
                font.setBold(true);
            if (run.flags & CellItalic)
                font.setItalic(true);
            if (run.flags & CellUnderline)
                font.setUnderline(true);
            if (run.flags & CellStrike)
                font.setStrikeOut(true);
            painter.setFont(font);

            const QColor regularForeground = QColor::fromRgb(run.fg);
            const int endColumn = qMin(m_cols, run.startColumn + run.columns);
            for (int column = run.startColumn; column < endColumn; ++column) {
                const FlatCell cell = m_engine->cellAt(line, column);
                if (cell.width == 0)
                    continue; // вторая ячейка широкого символа

                const int occupiedColumns = qMax(1, static_cast<int>(cell.width));
                // Скрытые символы нельзя делать видимыми даже при выделении.
                if (cell.ch != 0 && cell.ch != U' ' && !(cell.flags & CellConceal)) {
                    const bool selected = selectionFrom >= 0
                        && column <= selectionTo
                        && column + occupiedColumns - 1 >= selectionFrom;
                    painter.setPen(selected ? selectedForeground : regularForeground);
                    const qreal x = leftMargin() + column * m_cellWidth;
                    painter.drawText(QPointF(x, top + m_ascent),
                                     QString::fromUcs4(&cell.ch, 1));
                }
                column += occupiedColumns - 1;
            }
        }
        return;
    }

    // Фон ANSI-цветов остаётся поверх фона темы.
    for (const Run &run : runs) {
        if ((run.bg & ColorKindMask) == ColorKindDefaultBg)
            continue;
        const qreal x = leftMargin() + run.startColumn * m_cellWidth;
        painter.fillRect(QRectF(x, top, run.columns * m_cellWidth, rowHeight),
                         QColor::fromRgb(run.bg));
    }

    // Выделение рисуется после фона ячеек, но перед текстом.
    if (selectionFrom >= 0 && selectionTo >= selectionFrom) {
        const qreal x = leftMargin() + selectionFrom * m_cellWidth;
        const qreal w = (selectionTo - selectionFrom + 1) * m_cellWidth;
        painter.fillRect(QRectF(x, top, w, rowHeight), highlight);
    }
}

void TerminalView::drawCursor(QPainter &painter)
{
    if (!m_engine || !m_engine->cursorVisible())
        return;

    const int line = m_engine->cursorLine();
    const int row = line - visibleFirstLine();
    if (row < 0 || row >= m_rows)
        return;

    const int column = m_engine->cursorColumn();
    const qreal x = leftMargin() + column * m_cellWidth;
    const qreal top = topMargin() + row * m_cellHeight;
    QRectF cursorRect(x, top, m_cellWidth, m_cellHeight);

    if (!hasFocus() || (m_cursorBlink && !m_cursorBlinkOn)) {
        if (!hasFocus()) {
            painter.setPen(QPen(m_theme.cursor, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(cursorRect.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        return;
    }

    int shape = m_engine->cursorShape();
    switch (m_cursorStyle) {
    case CursorStyle::Block:
        shape = VTERM_PROP_CURSORSHAPE_BLOCK;
        break;
    case CursorStyle::Underline:
        shape = VTERM_PROP_CURSORSHAPE_UNDERLINE;
        break;
    case CursorStyle::Bar:
        shape = VTERM_PROP_CURSORSHAPE_BAR_LEFT;
        break;
    case CursorStyle::FollowApplication:
        break;
    }
    bool filled = true;
    if (shape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
        cursorRect = QRectF(x, top + m_cellHeight - qMax(2.0, m_cellHeight * 0.12), m_cellWidth,
                            qMax(2.0, m_cellHeight * 0.12));
        filled = false;
    } else if (shape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
        cursorRect = QRectF(x, top, qMax(2.0, m_cellWidth * 0.12), m_cellHeight);
        filled = false;
    }

    painter.fillRect(cursorRect, m_theme.cursor);

    if (filled) {
        const FlatCell cell = m_engine->cellAt(line, column);
        if (cell.width > 0 && cell.ch != U' ') {
            QFont font = m_font;
            if (cell.flags & CellBold)
                font.setBold(true);
            painter.setFont(font);
            painter.setPen(m_theme.cursorText);
            painter.drawText(QPointF(x, top + m_ascent),
                             QString::fromUcs4(&cell.ch, 1));
        }
    }
}

void TerminalView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_theme.background);

    if (!m_engine)
        return;

    const int firstLine = visibleFirstLine();
    const QRect clip = event->rect();

    for (int row = 0; row < m_rows; ++row) {
        const int line = firstLine + row;
        if (line < 0 || line >= m_engine->totalLines())
            continue;

        const int top = topMargin() + static_cast<int>(row * m_cellHeight);
        const QRect rowRect(leftMargin(), top, static_cast<int>(m_cols * m_cellWidth) + 1,
                            static_cast<int>(m_cellHeight) + 1);
        if (!clip.intersects(rowRect))
            continue;

        int from = -1;
        int to = -1;
        selectionRange(line, &from, &to);
        drawRow(painter, line, top, from, to, true);
    }

    // Второй проход — текст поверх готового фона
    for (int row = 0; row < m_rows; ++row) {
        const int line = firstLine + row;
        if (line < 0 || line >= m_engine->totalLines())
            continue;
        const int top = topMargin() + static_cast<int>(row * m_cellHeight);
        const QRect rowRect(leftMargin(), top, static_cast<int>(m_cols * m_cellWidth) + 1,
                            static_cast<int>(m_cellHeight) + 1);
        if (!clip.intersects(rowRect))
            continue;
        int from = -1;
        int to = -1;
        selectionRange(line, &from, &to);
        drawRow(painter, line, top, from, to, false);
    }

    drawCursor(painter);

    // Предварительный ввод метода ввода (IME)
    if (!m_preedit.isEmpty() && hasFocus()) {
        const int row = m_engine->cursorLine() - firstLine;
        if (row >= 0 && row < m_rows) {
            QFont font = m_font;
            font.setUnderline(true);
            painter.setFont(font);
            painter.setPen(m_theme.foreground);
            painter.drawText(QPointF(leftMargin() + m_engine->cursorColumn() * m_cellWidth,
                                     topMargin() + row * m_cellHeight + m_ascent),
                             m_preedit);
        }
    }
}

// ---------------------------------------------------------------------------
// Прокрутка
// ---------------------------------------------------------------------------

void TerminalView::updateScrollBar()
{
    if (!m_engine || !m_scrollBar)
        return;

    const int scrollback = m_engine->scrollbackCount();
    repositionScrollBar();

    m_scrollBar->blockSignals(true);
    m_scrollBar->setRange(0, scrollback);
    m_scrollBar->setPageStep(qMax(1, m_rows - 1));
    m_scrollBar->setSingleStep(1);
    m_scrollBar->setValue(scrollback - m_scrollOffset);
    m_scrollBar->blockSignals(false);
}

void TerminalView::onScrollBarValueChanged(int value)
{
    if (!m_engine)
        return;
    m_scrollOffset = m_engine->scrollbackCount() - value;
    m_scrollOffset = qBound(0, m_scrollOffset, m_engine->scrollbackCount());
    update();
}

void TerminalView::scrollToOffset(int offset)
{
    if (!m_engine)
        return;
    const int bounded = qBound(0, offset, m_engine->scrollbackCount());
    if (bounded == m_scrollOffset)
        return;
    m_scrollOffset = bounded;
    updateScrollBar();
    update();
}

void TerminalView::scrollToLine(int line)
{
    if (!m_engine)
        return;
    // Строка 0 — самая старая в буфере: сверху она при максимальном смещении
    const int offset = m_engine->scrollbackCount() - line;
    scrollToOffset(qBound(0, offset, m_engine->scrollbackCount()));
}

void TerminalView::scrollToBottom()
{
    scrollToOffset(0);
}

void TerminalView::scrollPageUp()
{
    scrollToOffset(m_scrollOffset + qMax(1, m_rows - 1));
}

void TerminalView::scrollPageDown()
{
    scrollToOffset(m_scrollOffset - qMax(1, m_rows - 1));
}

void TerminalView::scrollLines(int lines)
{
    scrollToOffset(m_scrollOffset + lines);
}

// ---------------------------------------------------------------------------
// Выделение и буфер обмена
// ---------------------------------------------------------------------------

TerminalView::SelectionPoint TerminalView::pointAt(const QPoint &position) const
{
    SelectionPoint point;
    const int row = static_cast<int>((position.y() - topMargin()) / m_cellHeight);
    const int column = static_cast<int>((position.x() - leftMargin()) / m_cellWidth);
    const int boundedRow = qBound(0, row, m_rows - 1);
    point.column = qBound(0, column, m_cols - 1);
    point.line = qBound(0, visibleFirstLine() + boundedRow,
                        qMax(0, m_engine ? m_engine->totalLines() - 1 : 0));
    return point;
}

void TerminalView::selectionRange(int line, int *from, int *to) const
{
    *from = -1;
    *to = -1;
    if (!m_hasSelection)
        return;

    SelectionPoint start = m_selectionAnchor;
    SelectionPoint end = m_selectionCursor;
    if (start.line > end.line || (start.line == end.line && start.column > end.column))
        std::swap(start, end);

    if (line < start.line || line > end.line)
        return;

    *from = (line == start.line) ? start.column : 0;
    *to = (line == end.line) ? end.column : m_cols - 1;
    if (*to < *from)
        return;
}

void TerminalView::setSelection(SelectionPoint anchor, SelectionPoint cursor)
{
    m_selectionAnchor = anchor;
    m_selectionCursor = cursor;
    m_hasSelection = (anchor.line != cursor.line || anchor.column != cursor.column);
    emit copyAvailable(m_hasSelection);
    if (m_hasSelection)
        emit selectionTextChanged(selectionText());
    update();
}

void TerminalView::clearSelection()
{
    if (!m_hasSelection && !m_selecting)
        return;
    m_hasSelection = false;
    m_selecting = false;
    emit copyAvailable(false);
    update();
}

void TerminalView::selectAll()
{
    if (!m_engine)
        return;
    SelectionPoint start{0, 0};
    SelectionPoint end{qMax(0, m_engine->totalLines() - 1), m_cols - 1};
    setSelection(start, end);
}

QString TerminalView::selectionText() const
{
    if (!m_hasSelection || !m_engine)
        return QString();

    SelectionPoint start = m_selectionAnchor;
    SelectionPoint end = m_selectionCursor;
    if (start.line > end.line || (start.line == end.line && start.column > end.column))
        std::swap(start, end);

    QString result;
    bool hasPreviousLine = false;
    for (int line = start.line; line <= end.line; ++line) {
        if (line < 0 || line >= m_engine->totalLines())
            continue;
        int from = -1;
        int to = -1;
        selectionRange(line, &from, &to);
        if (from < 0)
            continue;

        QString text = m_engine->lineText(line, false);
        if (to + 1 < text.size())
            text.truncate(to + 1);
        if (from > 0 && from < text.size())
            text.remove(0, from);
        else if (from > 0)
            text.clear();
        while (!text.isEmpty() && text.at(text.size() - 1) == QLatin1Char(' '))
            text.chop(1);

        // Мягкий перенос — продолжение той же логической строки.
        // История прокрутки пока не содержит метаданных, поэтому для неё
        // сохраняется прежнее поведение (обычный перевод строки).
        if (hasPreviousLine && !m_engine->lineContinuesPrevious(line))
            result.append(QLatin1Char('\n'));
        result.append(text);
        hasPreviousLine = true;
    }
    return result;
}

void TerminalView::selectWordAt(SelectionPoint point)
{
    if (!m_engine)
        return;
    const QString text = m_engine->lineText(point.line, false);
    int start = qBound(0, point.column, qMax(0, text.size() - 1));
    auto isWordChar = [](QChar ch) {
        return !ch.isSpace() && ch != QLatin1Char('(') && ch != QLatin1Char(')')
            && ch != QLatin1Char('[') && ch != QLatin1Char(']') && ch != QLatin1Char('{')
            && ch != QLatin1Char('}') && ch != QLatin1Char('"') && ch != QLatin1Char('\'');
    };

    if (start < text.size() && !isWordChar(text.at(start))) {
        setSelection({point.line, start}, {point.line, start});
        return;
    }

    int from = start;
    while (from > 0 && isWordChar(text.at(from - 1)))
        --from;
    int to = start;
    while (to + 1 < text.size() && isWordChar(text.at(to + 1)))
        ++to;

    setSelection({point.line, from}, {point.line, qMin(to, m_cols - 1)});
}

void TerminalView::selectLineAt(SelectionPoint point)
{
    if (!m_engine)
        return;
    setSelection({point.line, 0}, {point.line, m_cols - 1});
}

void TerminalView::copyToClipboard(const QString &text)
{
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text, QClipboard::Clipboard);
}

void TerminalView::copySelection()
{
    const QString text = selectionText();
    if (text.isEmpty())
        return;
    copyToClipboard(text);
}

void TerminalView::pasteClipboard()
{
    const QString text = QApplication::clipboard()->text(QClipboard::Clipboard);
    pasteText(text);
}

void TerminalView::pasteText(const QString &text)
{
    if (text.isEmpty() || !m_engine)
        return;

    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    if (normalized.contains(QLatin1Char('\n')) && Settings::instance().confirmMultilinePaste()) {
        const auto answer = QMessageBox::question(
            this, tr("Вставить несколько строк?"),
            tr("Буфер обмена содержит %1 строк. Вставить целиком?")
                .arg(normalized.count(QLatin1Char('\n')) + 1),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return;
    }

    normalized.replace(QLatin1Char('\n'), QLatin1Char('\r'));

    m_engine->setPasteMode(true);
    for (const QChar ch : normalized)
        m_engine->sendUnicode(ch.unicode(), VTERM_MOD_NONE);
    m_engine->setPasteMode(false);

    scrollToBottom();
    update();
}

void TerminalView::sendTextToShell(const QString &text)
{
    if (!m_engine)
        return;
    m_engine->sendText(text.toUtf8());
    scrollToBottom();
}

// ---------------------------------------------------------------------------
// Клавиатура
// ---------------------------------------------------------------------------

void TerminalView::sendKeyBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;
    if (m_scrollOnKeystroke)
        scrollToBottom();
    m_engine->sendText(bytes);
}

namespace {
QChar latinForControlKey(int key, const QString &text)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QChar('A' + (key - Qt::Key_A));
    if (!text.isEmpty()) {
        const QChar ch = text.at(0);
        const QChar latin = cyrillicToLatin(ch);
        if (!latin.isNull())
            return upperLatin(latin);
        if (ch.isLetter() || ch.isPunct() || ch == QLatin1Char(' '))
            return upperLatin(ch);
    }
    return QChar();
}

char controlCodeForLatin(QChar latin)
{
    if (latin.isNull())
        return 0;
    const ushort code = latin.unicode();
    if (code >= 'A' && code <= 'Z')
        return static_cast<char>(code - 'A' + 1);
    switch (code) {
    case ' ':
        return 0;
    case '[':
        return 27;
    case '\\':
        return 28;
    case ']':
        return 29;
    case '^':
        return 30;
    case '_':
        return 31;
    case '?':
        return 127;
    default:
        break;
    }
    return 0;
}
} // namespace

bool TerminalView::handleSpecialKey(QKeyEvent *event)
{
    if (!m_engine)
        return false;

    const int key = event->key();
    const Qt::KeyboardModifiers qmods = event->modifiers();
    const bool ctrl = qmods.testFlag(Qt::ControlModifier);
    const bool shift = qmods.testFlag(Qt::ShiftModifier);
    const bool alt = qmods.testFlag(Qt::AltModifier);

    int vmods = 0;
    if (shift)
        vmods |= VTERM_MOD_SHIFT;
    if (alt)
        vmods |= VTERM_MOD_ALT;
    if (ctrl)
        vmods |= VTERM_MOD_CTRL;

    // --- собственные сочетания (работают в любой раскладке) ---
    if (ctrl && shift) {
        const QChar latin = latinForControlKey(key, event->text());
        if (latin == QLatin1Char('C')) {
            copySelection();
            return true;
        }
        if (latin == QLatin1Char('V')) {
            pasteClipboard();
            return true;
        }
    }
    if (ctrl && (key == Qt::Key_Plus || key == Qt::Key_Equal)) {
        zoomIn();
        return true;
    }
    if (ctrl && key == Qt::Key_Minus) {
        zoomOut();
        return true;
    }
    if (ctrl && key == Qt::Key_0) {
        zoomReset();
        return true;
    }

    // --- прокрутка истории ---
    if (shift && !ctrl && !alt && key == Qt::Key_PageUp) {
        scrollPageUp();
        return true;
    }
    if (shift && !ctrl && !alt && key == Qt::Key_PageDown) {
        scrollPageDown();
        return true;
    }

    // --- служебные клавиши ---
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (alt)
            sendKeyBytes(QByteArray("\x1b\r", 2));
        else
            sendKeyBytes(QByteArray(1, '\r'));
        return true;
    case Qt::Key_Backspace:
        sendKeyBytes(QByteArray(1, ctrl ? '\x08' : '\x7f'));
        return true;
    case Qt::Key_Tab:
        if (shift && !ctrl)
            sendKeyBytes(QByteArray("\x1b[Z", 3));
        else
            sendKeyBytes(QByteArray(1, '\t'));
        return true;
    case Qt::Key_Escape:
        sendKeyBytes(QByteArray(1, '\x1b'));
        return true;
    case Qt::Key_Up:
        m_engine->sendKey(VTERM_KEY_UP, vmods);
        return true;
    case Qt::Key_Down:
        m_engine->sendKey(VTERM_KEY_DOWN, vmods);
        return true;
    case Qt::Key_Left:
        m_engine->sendKey(VTERM_KEY_LEFT, vmods);
        return true;
    case Qt::Key_Right:
        m_engine->sendKey(VTERM_KEY_RIGHT, vmods);
        return true;
    case Qt::Key_Home:
        m_engine->sendKey(VTERM_KEY_HOME, vmods);
        return true;
    case Qt::Key_End:
        m_engine->sendKey(VTERM_KEY_END, vmods);
        return true;
    case Qt::Key_PageUp:
        m_engine->sendKey(VTERM_KEY_PAGEUP, vmods);
        return true;
    case Qt::Key_PageDown:
        m_engine->sendKey(VTERM_KEY_PAGEDOWN, vmods);
        return true;
    case Qt::Key_Insert:
        m_engine->sendKey(VTERM_KEY_INS, vmods);
        return true;
    case Qt::Key_Delete:
        m_engine->sendKey(VTERM_KEY_DEL, vmods);
        return true;
    default:
        break;
    }

    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        m_engine->sendKey(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1), vmods);
        return true;
    }

    // --- Ctrl + буква/знак → управляющий код (в т.ч. Ctrl+C, Ctrl+D, Ctrl+Z) ---
    if (ctrl && !alt) {
        const QChar latin = latinForControlKey(key, event->text());
        const char code = controlCodeForLatin(latin);
        if (code != 0 || latin == QLatin1Char(' ') || latin == QLatin1Char('@')) {
            scrollToBottom();
            m_engine->sendText(QByteArray(1, code));
            return true;
        }
        return false;
    }

    // --- обычный текст (кириллица, символы, Alt+символ) ---
    if (!ctrl) {
        const QString text = event->text();
        if (!text.isEmpty()) {
            scrollToBottom();
            for (const QChar ch : text) {
                if (ch.unicode() == 0)
                    continue;
                m_engine->sendUnicode(ch.unicode(), alt ? VTERM_MOD_ALT : VTERM_MOD_NONE);
            }
            return true;
        }
    }

    return false;
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    if (handleSpecialKey(event)) {
        event->accept();
        return;
    }
    // Прочие клавиши не должны «просвечивать» наружу (Tab, Enter уже обработаны)
    event->ignore();
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_engine) {
        QWidget::inputMethodEvent(event);
        return;
    }

    const QString commit = event->commitString();
    if (!commit.isEmpty()) {
        m_preedit.clear();
        for (const QChar ch : commit)
            m_engine->sendUnicode(ch.unicode(), VTERM_MOD_NONE);
    }
    m_preedit = event->preeditString();
    update();
    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle && m_engine) {
        const int row = m_engine->cursorLine() - visibleFirstLine();
        return QRect(leftMargin() + static_cast<int>(m_engine->cursorColumn() * m_cellWidth),
                     topMargin() + static_cast<int>(row * m_cellHeight),
                     static_cast<int>(m_cellWidth), static_cast<int>(m_cellHeight));
    }
    if (query == Qt::ImEnabled)
        return true;
    return QWidget::inputMethodQuery(query);
}

// ---------------------------------------------------------------------------
// Мышь
// ---------------------------------------------------------------------------

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    setFocus();

    const QPoint position = event->position().toPoint();
    const int vmods = event->modifiers().testFlag(Qt::ShiftModifier) ? VTERM_MOD_SHIFT : VTERM_MOD_NONE;

    if (event->button() == Qt::LeftButton) {
        if (m_engine->mouseTrackingMode() != 0
            && !event->modifiers().testFlag(Qt::ShiftModifier)) {
            m_engine->sendMouseButton(1, true, vmods);
            return;
        }

        const SelectionPoint point = pointAt(position);
        if (event->modifiers().testFlag(Qt::ShiftModifier) && m_hasSelection) {
            setSelection(m_selectionAnchor, point);
            return;
        }

        clearSelection();
        m_selecting = true;
        m_selectionGranularity = 0;
        setSelection(point, point);
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        if (m_engine->mouseTrackingMode() != 0) {
            m_engine->sendMouseButton(2, true, vmods);
            return;
        }
        const QString text = QApplication::clipboard()->text(QClipboard::Selection);
        pasteText(text.isEmpty() ? QApplication::clipboard()->text(QClipboard::Clipboard) : text);
        return;
    }

    QWidget::mousePressEvent(event);
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_engine) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const SelectionPoint point = pointAt(event->position().toPoint());
    m_selecting = false;
    if (m_selectionGranularity == 1) {
        m_selectionGranularity = 2;
        selectLineAt(point);
    } else {
        m_selectionGranularity = 1;
        selectWordAt(point);
    }
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint position = event->position().toPoint();

    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        if (position.y() < topMargin())
            scrollLines(1);
        else if (position.y() > height() - topMargin())
            scrollLines(-1);

        const SelectionPoint point = pointAt(position);
        setSelection(m_selectionAnchor, point);
        return;
    }

    if (m_engine && m_engine->mouseTrackingMode() >= VTERM_PROP_MOUSE_MOVE) {
        const SelectionPoint point = pointAt(position);
        int col = point.column;
        int row = point.line - m_engine->scrollbackCount();
        if (row >= 0)
            m_engine->sendMouseMove(row, col, VTERM_MOD_NONE);
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_engine && m_engine->mouseTrackingMode() != 0 && !m_selecting) {
            m_engine->sendMouseButton(1, false, VTERM_MOD_NONE);
            return;
        }
        if (m_selecting) {
            m_selecting = false;
            const QString text = selectionText();
            if (!text.isEmpty()) {
                // Копия в PRIMARY — как в обычных терминалах
                QApplication::clipboard()->setText(text, QClipboard::Selection);
            }
            update();
        }
        return;
    }

    if (event->button() == Qt::MiddleButton && m_engine && m_engine->mouseTrackingMode() != 0) {
        m_engine->sendMouseButton(2, false, VTERM_MOD_NONE);
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void TerminalView::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (delta > 0)
            zoomIn();
        else
            zoomOut();
        return;
    }

    if (m_engine && m_engine->mouseTrackingMode() != 0
        && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int button = delta > 0 ? 4 : 5;
        m_engine->sendMouseButton(button, true, VTERM_MOD_NONE);
        m_engine->sendMouseButton(button, false, VTERM_MOD_NONE);
        return;
    }

    scrollLines(delta > 0 ? m_wheelScrollLines : -m_wheelScrollLines);
}

void TerminalView::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    emit focusChanged(true);
    m_cursorBlinkOn = true;
    if (!m_blinkTimer->isActive())
        m_blinkTimer->start();
    update();
}

void TerminalView::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    emit focusChanged(false);
    m_blinkTimer->stop();
    m_cursorBlinkOn = true;
    update();
}

void TerminalView::onCursorBlinkTick()
{
    if (!m_engine || !m_engine->cursorBlink() || !m_cursorBlink)
        return;
    m_cursorBlinkOn = !m_cursorBlinkOn;
    const int row = m_engine->cursorLine() - visibleFirstLine();
    if (row >= 0 && row < m_rows) {
        update(QRect(leftMargin() + static_cast<int>(m_engine->cursorColumn() * m_cellWidth),
                     topMargin() + static_cast<int>(row * m_cellHeight),
                     static_cast<int>(m_cellWidth), static_cast<int>(m_cellHeight)));
    }
}

void TerminalView::onCursorChanged()
{
    if (!m_blinkTimer->isActive() && hasFocus())
        m_blinkTimer->start();
    m_cursorBlinkOn = true;
}

void TerminalView::onEngineDamage()
{
    // Новый вывод. Если автопрокрутка выключена и пользователь читает историю,
    // удерживаем ту же строку на экране — текст не «уезжает» под курсором.
    if (m_engine) {
        const int total = m_engine->scrollbackCount();
        if (!m_scrollOnOutput && m_scrollOffset > 0 && total > m_lastScrollbackCount) {
            m_scrollOffset = qBound(0, m_scrollOffset + (total - m_lastScrollbackCount), total);
            updateScrollBar();
        }
        m_lastScrollbackCount = total;
    }
    scheduleRepaint();
}

void TerminalView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *copyAction = menu.addAction(tr("Копировать"));
    copyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    copyAction->setEnabled(m_hasSelection);
    connect(copyAction, &QAction::triggered, this, &TerminalView::copySelection);

    QAction *pasteAction = menu.addAction(tr("Вставить"));
    pasteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(pasteAction, &QAction::triggered, this, &TerminalView::pasteClipboard);

    menu.addSeparator();

    QAction *selectAllAction = menu.addAction(tr("Выделить всё"));
    connect(selectAllAction, &QAction::triggered, this, &TerminalView::selectAll);

    QAction *clearAction = menu.addAction(tr("Очистить экран"));
    connect(clearAction, &QAction::triggered, this, &TerminalView::clearScreen);

    menu.addSeparator();

    QAction *zoomInAction = menu.addAction(tr("Увеличить шрифт"));
    connect(zoomInAction, &QAction::triggered, this, &TerminalView::zoomIn);

    QAction *zoomOutAction = menu.addAction(tr("Уменьшить шрифт"));
    connect(zoomOutAction, &QAction::triggered, this, &TerminalView::zoomOut);

    QAction *zoomResetAction = menu.addAction(tr("Сбросить масштаб"));
    connect(zoomResetAction, &QAction::triggered, this, &TerminalView::zoomReset);

    menu.exec(event->globalPos());
}

// ---------------------------------------------------------------------------
// Прочее
// ---------------------------------------------------------------------------

void TerminalView::clearScreen()
{
    if (!m_engine)
        return;

    // Когда терминальная сессия работает, отправляем Ctrl+L самой программе.
    // Bash/Readline перерисует приглашение и сохранит набранную команду.
    // Интерактивные приложения получают такое же событие, как от клавиатуры.
    if (m_pty && m_pty->isRunning()) {
        sendTextToShell(QStringLiteral("\f"));
        setFocus(Qt::OtherFocusReason);
        return;
    }

    // Если сессия завершена, очищаем только отображаемый буфер.
    m_engine->feed(QByteArrayLiteral("\x1b[H\x1b[2J"));
    update();
}

void TerminalView::clearScrollback()
{
    if (!m_engine)
        return;
    m_engine->feed(QByteArrayLiteral("\x1b[3J"));
    m_scrollOffset = 0;
    updateScrollBar();
    update();
}

} // namespace BlackCore
