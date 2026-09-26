#pragma once

#include <QWidget>
#include <QTimer>
#include <map>
#include <vector>

#include "atem-session.h"

class PanelHeader;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

// ── Slider + number box pair ─────────────────────────────────
//
// The slider covers the useful on-screen range for quick moves; the spin box
// accepts precise values (and a wider range) typed by the operator.

class PipValueControl : public QWidget {
    Q_OBJECT
public:
    PipValueControl(const QString& label, int decimals, QWidget* parent = nullptr);

    void setLabel(const QString& label);
    void setLimits(double sliderMin, double sliderMax, double spinMin, double spinMax);
    // From the device: updates both widgets without emitting valueEdited.
    void setValue(double value);
    double value() const;
    // True while the operator is dragging or typing — device echoes must not
    // overwrite the control mid-edit.
    bool isEditing() const;

signals:
    void valueEdited(double value);

private:
    int toTicks(double value) const;

    QLabel* m_label;
    QSlider* m_slider;
    QDoubleSpinBox* m_spin;
    double m_scale;
    bool m_silent = false;
};

// ── PiP panel ────────────────────────────────────────────────
//
// Skeleton: functional, deliberately plain. The final layout is chosen from
// the HTML mockups in mockups/ and then applied here.

class AtemPipDock : public QWidget {
    Q_OBJECT
public:
    explicit AtemPipDock(AtemSession* session, QWidget* parent = nullptr);

private slots:
    void onConnectionChanged(AtemState state);
    void refreshFromDevice();
    void flushPending();

private:
    void buildUI();
    QWidget* buildControls();
    void rebuildInputLists(const std::vector<AtemInputInfo>& inputs);
    void queueValue(AtemPipField field, double value);
    void updateStatus();

    AtemSession* m_session;

    PanelHeader*    m_header = nullptr;
    QStackedWidget* m_pages = nullptr;
    QWidget*        m_offlinePage = nullptr;
    QLabel*         m_offlineText = nullptr;
    QWidget*        m_controlsPage = nullptr;

    QComboBox*   m_mainInput = nullptr;
    QComboBox*   m_pipInput = nullptr;
    QPushButton* m_onAirBtn = nullptr;
    QLabel*      m_notDveText = nullptr;
    QPushButton* m_makeDveBtn = nullptr;

    PipValueControl* m_posX = nullptr;
    PipValueControl* m_posY = nullptr;
    PipValueControl* m_sizeX = nullptr;
    PipValueControl* m_sizeY = nullptr;
    QCheckBox*       m_lockAspect = nullptr;
    QCheckBox*       m_cropEnabled = nullptr;
    PipValueControl* m_cropTop = nullptr;
    PipValueControl* m_cropBottom = nullptr;
    PipValueControl* m_cropLeft = nullptr;
    PipValueControl* m_cropRight = nullptr;

    std::vector<AtemInputInfo> m_inputs;
    bool m_syncAspectLock = true;   // set the lock from the device on next refresh
    std::map<AtemPipField, double> m_pending;
    QTimer m_flushTimer;
};
