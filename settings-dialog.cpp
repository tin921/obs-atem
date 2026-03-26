#include "settings-dialog.h"

static const char* SETTINGS_STYLE = R"(
    QDialog {
        background: #1e1e1e;
        color: #cccccc;
        font-family: -apple-system, "Segoe UI", sans-serif;
        font-size: 12px;
    }
    QGroupBox {
        background: #252526;
        border: 1px solid #3c3c3c;
        border-radius: 4px;
        margin-top: 12px;
        padding: 12px;
        padding-top: 24px;
        font-weight: bold;
        color: #cccccc;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 10px;
        padding: 0 4px;
        color: #888888;
        font-size: 10px;
        text-transform: uppercase;
    }
    QLabel {
        color: #cccccc;
        font-size: 11px;
    }
    QLabel.value {
        color: #e0e0e0;
        font-weight: 500;
    }
    QLabel.error {
        color: #e04040;
    }
    QLabel.success {
        color: #4ec960;
    }
    QPushButton {
        background: #2d2d30;
        border: 1px solid #3c3c3c;
        border-radius: 3px;
        color: #cccccc;
        padding: 6px 14px;
        font-size: 11px;
    }
    QPushButton:hover {
        background: #383838;
        border-color: #555555;
    }
    QPushButton#connectBtn {
        background: #c83232;
        border-color: #c83232;
        color: white;
        font-weight: bold;
    }
    QPushButton#connectBtn:hover {
        background: #e04040;
    }
    QPushButton#disconnectBtn {
        background: #6e2020;
        border-color: #6e2020;
        color: #e0a0a0;
    }
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
    QTextEdit {
        background: #1a1a1a;
        border: 1px solid #3c3c3c;
        border-radius: 3px;
        color: #888888;
        font-family: "Consolas", "Courier New", monospace;
        font-size: 10px;
        padding: 4px;
    }
)";

SettingsDialog::SettingsDialog(AtemController* atem, QWidget* parent)
    : QDialog(parent), m_atem(atem)
{
    setWindowTitle("ATEM Connection Settings");
    setMinimumWidth(380);
    buildUI();
    applyStyle();
}

void SettingsDialog::buildUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    bool isConnected = m_atem->state() == AtemState::Connected;

    // ── Connection Status Group ──────────────────────────────
    auto* statusGroup = new QGroupBox("Connection Status", this);
    auto* statusLayout = new QVBoxLayout(statusGroup);

    auto addRow = [&](const QString& label, const QString& value, const QString& cls = "value") {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(label, statusGroup);
        lbl->setStyleSheet("color: #888888;");
        lbl->setFixedWidth(100);
        row->addWidget(lbl);
        auto* val = new QLabel(value, statusGroup);
        val->setProperty("class", cls);
        if (cls == "error") val->setStyleSheet("color: #e04040;");
        else if (cls == "success") val->setStyleSheet("color: #4ec960; font-weight: 500;");
        else val->setStyleSheet("color: #e0e0e0; font-weight: 500;");
        row->addWidget(val, 1);
        statusLayout->addLayout(row);
    };

    if (isConnected) {
        addRow("Status:", "Connected", "success");
        addRow("Model:", QString::fromStdString(m_atem->modelName()));
        addRow("Address:", QString::fromStdString(m_atem->connectedAddress()));

        auto macros = m_atem->getMacros();
        addRow("Macros:", QString::number(macros.size()) + " loaded");
    } else {
        addRow("Status:", "Disconnected", "error");
        if (!m_atem->lastError().empty()) {
            addRow("Last Error:", QString::fromStdString(m_atem->lastError()), "error");
        }
    }

    layout->addWidget(statusGroup);

    // ── Connection Actions Group ─────────────────────────────
    auto* connectGroup = new QGroupBox("Connection", this);
    auto* connectLayout = new QVBoxLayout(connectGroup);

    if (isConnected) {
        auto* disconnectBtn = new QPushButton("Disconnect", connectGroup);
        disconnectBtn->setObjectName("disconnectBtn");
        connect(disconnectBtn, &QPushButton::clicked, this, [this]() {
            m_action = Action::Disconnect;
            accept();
        });
        connectLayout->addWidget(disconnectBtn);
    } else {
        auto* usbBtn = new QPushButton("Connect via USB (auto-detect)", connectGroup);
        usbBtn->setObjectName("connectBtn");
        connect(usbBtn, &QPushButton::clicked, this, [this]() {
            m_action = Action::ConnectUSB;
            accept();
        });
        connectLayout->addWidget(usbBtn);

        auto* divider = new QLabel("— or enter IP manually —", connectGroup);
        divider->setAlignment(Qt::AlignCenter);
        divider->setStyleSheet("color: #555555; font-size: 10px; margin: 4px 0;");
        connectLayout->addWidget(divider);

        auto* ipRow = new QHBoxLayout();
        m_ipInput = new QLineEdit(connectGroup);
        m_ipInput->setPlaceholderText("192.168.10.240");
        ipRow->addWidget(m_ipInput, 1);

        auto* ipBtn = new QPushButton("Connect", connectGroup);
        connect(ipBtn, &QPushButton::clicked, this, [this]() {
            m_action = Action::ConnectIP;
            accept();
        });
        ipRow->addWidget(ipBtn);
        connectLayout->addLayout(ipRow);
    }

    layout->addWidget(connectGroup);

    // ── Troubleshooting Group ────────────────────────────────
    auto* troubleGroup = new QGroupBox("Troubleshooting", this);
    auto* troubleLayout = new QVBoxLayout(troubleGroup);

    m_logArea = new QTextEdit(troubleGroup);
    m_logArea->setReadOnly(true);
    m_logArea->setMaximumHeight(120);

    QString tips;
    tips += "Checklist:\n";
    tips += "  ✓ ATEM Software Control installed (provides SDK DLLs)\n";
    tips += "  ✓ USB cable connected to ATEM Mini\n";
    tips += "  ✓ ATEM appears as network adapter in Device Manager\n";
    tips += "  ✓ No other program exclusively holding the connection\n";
    tips += "\n";
    tips += "USB Detection:\n";
    tips += "  Run 'ipconfig' and look for a 'Blackmagic Design'\n";
    tips += "  adapter with a 169.254.x.x address.\n";
    tips += "\n";
    tips += "Network Connection:\n";
    tips += "  Ensure your PC is on the same subnet as the ATEM.\n";
    tips += "  Default ATEM IP is usually 192.168.10.240.\n";
    tips += "\n";

    if (!m_atem->lastError().empty()) {
        tips += "Last Error:\n  " + QString::fromStdString(m_atem->lastError()) + "\n\n";
    }

    tips += "SDK Path:\n";
    tips += "  C:\\Program Files (x86)\\Blackmagic Design\\\n";
    tips += "    Blackmagic ATEM Switchers\\BMDSwitcherAPI64.dll\n";

    m_logArea->setPlainText(tips);
    troubleLayout->addWidget(m_logArea);

    layout->addWidget(troubleGroup);

    // ── Close button ─────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}

void SettingsDialog::applyStyle() {
    setStyleSheet(SETTINGS_STYLE);
}
