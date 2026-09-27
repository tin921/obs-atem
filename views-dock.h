#pragma once

#include <QWidget>
#include <vector>

#include "atem-session.h"

class PanelHeader;
class PipSettings;
class PresetButton;
class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

// ── Views panel ──────────────────────────────────────────────
//
// The operator's launcher for the PiP presets built in the PiP panel, the
// way the macro panel launches the switcher's macros: the chosen presets'
// pictures in two columns; clicking one recalls it, the one on air is lit.
// ⚙ chooses which presets appear and in what order (left to right, top to
// bottom). Talks to the switcher itself, so the PiP panel need not be open.

class AtemViewsDock : public QWidget {
    Q_OBJECT
public:
    AtemViewsDock(AtemSession* session, PipSettings* settings, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUI();
    QWidget* buildSettingsPage();
    void rebuildGrid();
    void layoutGrid();
    void updateLit();
    void updatePage();
    void fillSettingsList();
    void storeSettingsList();
    void moveSelected(int delta);
    QString describe(int index) const;

    AtemSession* m_session;
    PipSettings* m_settings;

    PanelHeader* m_header = nullptr;
    QStackedWidget* m_pages = nullptr;
    QWidget* m_offlinePage = nullptr;
    QLabel* m_offlineText = nullptr;
    QPushButton* m_connectBtn = nullptr;
    QWidget* m_mainPage = nullptr;
    QWidget* m_settingsPage = nullptr;
    bool m_showingSettings = false;

    QWidget* m_gridBody = nullptr;
    QGridLayout* m_grid = nullptr;
    QLabel* m_emptyHint = nullptr;
    std::vector<std::pair<int, PresetButton*>> m_buttons;   // preset index, button

    QListWidget* m_list = nullptr;
    bool m_fillingList = false;
};
