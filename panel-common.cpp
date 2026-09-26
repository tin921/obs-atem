#include "panel-common.h"

#include <QHBoxLayout>
#include <QStyle>

const char* const kPanelStyleSheet = R"(
    QWidget {
        font-family: "Segoe UI", sans-serif;
        font-size: 12px;
        color: #cccccc;
    }
    QLabel {
        font-size: 11px;
    }
    #atemPanel {
        background: #1e1e1e;
    }

    /* Header bar */
    #headerBar {
        background: #252526;
        border-bottom: 1px solid #3c3c3c;
    }
    #panelTitle {
        font-size: 10px;
        font-weight: 600;
        color: #888888;
    }
    #statusLabel {
        color: #888888;
        font-size: 10px;
    }
    #statusDot {
        font-size: 10px;
        color: #c83232;
    }
    #statusDot[state="connecting"] { color: #cca832; }
    #statusDot[state="connected"]  { color: #4ec960; }

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
    }
    QPushButton:disabled {
        color: #666666;
        border-color: #333333;
    }
    QPushButton#primaryBtn {
        background: #c83232;
        border-color: #c83232;
        color: white;
        font-size: 12px;
        padding: 8px;
        font-weight: bold;
    }
    QPushButton#primaryBtn:hover {
        background: #e04040;
    }

    /* Tool buttons (gear, refresh) */
    QToolButton {
        background: none;
        border: 1px solid #3c3c3c;
        border-radius: 3px;
        color: #888888;
        padding: 2px;
        font-size: 13px;
        min-width: 22px;
        min-height: 22px;
    }
    QToolButton:hover {
        background: #383838;
        color: #e0e0e0;
        border-color: #555555;
    }

    /* Macro buttons */
    QPushButton#macroBtn {
        background: #252526;
        border: 1px solid #3c3c3c;
        border-radius: 4px;
        color: #e0e0e0;
        padding: 8px 4px;
        font-weight: 500;
        min-height: 48px;
    }
    QPushButton#macroBtn:hover {
        background: #383838;
        border-color: #555555;
    }
    QPushButton#macroBtn:pressed {
        background: #094771;
        border-color: #c83232;
    }
    QPushButton#macroBtn[running="true"] {
        border: 2px solid #4ec960;
        background: #1f3324;
    }

    /* Stop / destructive */
    QPushButton#stopBtn {
        background: #c83232;
        border: none;
        border-radius: 3px;
        color: white;
        font-size: 10px;
        font-weight: bold;
        padding: 4px 10px;
    }
    QPushButton#stopBtn:hover {
        background: #e03a3a;
    }

    /* Player bar */
    #playerBar {
        background: #2d2d30;
        border-top: 1px solid #3c3c3c;
    }
    #runningLabel {
        color: #4ec960;
    }

    /* Messages */
    #hintText {
        color: #888888;
    }
    #errorText {
        color: #e04040;
    }
    #dividerText {
        color: #555555;
        font-size: 10px;
    }

    /* Scroll area */
    QScrollArea, #scrollBody {
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

    /* Inputs */
    QLineEdit, QDoubleSpinBox, QComboBox {
        background: #1e1e1e;
        border: 1px solid #3c3c3c;
        border-radius: 3px;
        color: #cccccc;
        padding: 3px 6px;
        font-size: 11px;
    }
    QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus {
        border-color: #c83232;
    }
    QComboBox QAbstractItemView {
        background: #252526;
        color: #cccccc;
        selection-background-color: #094771;
    }
)";

void applyPanelStyle(QWidget* panel, const QString& extraStyle) {
    // Paint our own background: the text colours below assume a dark panel,
    // whatever OBS theme is active.
    panel->setObjectName("atemPanel");
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setStyleSheet(QString(kPanelStyleSheet) + extraStyle);
}

PanelHeader::PanelHeader(const QString& title, QWidget* parent)
    : QFrame(parent)
{
    setObjectName("headerBar");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    m_dot = new QLabel("●", this);
    m_dot->setObjectName("statusDot");
    layout->addWidget(m_dot);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName("panelTitle");
    layout->addWidget(titleLabel);

    m_status = new QLabel("disconnected", this);
    m_status->setObjectName("statusLabel");
    layout->addWidget(m_status, 1);
}

QToolButton* PanelHeader::addToolButton(const QString& text, const QString& tooltip) {
    auto* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    layout()->addWidget(btn);
    return btn;
}

void PanelHeader::setConnectionState(AtemState state, const QString& detail) {
    const char* key = "disconnected";
    if (state == AtemState::Connected) key = "connected";
    else if (state == AtemState::Connecting) key = "connecting";

    m_dot->setProperty("state", key);
    m_dot->style()->unpolish(m_dot);
    m_dot->style()->polish(m_dot);
    m_status->setText(detail.isEmpty() ? QString(key) : detail);
}
