#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QTextEdit>

#include "atem-session.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action {
        None,
        ConnectUSB,
        ConnectIP,
        Disconnect
    };

    explicit SettingsDialog(AtemSession* session, QWidget* parent = nullptr);

    Action selectedAction() const { return m_action; }
    QString ipAddress() const { return m_ipInput ? m_ipInput->text().trimmed() : ""; }

private:
    void buildUI();
    void applyStyle();

    AtemSession* m_session;
    Action m_action = Action::None;

    QLineEdit*  m_ipInput = nullptr;
    QTextEdit*  m_logArea = nullptr;
};
