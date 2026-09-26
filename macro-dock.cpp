#include "macro-dock.h"
#include "panel-common.h"
#include "settings-dialog.h"

#include "obs-log.h"
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QStyle>

// ── MacroButton ──────────────────────────────────────────────

MacroButton::MacroButton(const AtemMacroInfo& info, QWidget* parent)
    : QPushButton(parent), m_index(info.index)
{
    setObjectName("macroBtn");

    // Display: index number + name
    setText(QString("#%1\n%2")
        .arg(info.index + 1)
        .arg(QString::fromStdString(info.name)));

    QString tip = QString::fromStdString(info.description);
    if (info.hasUnsupportedOps) {
        if (!tip.isEmpty()) tip += "\n\n";
        tip += "⚠ Contains steps this ATEM model does not support.";
    }
    if (!tip.isEmpty()) setToolTip(tip);

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

AtemMacroDock::AtemMacroDock(AtemSession* session, QWidget* parent)
    : QWidget(parent), m_session(session)
{
    applyPanelStyle(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout, this, &AtemMacroDock::pollRunStatus);

    buildUI();

    connect(m_session, &AtemSession::connectionChanged, this, &AtemMacroDock::onConnectionChanged);
    connect(m_session, &AtemSession::macrosChanged, this, &AtemMacroDock::rebuildContent);
    connect(m_session, &AtemSession::traceMessage, this, &AtemMacroDock::onTrace);

    onConnectionChanged(m_session->state());
}

void AtemMacroDock::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_header = new PanelHeader("ATEM MACROS", this);
    m_refreshBtn = m_header->addToolButton("⟳", "Reload macros from the ATEM");
    connect(m_refreshBtn, &QToolButton::clicked, this, &AtemMacroDock::rebuildContent);
    m_settingsBtn = m_header->addToolButton("⚙", "Connection settings");
    connect(m_settingsBtn, &QToolButton::clicked, this, &AtemMacroDock::onSettings);
    mainLayout->addWidget(m_header);

    m_contentArea = new QWidget(this);
    m_contentLayout = new QVBoxLayout(m_contentArea);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_contentArea, 1);

    // Trace log: connection diagnostics for first-time hardware setup
    auto* traceWidget = new QWidget(this);
    auto* traceLayout = new QHBoxLayout(traceWidget);
    traceLayout->setContentsMargins(4, 4, 4, 4);
    traceLayout->setSpacing(4);

    m_traceArea = new QTextEdit(traceWidget);
    m_traceArea->setObjectName("traceArea");
    m_traceArea->setReadOnly(true);
    m_traceArea->setMaximumHeight(80);
    traceLayout->addWidget(m_traceArea, 1);

    auto* copyBtn = new QPushButton("Copy", traceWidget);
    copyBtn->setToolTip("Copy connection trace to clipboard");
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_traceArea->toPlainText());
    });
    auto* copyColumn = new QVBoxLayout();
    copyColumn->addWidget(copyBtn);
    copyColumn->addStretch();
    traceLayout->addLayout(copyColumn);
    mainLayout->addWidget(traceWidget);

    m_playerBar = new QFrame(this);
    m_playerBar->setObjectName("playerBar");
    m_playerBar->setVisible(false);
    auto* playerLayout = new QHBoxLayout(m_playerBar);
    playerLayout->setContentsMargins(8, 4, 8, 4);
    playerLayout->setSpacing(8);

    m_runningLabel = new QLabel(m_playerBar);
    m_runningLabel->setObjectName("runningLabel");
    playerLayout->addWidget(m_runningLabel, 1);

    m_stopBtn = new QPushButton("STOP", m_playerBar);
    m_stopBtn->setObjectName("stopBtn");
    connect(m_stopBtn, &QPushButton::clicked, this, &AtemMacroDock::onStopMacro);
    playerLayout->addWidget(m_stopBtn);
    mainLayout->addWidget(m_playerBar);
}

