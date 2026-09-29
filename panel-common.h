#pragma once

#include <QFrame>
#include <QLabel>
#include <QString>
#include <QTimer>
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

// One line under a panel's header for an action the switcher refused (or
// that couldn't be sent). Hidden until show(); hides itself after a while.
class ErrorLine : public QLabel {
public:
    explicit ErrorLine(QWidget* parent = nullptr);
    void show(const QString& message);

private:
    QTimer* m_timer;
};
