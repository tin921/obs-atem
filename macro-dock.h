#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QFrame>
#include <vector>

#include "atem-session.h"

class PanelHeader;

// ── Individual Macro Button ──────────────────────────────────

class MacroButton : public QPushButton {
    Q_OBJECT
public:
    MacroButton(const AtemMacroInfo& info, QWidget* parent = nullptr);
    void setRunning(bool running);
    uint32_t macroIndex() const { return m_index; }

private:
    uint32_t m_index;
    bool m_running = false;
};

// ── Macro panel ──────────────────────────────────────────────
//
// Dock content widget: OBS 30+ wraps it in its own dock (see plugin-main.cpp),
// the harness wraps it in a QDockWidget. Also owns the connection UI (connect
// view, settings dialog) for the shared AtemSession.

class AtemMacroDock : public QWidget {
    Q_OBJECT
public:
    explicit AtemMacroDock(AtemSession* session, QWidget* parent = nullptr);

private slots:
    void onConnectUSB();
    void onConnectIP();
    void onSettings();
    void onStopMacro();
    void onResumeMacro();
    void onMacroClicked(uint32_t index);
    void onConnectionChanged(AtemState state);
    void rebuildContent();
    void pollRunStatus();

private:
    void buildUI();
    void clearContent();
    void showConnectView();
    void showMacroView(const std::vector<AtemMacroInfo>& macros);
    void showEmptyView();
    void updateStatusBar();

    AtemSession* m_session;

    // Header
    PanelHeader*  m_header = nullptr;
    QToolButton*  m_settingsBtn = nullptr;
    QToolButton*  m_refreshBtn = nullptr;

    // Content area (swapped between connect / macro / empty views)
    QWidget*      m_contentArea = nullptr;
    QVBoxLayout*  m_contentLayout = nullptr;
    std::vector<MacroButton*> m_macroButtons;

    // Player bar (bottom)
    QFrame*       m_playerBar = nullptr;
    QLabel*       m_runningLabel = nullptr;
    QPushButton*  m_resumeBtn = nullptr;
    QPushButton*  m_stopBtn = nullptr;

    QTimer*       m_pollTimer = nullptr;

    std::vector<AtemMacroInfo> m_cachedMacros;
};
