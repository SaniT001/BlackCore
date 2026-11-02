// SPDX-License-Identifier: MIT
#include "SideBar.h"
#include "UiIcons.h"

#include "../core/AppInfo.h"
#include "../core/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

namespace BlackCore {
namespace {

QLabel *makeHeader(const QString &text)
{
    auto *label = new QLabel(text);
    label->setProperty("role", QStringLiteral("header"));
    return label;
}

struct InfoRow
{
    QLabel *value = nullptr;
    QWidget *row = nullptr;
};

InfoRow makeInfoRow(QWidget *parent, const QString &caption, QVBoxLayout *layout)
{
    InfoRow info;
    auto *wrapper = new QWidget(parent);
    auto *box = new QVBoxLayout(wrapper);
    box->setContentsMargins(10, 2, 10, 2);
    box->setSpacing(1);

    auto *captionLabel = new QLabel(caption, wrapper);
    captionLabel->setProperty("role", QStringLiteral("secondary"));
    QFont small = captionLabel->font();
    small.setPointSizeF(qMax(7.0, small.pointSizeF() - 1.0));
    captionLabel->setFont(small);

    auto *valueLabel = new QLabel(QStringLiteral("—"), wrapper);
    valueLabel->setWordWrap(false);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    box->addWidget(captionLabel);
    box->addWidget(valueLabel);
    layout->addWidget(wrapper);

    info.value = valueLabel;
    info.row = wrapper;
    return info;
}

QPushButton *makeAction(QWidget *parent, const QString &text)
{
    auto *button = new QPushButton(text, parent);
    button->setIconSize(QSize(UiIcons::kActionSize, UiIcons::kActionSize));
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(30);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

} // namespace

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sideBar"));
    setMinimumWidth(200);
    setMaximumWidth(280);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(4);

    // --- заголовок раздела ---
    auto *sessionsHeaderRow = new QHBoxLayout;
    sessionsHeaderRow->setContentsMargins(10, 0, 10, 0);
    m_sessionsCount = new QLabel(QStringLiteral("0"));
    m_sessionsCount->setProperty("role", QStringLiteral("secondary"));
    sessionsHeaderRow->addWidget(makeHeader(tr("СЕССИИ")));
    sessionsHeaderRow->addStretch(1);
    sessionsHeaderRow->addWidget(m_sessionsCount);
    layout->addLayout(sessionsHeaderRow);

    m_sessionList = new QListWidget(this);
    m_sessionList->setObjectName(QStringLiteral("sessionList"));
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sessionList->setMinimumHeight(96);
    m_sessionList->setMaximumHeight(220);
    layout->addWidget(m_sessionList);

    connect(m_sessionList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            emit sessionActivated(row);
    });

    // --- быстрые действия ---
    auto *actionsWrapper = new QWidget(this);
    auto *actionsBox = new QVBoxLayout(actionsWrapper);
    actionsBox->setContentsMargins(6, 4, 6, 4);
    actionsBox->setSpacing(4);

    actionsBox->addWidget(makeHeader(tr("ДЕЙСТВИЯ")));

    auto *newTabButton = makeAction(actionsWrapper, tr("Новая вкладка"));
    auto *copyButton = makeAction(actionsWrapper, tr("Копировать"));
    auto *pasteButton = makeAction(actionsWrapper, tr("Вставить"));
    auto *executeButton = makeAction(actionsWrapper, tr("Выполнить"));
    executeButton->setObjectName(QStringLiteral("executeCommandButton"));
    executeButton->setToolTip(tr("Отправить Enter в активный терминал"));
    auto *clearButton = makeAction(actionsWrapper, tr("Очистить экран"));
    auto *settingsButton = makeAction(actionsWrapper, tr("Настройки"));

    m_actionButtons = {
        {newTabButton, QStringLiteral("new-tab")},
        {copyButton, QStringLiteral("copy")},
        {pasteButton, QStringLiteral("paste")},
        {executeButton, QStringLiteral("execute")},
        {clearButton, QStringLiteral("clear")},
        {settingsButton, QStringLiteral("settings")}
    };
    refreshActionIcons();

