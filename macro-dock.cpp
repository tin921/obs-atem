#include "macro-dock.h"
#include "settings-dialog.h"

#include <obs-module.h>
#include <QLineEdit>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QTextEdit>
#include <chrono>
#include <iomanip>
#include <sstream>

// ── Stylesheet (matches OBS dark theme) ──────────────────────

static const char* DOCK_STYLE = R"(
    QDockWidget {
        font-family: -apple-system, "Segoe UI", sans-serif;
        font-size: 12px;
        color: #cccccc;
    }
    QDockWidget::title {
        background: #252526;
        padding: 4px 8px;
        font-weight: bold;
    }

    /* Header bar */
    #headerBar {
        background: #252526;
        border-bottom: 1px solid #3c3c3c;
        padding: 4px 8px;
    }

    /* Status dot */
    #statusDot {
        font-size: 8px;
    }

    /* General labels */
    QLabel {
        color: #cccccc;
        font-size: 11px;
    }
    QLabel#statusLabel {
        color: #888888;
        font-size: 10px;
    }

    /* Buttons */
    QPushButton {
        background: #2d2d30;
        border: 1px solid #3c3c3c;
        border-radius: 3px;
        color: #cccccc;
        padding: 5px 12px;
        font-size: 11px;
    }
    QPushButton:hover {
        background: #383838;
        border-color: #555555;
    }
    QPushButton:pressed {
        background: #094771;
        border-color: #c83232;
    }
    QPushButton#connectUSBBtn {
        background: #c83232;
        border-color: #c83232;
        color: white;
        font-size: 12px;
        padding: 8px;
        font-weight: bold;
    }
    QPushButton#connectUSBBtn:hover {
        background: #e04040;
    }

    /* Tool buttons (gear, refresh) */
    QToolButton {
        background: none;
        border: 1px solid #3c3c3c;
        border-radius: 3px;
        color: #888888;
        padding: 3px;
        font-size: 13px;
        min-width: 24px;
        min-height: 24px;
    }
    QToolButton:hover {
        background: #383838;
        color: #e0e0e0;
        border-color: #555555;
    }

    /* Macro buttons */
    QPushButton.macroBtn {
        background: #252526;
        border: 1px solid #3c3c3c;
        border-radius: 4px;
        color: #e0e0e0;
        padding: 8px 4px;
        font-size: 11px;
        font-weight: 500;
        min-height: 48px;
    }
    QPushButton.macroBtn:hover {
        background: #383838;
        border-color: #555555;
    }
    QPushButton.macroBtn:pressed {
        background: #094771;
        border-color: #c83232;
    }
    QPushButton.macroBtn[running="true"] {
        border-color: #4ec960;
        background: rgba(78, 201, 96, 0.08);
    }

    /* Stop button */
    QPushButton#stopBtn {
        background: #c83232;
        border: none;
        border-radius: 3px;
        color: white;
        font-size: 10px;
        font-weight: bold;
        padding: 4px 10px;
        text-transform: uppercase;
    }
    QPushButton#stopBtn:hover {
        background: #e03a3a;
    }

    /* Player bar */
    #playerBar {
        background: #2d2d30;
        border-top: 1px solid #3c3c3c;
        padding: 4px 8px;
    }
    QLabel#runningLabel {
        color: #4ec960;
        font-size: 11px;
    }

    /* Scroll area */
    QScrollArea {
        background: #1e1e1e;
        border: none;
    }
    QScrollBar:vertical {
        background: #1e1e1e;
        width: 6px;
    }
    QScrollBar::handle:vertical {
        background: #444444;
        border-radius: 3px;
        min-height: 20px;
    }
    QScrollBar::handle:vertical:hover {
        background: #555555;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
        height: 0;
    }

    /* Line edit for IP */
    QLineEdit {
        background: #1e1e1e;
        border: 1px solid #3c3c3c;
        border-radius: 3px;
        color: #cccccc;
        padding: 5px 8px;
        font-size: 11px;
    }
    QLineEdit:focus {
        border-color: #c83232;
    }

    /* Separator */
    QLabel#dividerText {
        color: #555555;
        font-size: 10px;
    }
)";

