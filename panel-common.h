#pragma once

#include <QFrame>
#include <QLabel>
#include <QString>
#include <QToolButton>

#include "atem-controller.h"

// Shared look for the ATEM panels (OBS dark theme palette).
extern const char* const kPanelStyleSheet;

// Applies kPanelStyleSheet (+ panel-specific rules) to a panel's root widget.
void applyPanelStyle(QWidget* panel, const QString& extraStyle = QString());

// Header bar used by every panel: status dot, title, status text, then any
// tool buttons added with addToolButton().
class PanelHeader : public QFrame {
public:
    PanelHeader(const QString& title, QWidget* parent = nullptr);

    QToolButton* addToolButton(const QString& text, const QString& tooltip);
    void setConnectionState(AtemState state, const QString& detail);

private:
    QLabel* m_dot;
    QLabel* m_status;
};
