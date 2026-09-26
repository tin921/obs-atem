#pragma once

#include <QTimer>
#include <QWidget>
#include <array>
#include <map>
#include <vector>

#include "atem-session.h"

class CamButton;
class PanelHeader;
class PipPreview;
class PipSettings;
class PipSpinBox;
class PresetButton;
struct PipPreset;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

// ── PiP panel ────────────────────────────────────────────────
//
// Layout (mockups/index.html is the reference):
//   camera row 1  — main (program) camera, one lit
//   camera row 2  — PiP camera; press the lit one to take the PiP off air
//   presets 1–7   | program preview (drag = move, corner = resize)
//                 | Position X / Y / Size
//                 | Crop Top / Bottom / Left / Right (mask on when any > 0)
//                 | Save current to [Button n] [Save]
//   ⚙ page        — name, colour and picture per camera
//
// The panel keeps a local view of the PiP state so it reacts instantly;
// edits are sent to the switcher (continuous values at ~30 Hz) and the
// device's echoes are merged back without fighting the operator.

class AtemPipDock : public QWidget {
    Q_OBJECT
public:
    explicit AtemPipDock(AtemSession* session, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onConnectionChanged(AtemState state);
    void refreshFromDevice();
    void flushPending();

private:
    // Everything the operator can change, as one list so edits, holds and
    // device merging work the same way for all of them.
    enum class Field {
        Program, Pip, OnAir, CropEnabled,
        PositionX, PositionY, SizeX, SizeY,
        CropTop, CropBottom, CropLeft, CropRight,
    };
    static constexpr int kFieldCount = 12;

    struct Hold {
        double value;
        qint64 until;   // ms since epoch
    };

    void buildUI();
    QWidget* buildControlsPage();
    QWidget* buildSettingsPage();
    void layoutStage();
    void render();
    void renderSettingsPage();
    void updateStatus();
    void showMainPage();

    void edit(Field field, double value);
    void editSize(double size);
    void editCrop(Field field, double value);
    void syncMask();
    void onMainClicked(BMDSwitcherInputId input);
    void onPipClicked(BMDSwitcherInputId input);

    void savePreset(int index);
    void recallPreset(int index);
    bool presetMatches(const PipPreset& preset) const;
    QImage snapshotProgram() const;

    void chooseColor(int camera);
    void choosePicture(int camera);

    AtemSession* m_session;
    PipSettings* m_settings;

    PanelHeader*    m_header = nullptr;
    QStackedWidget* m_pages = nullptr;
    QWidget*        m_offlinePage = nullptr;
    QLabel*         m_offlineText = nullptr;
    QPushButton*    m_connectBtn = nullptr;
    QWidget*        m_controlsPage = nullptr;
    QWidget*        m_settingsPage = nullptr;

    QWidget* m_body = nullptr;
    QWidget* m_presetColumn = nullptr;
    QWidget* m_dveWarning = nullptr;
    QPushButton* m_makeDveBtn = nullptr;
    std::vector<CamButton*> m_mainButtons;
    std::vector<CamButton*> m_pipButtons;
    std::vector<PresetButton*> m_presetButtons;
    PipPreview* m_preview = nullptr;
    std::map<Field, PipSpinBox*> m_fields;
    QComboBox* m_saveSlot = nullptr;

    struct CameraRow {
        QLabel* thumb = nullptr;
        QLineEdit* name = nullptr;
        QToolButton* color = nullptr;
    };
    std::array<CameraRow, 4> m_cameraRows;
    QCheckBox* m_showNames = nullptr;

    AtemPipState m_view;           // what the panel shows
    bool m_viewLoaded = false;     // m_view has been filled from the device
    std::map<AtemPipField, double> m_pending;   // continuous edits not yet sent
    std::map<Field, Hold> m_holds;              // sent edits awaiting the echo
    QTimer m_flushTimer;
    QTimer m_dveRetryTimer;     // retries makeDVE while the DVE is being freed
    int m_dveRetries = 0;
    QLabel* m_dveWarningText = nullptr;
    QTimer m_holdTimer;
};