// ── MacroButton ──────────────────────────────────────────────

MacroButton::MacroButton(const AtemMacroInfo& info, QWidget* parent)
    : QPushButton(parent), m_index(info.index)
{
    setProperty("class", "macroBtn");
    setObjectName("macroBtn");

    // Display: index number + name
    QString label = QString("#%1\n%2")
        .arg(info.index + 1)
        .arg(QString::fromStdString(info.name));
    setText(label);

    if (!info.description.empty()) {
        setToolTip(QString::fromStdString(info.description));
    }

    setMinimumHeight(48);
    setCursor(Qt::PointingHandCursor);
}

void MacroButton::setRunning(bool running) {
    if (m_running == running) return;
    m_running = running;
    setProperty("running", running ? "true" : "false");
    style()->unpolish(this);
    style()->polish(this);
}

// ── AtemMacroDock ────────────────────────────────────────────

AtemMacroDock::AtemMacroDock(QWidget* parent)
    : QDockWidget("ATEM Macros", parent)
{
    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: QDockWidget base OK");

    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: creating AtemController...");
    m_atem = std::make_unique<AtemController>();
    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: AtemController created, lastError='%s'",
         m_atem->lastError().c_str());

    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: setting callbacks...");
    m_atem->setStateChangeCallback([this](AtemState) {
        QMetaObject::invokeMethod(this, "onRefresh", Qt::QueuedConnection);
    });
    m_atem->setMacroUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, "onRefresh", Qt::QueuedConnection);
    });

    m_atem->setTraceCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            if (m_traceArea) {
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
                std::stringstream ss;
                ss << std::put_time(std::localtime(&time_t_now), "[%H:%M:%S.")
                   << std::setfill('0') << std::setw(3) << ms.count() << "] " << msg;
                m_traceArea->append(QString::fromStdString(ss.str()));
            }
        });
    });

    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: creating poll timer...");
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &AtemMacroDock::pollUpdate);

    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: buildUI...");
    buildUI();
    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: applyStyleSheet...");
    applyStyleSheet();

    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: scheduling auto-connect...");
    QTimer::singleShot(500, this, &AtemMacroDock::onConnectUSB);
    blog(LOG_INFO, "[ATEM Macros] AtemMacroDock ctor: done");
}

AtemMacroDock::~AtemMacroDock() {
    m_pollTimer->stop();
    m_atem->disconnect();
}

void AtemMacroDock::applyStyleSheet() {
    setStyleSheet(DOCK_STYLE);
}