    actionsBox->addWidget(newTabButton);
    actionsBox->addWidget(copyButton);
    actionsBox->addWidget(pasteButton);
    actionsBox->addWidget(executeButton);
    actionsBox->addWidget(clearButton);
    actionsBox->addWidget(settingsButton);
    actionsWrapper->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(actionsWrapper);

    connect(newTabButton, &QPushButton::clicked, this, &SideBar::newTabRequested);
    connect(copyButton, &QPushButton::clicked, this, &SideBar::copyRequested);
    connect(pasteButton, &QPushButton::clicked, this, &SideBar::pasteRequested);
    connect(executeButton, &QPushButton::clicked, this, &SideBar::executeRequested);
    connect(clearButton, &QPushButton::clicked, this, &SideBar::clearRequested);
    connect(settingsButton, &QPushButton::clicked, this, &SideBar::settingsRequested);

    // --- сведения о текущей сессии ---
    auto *separator = new QFrame(this);
    separator->setProperty("role", QStringLiteral("separator"));
    separator->setFrameShape(QFrame::HLine);
    separator->setFixedHeight(1);
    layout->addSpacing(6);
    layout->addWidget(separator);

    auto *infoHeader = makeHeader(tr("ТЕКУЩАЯ СЕССИЯ"));
    infoHeader->setContentsMargins(10, 0, 10, 0);
    layout->addWidget(infoHeader);

    const InfoRow shell = makeInfoRow(this, tr("Оболочка"), layout);
    m_shellValue = shell.value;
    const InfoRow directory = makeInfoRow(this, tr("Каталог"), layout);
    m_directoryValue = directory.value;
    const InfoRow size = makeInfoRow(this, tr("Размер"), layout);
    m_sizeValue = size.value;
    const InfoRow pid = makeInfoRow(this, tr("Процесс"), layout);
    m_pidValue = pid.value;

    layout->addStretch(1);

    m_footer = new QLabel(QStringLiteral("BlackCore %1  ·  F9").arg(versionString()), this);
    m_footer->setProperty("role", QStringLiteral("secondary"));
    m_footer->setContentsMargins(10, 6, 10, 2);
    QFont footerFont = m_footer->font();
    footerFont.setPointSizeF(qMax(7.0, footerFont.pointSizeF() - 1.0));
    m_footer->setFont(footerFont);
    layout->addWidget(m_footer);
}

void SideBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshActionIcons();
}

void SideBar::refreshActionIcons()
{
    const BrandPalette palette = ThemeManager::colors();
    for (const auto &entry : m_actionButtons) {
        if (entry.first) {
            entry.first->setIcon(UiIcons::create(entry.second, palette.textPrimary,
                                                 palette.goldLight, palette.textSecondary));
        }
    }
}

void SideBar::setSessions(const QStringList &titles, int currentIndex)
{
    const QSignalBlocker blocker(m_sessionList);
    m_sessionList->clear();
    for (int i = 0; i < titles.size(); ++i) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(i + 1).arg(titles.at(i)));
        item->setToolTip(titles.at(i));
        m_sessionList->addItem(item);
    }
    if (currentIndex >= 0 && currentIndex < m_sessionList->count())
        m_sessionList->setCurrentRow(currentIndex);
    updateCounters(titles.size());
}

void SideBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_sessionList->count())
        return;
    const QSignalBlocker blocker(m_sessionList);
    m_sessionList->setCurrentRow(index);
}

void SideBar::updateCounters(int sessions)
{
    m_sessionsCount->setText(QString::number(sessions));
}

void SideBar::setCounters(int sessions)
{
    updateCounters(sessions);
}

void SideBar::setSessionInfo(const QString &shell, const QString &directory, const QString &size,
                             int pid, bool running)
{
    m_shellValue->setText(shell);
    m_directoryValue->setText(directory.isEmpty() ? tr("—") : directory);
    m_directoryValue->setToolTip(directory);
    m_sizeValue->setText(size);
    if (running)
        m_pidValue->setText(QString::number(pid));
    else
        m_pidValue->setText(tr("не запущен"));
    m_pidValue->setProperty("role", running ? QStringLiteral("success") : QStringLiteral("error"));
    m_pidValue->style()->unpolish(m_pidValue);
    m_pidValue->style()->polish(m_pidValue);
}

} // namespace BlackCore
