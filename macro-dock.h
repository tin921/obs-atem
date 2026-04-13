#pragma once

#include <QDockWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QFrame>
#include <QTextEdit>
#include <memory>

#include "atem-controller.h"

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

// ── Main Dock Widget ─────────────────────────────────────────

class AtemMacroDock : public QDockWidget {
    Q_OBJECT
public:
    explicit AtemMacroDock(QWidget* parent = nullptr);
    ~AtemMacroDock();

private slots:
    void onConnectUSB();
    void onConnectIP();
    void onDisconnect();
    void onRefresh();
    void onSettings();
    void onStopMacro();
    void onMacroClicked(uint32_t index);
    void pollUpdate();

private:
    void buildUI();
    void showConnectView();
    void showMacroView();
    void showEmptyView();
    void refreshMacros();
    void updateStatusBar();
    void applyStyleSheet();

    // Controller
    std::unique_ptr<AtemController> m_atem;

    // UI elements
    QWidget*      m_centralWidget = nullptr;
    QVBoxLayout*  m_mainLayout = nullptr;

    // Header
    QFrame*       m_headerBar = nullptr;
    QLabel*       m_statusDot = nullptr;
    QLabel*       m_statusLabel = nullptr;
    QToolButton*  m_settingsBtn = nullptr;
    QToolButton*  m_refreshBtn = nullptr;

    // Content area (swapped between connect / macro views)
    QWidget*      m_contentArea = nullptr;
    QVBoxLayout*  m_contentLayout = nullptr;

    // Macro grid
    QScrollArea*  m_scrollArea = nullptr;
    QWidget*      m_gridWidget = nullptr;
    QGridLayout*  m_gridLayout = nullptr;
    std::vector<MacroButton*> m_macroButtons;

    // Player bar (bottom)
    QFrame*       m_playerBar = nullptr;
    QLabel*       m_runningLabel = nullptr;
    QPushButton*  m_stopBtn = nullptr;

    // Trace area
    QTextEdit*    m_traceArea = nullptr;
    QPushButton*  m_copyBtn = nullptr;

    // Poll timer
    QTimer*       m_pollTimer = nullptr;

    // Cached state
    std::vector<AtemMacroInfo> m_cachedMacros;
    int m_lastRunningIndex = -1;
};