void AtemMacroDock::buildUI() {
    blog(LOG_INFO, "[ATEM Macros] buildUI: centralWidget...");
    m_centralWidget = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    blog(LOG_INFO, "[ATEM Macros] buildUI: headerBar...");
    m_headerBar = new QFrame(m_centralWidget);
    m_headerBar->setObjectName("headerBar");
    auto* headerLayout = new QHBoxLayout(m_headerBar);
    headerLayout->setContentsMargins(8, 4, 8, 4);
    headerLayout->setSpacing(6);

    blog(LOG_INFO, "[ATEM Macros] buildUI: statusDot...");
    m_statusDot = new QLabel("*", m_headerBar);
    m_statusDot->setObjectName("statusDot");
    m_statusDot->setStyleSheet("color: #c83232; font-size: 8px;");
    headerLayout->addWidget(m_statusDot);

    blog(LOG_INFO, "[ATEM Macros] buildUI: titleLabel...");
    auto* titleLabel = new QLabel("ATEM MACROS", m_headerBar);
    titleLabel->setStyleSheet("font-size: 10px; font-weight: 600; color: #888888;");
    headerLayout->addWidget(titleLabel);

    blog(LOG_INFO, "[ATEM Macros] buildUI: statusLabel...");
    m_statusLabel = new QLabel("disconnected", m_headerBar);
    m_statusLabel->setObjectName("statusLabel");
    headerLayout->addWidget(m_statusLabel);
    headerLayout->addStretch();

    blog(LOG_INFO, "[ATEM Macros] buildUI: refreshBtn...");
    m_refreshBtn = new QToolButton(m_headerBar);
    m_refreshBtn->setText("R");
    m_refreshBtn->setToolTip("Refresh macros");
    connect(m_refreshBtn, &QToolButton::clicked, this, &AtemMacroDock::onRefresh);
    headerLayout->addWidget(m_refreshBtn);

    blog(LOG_INFO, "[ATEM Macros] buildUI: settingsBtn...");
    m_settingsBtn = new QToolButton(m_headerBar);
    m_settingsBtn->setText("S");
    m_settingsBtn->setToolTip("Connection settings");
    connect(m_settingsBtn, &QToolButton::clicked, this, &AtemMacroDock::onSettings);
    headerLayout->addWidget(m_settingsBtn);

    m_mainLayout->addWidget(m_headerBar);

    blog(LOG_INFO, "[ATEM Macros] buildUI: contentArea...");
    m_contentArea = new QWidget(m_centralWidget);
    m_contentLayout = new QVBoxLayout(m_contentArea);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->addWidget(m_contentArea, 1);

    blog(LOG_INFO, "[ATEM Macros] buildUI: playerBar...");
    m_playerBar = new QFrame(m_centralWidget);
    m_playerBar->setObjectName("playerBar");
    m_playerBar->setVisible(false);
    auto* playerLayout = new QHBoxLayout(m_playerBar);
    playerLayout->setContentsMargins(8, 4, 8, 4);
    playerLayout->setSpacing(8);

    blog(LOG_INFO, "[ATEM Macros] buildUI: runningLabel...");
    m_runningLabel = new QLabel("> --", m_playerBar);
    m_runningLabel->setObjectName("runningLabel");
    playerLayout->addWidget(m_runningLabel, 1);

    blog(LOG_INFO, "[ATEM Macros] buildUI: stopBtn...");
    m_stopBtn = new QPushButton("STOP", m_playerBar);
    m_stopBtn->setObjectName("stopBtn");
    connect(m_stopBtn, &QPushButton::clicked, this, &AtemMacroDock::onStopMacro);
    playerLayout->addWidget(m_stopBtn);

    // Add trace area before player bar
    auto* traceWidget = new QWidget(m_centralWidget);
    auto* traceHLayout = new QHBoxLayout(traceWidget);
    traceHLayout->setContentsMargins(4, 4, 4, 4);
    traceHLayout->setSpacing(4);

    m_traceArea = new QTextEdit(traceWidget);
    m_traceArea->setReadOnly(true);
    m_traceArea->setMaximumHeight(80);
    m_traceArea->setStyleSheet("background: #1a1a1a; color: #aaaaaa; font-family: Consolas, monospace; font-size: 10px; border: 1px solid #3c3c3c;");
    traceHLayout->addWidget(m_traceArea, 1);

    m_copyBtn = new QPushButton("Copy", traceWidget);
    m_copyBtn->setToolTip("Copy connection trace to clipboard");
    m_copyBtn->setStyleSheet("padding: 2px; font-size: 10px; height: 16px;");
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        if (m_traceArea) {
            m_traceArea->selectAll();
            m_traceArea->copy();
            // optionally deselect after copy
            auto cursor = m_traceArea->textCursor();
            cursor.clearSelection();
            m_traceArea->setTextCursor(cursor);
        }
    });
    // Add copy button at the top of the hbox
    auto* btnVLayout = new QVBoxLayout();
    btnVLayout->addWidget(m_copyBtn);
    btnVLayout->addStretch();
    traceHLayout->addLayout(btnVLayout);

    m_mainLayout->addWidget(traceWidget);

    m_mainLayout->addWidget(m_playerBar);

    blog(LOG_INFO, "[ATEM Macros] buildUI: setWidget...");
    setWidget(m_centralWidget);

    blog(LOG_INFO, "[ATEM Macros] buildUI: showConnectView...");
    showConnectView();
    blog(LOG_INFO, "[ATEM Macros] buildUI: done");
}