void AtemMacroDock::clearContent() {
    m_pollTimer->stop();
    m_playerBar->setVisible(false);
    m_macroButtons.clear();

    QLayoutItem* child;
    while ((child = m_contentLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
}

void AtemMacroDock::showConnectView() {
    clearContent();

    auto* container = new QWidget(m_contentArea);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(14, 20, 14, 20);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignCenter);

    auto* msg = new QLabel("ATEM not connected.\nConnect via USB or enter IP address.", container);
    msg->setObjectName("hintText");
    msg->setAlignment(Qt::AlignCenter);
    msg->setWordWrap(true);
    layout->addWidget(msg);

    QString error = QString::fromStdString(m_session->atem().lastError());
    if (!error.isEmpty()) {
        auto* err = new QLabel(error, container);
        err->setObjectName("errorText");
        err->setAlignment(Qt::AlignCenter);
        err->setWordWrap(true);
        layout->addWidget(err);
    }

    auto* usbBtn = new QPushButton("Connect via USB (auto-detect)", container);
    usbBtn->setObjectName("primaryBtn");
    connect(usbBtn, &QPushButton::clicked, this, &AtemMacroDock::onConnectUSB);
    layout->addWidget(usbBtn);

    auto* divider = new QLabel("— or —", container);
    divider->setObjectName("dividerText");
    divider->setAlignment(Qt::AlignCenter);
    layout->addWidget(divider);

    auto* ipRow = new QHBoxLayout();
    ipRow->setSpacing(6);
    auto* ipInput = new QLineEdit(m_session->lastIP(), container);
    ipInput->setObjectName("ipInput");
    ipInput->setPlaceholderText("192.168.10.240");
    connect(ipInput, &QLineEdit::returnPressed, this, &AtemMacroDock::onConnectIP);
    ipRow->addWidget(ipInput, 1);

    auto* ipBtn = new QPushButton("Connect", container);
    connect(ipBtn, &QPushButton::clicked, this, &AtemMacroDock::onConnectIP);
    ipRow->addWidget(ipBtn);
    layout->addLayout(ipRow);

    m_contentLayout->addWidget(container);
}

void AtemMacroDock::showMacroView(const std::vector<AtemMacroInfo>& macros) {
    clearContent();

    auto* scrollArea = new QScrollArea(m_contentArea);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* gridWidget = new QWidget(scrollArea);
    gridWidget->setObjectName("scrollBody");
    auto* gridLayout = new QGridLayout(gridWidget);
    gridLayout->setContentsMargins(6, 6, 6, 6);
    gridLayout->setSpacing(4);

    m_cachedMacros = macros;

    int row = 0, col = 0;
    for (const auto& macro : macros) {
        auto* btn = new MacroButton(macro, gridWidget);
        connect(btn, &QPushButton::clicked, this, [this, idx = macro.index]() {
            onMacroClicked(idx);
        });
        gridLayout->addWidget(btn, row, col);
        m_macroButtons.push_back(btn);

        if (++col >= 2) { col = 0; row++; }
    }
    gridLayout->setRowStretch(row + 1, 1);

    scrollArea->setWidget(gridWidget);
    m_contentLayout->addWidget(scrollArea);

    pollRunStatus();
    m_pollTimer->start();
}

void AtemMacroDock::showEmptyView() {
    clearContent();

    auto* container = new QWidget(m_contentArea);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(14, 30, 14, 30);
    layout->setAlignment(Qt::AlignCenter);

    auto* msg = new QLabel("No macros found.\nRecord macros in ATEM Software Control\n"
                           "and they'll appear here.", container);
    msg->setObjectName("hintText");
    msg->setAlignment(Qt::AlignCenter);
    msg->setWordWrap(true);
    layout->addWidget(msg);

    m_contentLayout->addWidget(container);
}

// ── Slots ────────────────────────────────────────────────────

void AtemMacroDock::onConnectUSB() {
    m_session->connectUSB();
}

void AtemMacroDock::onConnectIP() {
    auto* ipInput = m_contentArea->findChild<QLineEdit*>("ipInput");
    if (!ipInput) return;
    blog(LOG_INFO, "[ATEM Macros] onConnectIP: '%s'", ipInput->text().toStdString().c_str());
    m_session->connectIP(ipInput->text());
}

void AtemMacroDock::onSettings() {
    SettingsDialog dlg(m_session, this);
    if (dlg.exec() != QDialog::Accepted) return;

    switch (dlg.selectedAction()) {
    case SettingsDialog::Action::ConnectUSB: m_session->connectUSB(); break;
    case SettingsDialog::Action::ConnectIP:  m_session->connectIP(dlg.ipAddress()); break;
    case SettingsDialog::Action::Disconnect: m_session->disconnect(); break;
    case SettingsDialog::Action::None: break;
    }
}

void AtemMacroDock::onStopMacro() {
    m_session->atem().stopMacro();
}

void AtemMacroDock::onMacroClicked(uint32_t index) {
    m_session->atem().runMacro(index);
    pollRunStatus();
}

void AtemMacroDock::onConnectionChanged(AtemState state) {
    updateStatusBar();
    m_refreshBtn->setEnabled(state == AtemState::Connected);
    if (state != AtemState::Connecting) rebuildContent();
}

void AtemMacroDock::onTrace(const QString& line) {
    m_traceArea->append(line);
}

void AtemMacroDock::rebuildContent() {
    if (m_session->isBusy()) return;
    if (!m_session->isConnected()) {
        showConnectView();
        return;
    }
    auto macros = m_session->atem().getMacros();
    if (macros.empty()) {
        showEmptyView();
    } else {
        showMacroView(macros);
    }
}

void AtemMacroDock::pollRunStatus() {
    // While a ConnectTo is in progress the controller lock is held; the SDK
    // may pump messages, so this timer can fire re-entrantly. Skip it.
    if (m_session->isBusy() || !m_session->isConnected()) {
        m_playerBar->setVisible(false);
        return;
    }

    AtemMacroRunStatus run = m_session->atem().runStatus();

    for (auto* btn : m_macroButtons) {
        btn->setRunning(static_cast<int>(btn->macroIndex()) == run.index);
    }

    if (run.index < 0) {
        m_playerBar->setVisible(false);
        return;
    }

    QString name = QString("Macro %1").arg(run.index + 1);
    for (const auto& m : m_cachedMacros) {
        if (static_cast<int>(m.index) == run.index) {
            name = QString::fromStdString(m.name);
            break;
        }
    }
    m_runningLabel->setText(run.waitingForUser
        ? QString("⏸ %1 — waiting").arg(name)
        : QString("▶ %1").arg(name));
    m_playerBar->setVisible(true);
}

void AtemMacroDock::updateStatusBar() {
    AtemState state = m_session->state();
    if (m_session->isBusy()) state = AtemState::Connecting;

    QString detail;
    if (state == AtemState::Connected) {
        const auto& atem = m_session->atem();
        detail = QString::fromStdString(atem.modelName().empty() ? atem.connectedAddress()
                                                                 : atem.modelName());
    } else if (state == AtemState::Connecting) {
        detail = "connecting…";
    }
    m_header->setConnectionState(state, detail);
}
