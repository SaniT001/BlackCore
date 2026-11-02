// SPDX-License-Identifier: MIT
#pragma once

#include "../core/Notifications.h"
#include "../core/Settings.h"

#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace BlackCore {

//! Внешний вид: тема, шрифт, окно, панели, прокрутка (то, что видно на экране).
struct AppearanceSettings
{
    QString themeId;
    QString fontFamily;
    int fontSize = 11;
    qreal lineSpacing = 1.0;
    int padding = 6;
    CursorStyle cursorStyle = CursorStyle::FollowApplication;
    bool cursorBlink = true;
    int windowOpacity = 100;
    bool useSystemTitleBar = false;
    qreal uiScale = 1.0;

    // видимость панелей
    bool sidebarVisible = true;
    bool statusBarVisible = true;
    bool menuBarVisible = true;
    bool tabBarVisible = true;

    // прокрутка
    int scrollbackLines = 3000;
    int wheelScrollLines = 3;
    bool scrollOnOutput = true;
    bool scrollOnKeystroke = true;

    static AppearanceSettings fromSettings();
    void applyToSettings() const;
};

//! Поведение: оболочка, каталоги, вкладки, предупреждения, автосохранение.
struct BehaviorSettings
{
    QString shell;
    QString startDirectory;
    bool restoreSession = true;
    bool newTabInCurrentDirectory = true;
    bool warnOnCloseRunning = true;
    bool confirmCloseWindow = true;
    bool confirmMultilinePaste = true;
    bool bellAudible = true;
    bool autoSave = true;

    static BehaviorSettings fromSettings();
    void applyToSettings() const;
};

//! Диалог настроек BlackCore: оформление, поведение, клавиши, конфигурация.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    AppearanceSettings appearance() const;
    BehaviorSettings behavior() const;

    //! Совместимость с этапом 2
    AppearanceSettings values() const { return appearance(); }

    //! Каталог, из которого пользователь просит импортировать настройки
    QString importedConfigPath() const { return m_importedPath; }

signals:
    //! Предпросмотр внешнего вида (не сохраняя настройки)
    void previewChanged(const BlackCore::AppearanceSettings &values);
    //! Полный набор изменений после «Применить»/«Сохранить»
    void settingsApplied();

private slots:
    void emitPreview();
    void resetToDefaults();
    void exportConfig();
    void importConfig();

private:
    void buildUi();
    QWidget *buildAppearancePage();
    QWidget *buildPanelsPage();
    QWidget *buildBehaviorPage();
    QWidget *buildShortcutsPage();
    QWidget *buildConfigPage();
    QWidget *buildNotificationsPage();
    void loadValues();
    void loadShortcutTable();
    void apply();

    QTabWidget *m_tabs = nullptr;

    // оформление
    QComboBox *m_themeCombo = nullptr;
    QLabel *m_themeHint = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QCheckBox *m_systemTitleCheck = nullptr;
    QComboBox *m_scaleCombo = nullptr;

    // терминал
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    QSpinBox *m_lineSpacingSpin = nullptr;
    QSpinBox *m_paddingSpin = nullptr;
    QComboBox *m_cursorCombo = nullptr;
    QCheckBox *m_cursorBlinkCheck = nullptr;

    // панели и прокрутка
    QCheckBox *m_sidebarCheck = nullptr;
    QCheckBox *m_statusBarCheck = nullptr;
    QCheckBox *m_menuBarCheck = nullptr;
    QCheckBox *m_tabBarCheck = nullptr;
    QSpinBox *m_scrollbackSpin = nullptr;
    QSpinBox *m_wheelSpin = nullptr;
    QCheckBox *m_scrollOutputCheck = nullptr;
    QCheckBox *m_scrollKeystrokeCheck = nullptr;

    // поведение
    QComboBox *m_shellCombo = nullptr;
    QLineEdit *m_startDirEdit = nullptr;
    QCheckBox *m_restoreCheck = nullptr;
    QCheckBox *m_newTabDirCheck = nullptr;
    QCheckBox *m_warnCloseCheck = nullptr;
    QCheckBox *m_confirmWindowCheck = nullptr;
    QCheckBox *m_confirmPasteCheck = nullptr;
    QCheckBox *m_bellCheck = nullptr;
    QCheckBox *m_autoSaveCheck = nullptr;

    // горячие клавиши
    QTableWidget *m_shortcutTable = nullptr;

    // уведомления
    QCheckBox *m_notifyEnabledCheck = nullptr;
    QCheckBox *m_notifyLongCheck = nullptr;
    QSpinBox *m_notifySecondsSpin = nullptr;
    QCheckBox *m_recordHistoryCheck = nullptr;
    QCheckBox *m_notifyScriptCheck = nullptr;
    QCheckBox *m_notifySshCheck = nullptr;
    QCheckBox *m_notifyErrorsCheck = nullptr;
    QCheckBox *m_notifyDesktopCheck = nullptr;

    // конфигурация
    QLabel *m_configPathLabel = nullptr;
    QLabel *m_backupLabel = nullptr;
    QList<QPushButton *> m_configButtons;

    QString m_importedPath;
    bool m_updating = false;
};

} // namespace BlackCore