void AtemMacroDock::showConnectView() {
    // Clear content
    QLayoutItem* child;
    while ((child = m_contentLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    m_macroButtons.clear();

    auto* container = new QWidget(m_contentArea);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(14, 20, 14, 20);
    layout->setAlignment(Qt::AlignCenter);

    auto* msg = new QLabel("ATEM not connected.\nConnect via USB or enter IP address.", container);
    msg->setAlignment(Qt::AlignCenter);
    msg->setStyleSheet("color: #888888; font-size: 11px; margin-bottom: 12px;");
    msg->setWordWrap(true);
    layout->addWidget(msg);

    auto* usbBtn = new QPushButton("Connect via USB (auto-detect)", container);
    usbBtn->setObjectName("connectUSBBtn");
    connect(usbBtn, &QPushButton::clicked, this, &AtemMacroDock::onConnectUSB);
    layout->addWidget(usbBtn);

    auto* divider = new QLabel("— or —", container);
    divider->setObjectName("dividerText");
    divider->setAlignment(Qt::AlignCenter);
    layout->addWidget(divider);

    auto* ipRow = new QWidget(container);
    auto* ipLayout = new QHBoxLayout(ipRow);
    ipLayout->setContentsMargins(0, 0, 0, 0);
    ipLayout->setSpacing(6);

    auto* ipInput = new QLineEdit(ipRow);
    ipInput->setText("127.0.0.1");
    ipInput->setObjectName("ipInput");
    ipLayout->addWidget(ipInput, 1);

    auto* ipBtn = new QPushButton("Connect", ipRow);
    connect(ipBtn, &QPushButton::clicked, this, [this]() { onConnectIP(); });
    ipLayout->addWidget(ipBtn);

    layout->addWidget(ipRow);

    m_contentLayout->addWidget(container);
    m_pollTimer->stop();
}

void AtemMacroDock::showMacroView() {
    QLayoutItem* child;
    while ((child = m_contentLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    m_macroButtons.clear();

    m_scrollArea = new QScrollArea(m_contentArea);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridWidget = new QWidget(m_scrollArea);
    m_gridWidget->setStyleSheet("background: #1e1e1e;");
    m_gridLayout = new QGridLayout(m_gridWidget);
    m_gridLayout->setContentsMargins(6, 6, 6, 6);
    m_gridLayout->setSpacing(4);

    auto macros = m_atem->getMacros();
    m_cachedMacros = macros;

    int row = 0, col = 0;
    for (const auto& macro : macros) {
        auto* btn = new MacroButton(macro, m_gridWidget);
        connect(btn, &QPushButton::clicked, this, [this, idx = macro.index]() {
            onMacroClicked(idx);
        });
        m_gridLayout->addWidget(btn, row, col);
        m_macroButtons.push_back(btn);

        col++;
        if (col >= 2) { col = 0; row++; }
    }

    // Add stretch at bottom
    m_gridLayout->setRowStretch(row + 1, 1);

    m_scrollArea->setWidget(m_gridWidget);
    m_contentLayout->addWidget(m_scrollArea);

    // Start polling for running state
    m_pollTimer->start(500);
}

void AtemMacroDock::showEmptyView() {
    QLayoutItem* child;
    while ((child = m_contentLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    m_macroButtons.clear();

    auto* container = new QWidget(m_contentArea);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(14, 30, 14, 30);
    layout->setAlignment(Qt::AlignCenter);

    auto* icon = new QLabel("🎛", container);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size: 28px; opacity: 0.4;");
    layout->addWidget(icon);

    auto* msg = new QLabel("No macros found.\nRecord macros in ATEM Software Control\nand they'll appear here.", container);
    msg->setAlignment(Qt::AlignCenter);
    msg->setStyleSheet("color: #888888; font-size: 11px;");
    msg->setWordWrap(true);
    layout->addWidget(msg);

    m_contentLayout->addWidget(container);
    m_pollTimer->start(3000); // Slow poll in case macros are added
}

// ── Slots ────────────────────────────────────────────────────

void AtemMacroDock::onConnectUSB() {
    m_statusDot->setStyleSheet("color: #cca832; font-size: 8px;");
    m_statusLabel->setText("connecting…");
    QApplication::processEvents();

    if (m_atem->connectUSB()) {
        updateStatusBar();
        refreshMacros();
    } else {
        updateStatusBar();
        showConnectView();
    }
}

void AtemMacroDock::onConnectIP() {
    auto* ipInput = m_contentArea->findChild<QLineEdit*>("ipInput");
    if (!ipInput || ipInput->text().trimmed().isEmpty()) return;

    QString ip = ipInput->text().trimmed();
    blog(LOG_INFO, "[ATEM Macros] onConnectIP: '%s'", ip.toStdString().c_str());

    m_statusDot->setStyleSheet("color: #cca832; font-size: 8px;");
    m_statusLabel->setText("connecting...");
    QApplication::processEvents();

    bool ok = m_atem->connectIP(ip.toStdString());
    blog(LOG_INFO, "[ATEM Macros] onConnectIP: result=%d lastError='%s'",
         (int)ok, m_atem->lastError().c_str());

    updateStatusBar();
    if (ok) {
        refreshMacros();
    } else {
        showConnectView();
    }
}

void AtemMacroDock::onDisconnect() {
    m_atem->disconnect();
    updateStatusBar();
    showConnectView();
}

void AtemMacroDock::onRefresh() {
    updateStatusBar();
    if (m_atem->state() == AtemState::Connected) {
        refreshMacros();
    } else {
        showConnectView();
    }
}

void AtemMacroDock::onSettings() {
    SettingsDialog dlg(m_atem.get(), this);
    if (dlg.exec() == QDialog::Accepted) {
        auto action = dlg.selectedAction();
        if (action == SettingsDialog::Action::ConnectUSB) {
            onConnectUSB();
        } else if (action == SettingsDialog::Action::ConnectIP) {
            m_atem->connectIP(dlg.ipAddress().toStdString());
            updateStatusBar();
            refreshMacros();
        } else if (action == SettingsDialog::Action::Disconnect) {
            onDisconnect();
        }
    }
}

void AtemMacroDock::onStopMacro() {
    m_atem->stopMacro();
}

void AtemMacroDock::onMacroClicked(uint32_t index) {
    m_atem->runMacro(index);
}

void AtemMacroDock::refreshMacros() {
    auto macros = m_atem->getMacros();
    if (macros.empty()) {
        showEmptyView();
    } else {
        showMacroView();
    }
}

void AtemMacroDock::pollUpdate() {
    if (m_atem->state() != AtemState::Connected) {
        m_playerBar->setVisible(false);
        return;
    }

    int runIdx = m_atem->runningMacroIndex();
    bool running = m_atem->isRunning();

    // Update macro button highlights
    for (auto* btn : m_macroButtons) {
        btn->setRunning(running && static_cast<int>(btn->macroIndex()) == runIdx);
    }

    // Update player bar
    if (running && runIdx >= 0) {
        QString name = QString("Macro %1").arg(runIdx + 1);
        for (const auto& m : m_cachedMacros) {
            if (static_cast<int>(m.index) == runIdx) {
                name = QString::fromStdString(m.name);
                break;
            }
        }
        m_runningLabel->setText(QString("▶ %1").arg(name));
        m_playerBar->setVisible(true);
    } else {
        m_playerBar->setVisible(false);
    }

    m_lastRunningIndex = runIdx;
}

void AtemMacroDock::updateStatusBar() {
    auto state = m_atem->state();

    switch (state) {
    case AtemState::Connected:
        m_statusDot->setStyleSheet("color: #4ec960; font-size: 8px;");
        m_statusLabel->setText(QString::fromStdString(
            m_atem->modelName().empty() ? m_atem->connectedAddress() : m_atem->modelName()
        ));
        break;
    case AtemState::Connecting:
        m_statusDot->setStyleSheet("color: #cca832; font-size: 8px;");
        m_statusLabel->setText("connecting…");
        break;
    default:
        m_statusDot->setStyleSheet("color: #c83232; font-size: 8px;");
        m_statusLabel->setText("disconnected");
        break;
    }
}
