#include "pip-dock.h"
#include "panel-common.h"
#include "pip-preview.h"
#include "pip-settings.h"
#include "pip-widgets.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

// ── Value ranges and steps ───────────────────────────────────
// The SDK manual documents no ranges. These are ATEM Software Control's DVE
// ranges for 16:9 and MUST be verified on the real ATEM Mini (atem-cli pip).
struct FieldSpec {
    double min, max, step;
    int decimals;
};
constexpr FieldSpec kPositionX { -32.0, 32.0, 0.1, 2 };
constexpr FieldSpec kPositionY { -18.0, 18.0, 0.1, 2 };
constexpr FieldSpec kSize      {   0.0,  1.0, 0.01, 3 };
constexpr double kSizeScaleUpMax = 2.0;   // only when GetCanScaleUp() is true
constexpr FieldSpec kCropTopBottom { 0.0, 38.0, 0.1, 2 };
constexpr FieldSpec kCropLeftRight { 0.0, 52.0, 0.1, 2 };

// Continuous edits are coalesced and sent at most this often (~30 Hz).
constexpr int kFlushIntervalMs = 33;
// After an edit the panel shows its own value until the switcher echoes it
// back (or this long passes), so stale echoes don't make values jump back.
constexpr int kHoldMs = 400;
constexpr double kMatchTolerance = 0.005;

constexpr int kPresetThumbWidth = 192;
constexpr int kPresetThumbHeight = 108;

const char* const kPipExtraStyle = R"(
    #groupLabel {
        font-size: 10px;
        font-weight: 600;
        color: #cccccc;
    }
    #fieldLabel {
        font-size: 10px;
        color: #888888;
    }
    QDoubleSpinBox {
        padding: 3px 4px;
    }
    #warnBox {
        background: #2a2412;
        border: 1px solid #5a4a18;
        border-radius: 3px;
    }
    #warnText {
        color: #cca832;
    }
    #settingsTitle {
        font-size: 12px;
        font-weight: 600;
        color: #e0e0e0;
    }
    #cameraThumb {
        border: 1px solid #3c3c3c;
        border-radius: 2px;
    }
    QCheckBox {
        font-size: 11px;
    }
)";

// Field index ↔ AtemPipState. Indices follow AtemPipDock::Field; 0–3 are
// discrete (sent at once), 4–11 continuous (coalesced).
bool isContinuous(int f) { return f >= 4; }

double getField(const AtemPipState& s, int f) {
    switch (f) {
    case 0:  return static_cast<double>(s.programInput);
    case 1:  return static_cast<double>(s.pipInput);
    case 2:  return s.onAir ? 1.0 : 0.0;
    case 3:  return s.cropEnabled ? 1.0 : 0.0;
    case 4:  return s.positionX;
    case 5:  return s.positionY;
    case 6:  return s.sizeX;
    case 7:  return s.sizeY;
    case 8:  return s.cropTop;
    case 9:  return s.cropBottom;
    case 10: return s.cropLeft;
    case 11: return s.cropRight;
    }
    return 0.0;
}

void setField(AtemPipState& s, int f, double v) {
    switch (f) {
    case 0:  s.programInput = static_cast<BMDSwitcherInputId>(std::llround(v)); break;
    case 1:  s.pipInput = static_cast<BMDSwitcherInputId>(std::llround(v)); break;
    case 2:  s.onAir = v != 0.0; break;
    case 3:  s.cropEnabled = v != 0.0; break;
    case 4:  s.positionX = v; break;
    case 5:  s.positionY = v; break;
    case 6:  s.sizeX = v; break;
    case 7:  s.sizeY = v; break;
    case 8:  s.cropTop = v; break;
    case 9:  s.cropBottom = v; break;
    case 10: s.cropLeft = v; break;
    case 11: s.cropRight = v; break;
    }
}

AtemPipField toPipField(int f) {
    static const AtemPipField map[] = {
        AtemPipField::PositionX, AtemPipField::PositionY, AtemPipField::SizeX, AtemPipField::SizeY,
        AtemPipField::CropTop, AtemPipField::CropBottom, AtemPipField::CropLeft, AtemPipField::CropRight,
    };
    return map[f - 4];
}

QPixmap swatch(const QColor& color, const QSize& size) {
    QPixmap pix(size);
    pix.fill(color);
    return pix;
}

} // namespace

// ── AtemPipDock ──────────────────────────────────────────────

AtemPipDock::AtemPipDock(AtemSession* session, QWidget* parent)
    : QWidget(parent), m_session(session), m_settings(new PipSettings(this))
{
    applyPanelStyle(this, kPipExtraStyle);

    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &AtemPipDock::flushPending);

    m_dveRetryTimer.setInterval(100);
    connect(&m_dveRetryTimer, &QTimer::timeout, this, [this]() {
        bool done = m_session->isConnected() && m_session->pip().makeDVE();
        if (done || ++m_dveRetries >= 20) {
            m_dveRetryTimer.stop();
            m_makeDveBtn->setEnabled(true);
        }
    });

    // When a hold runs out without an echo, re-read the device.
    m_holdTimer.setSingleShot(true);
    connect(&m_holdTimer, &QTimer::timeout, this, &AtemPipDock::refreshFromDevice);

    buildUI();

    connect(m_session, &AtemSession::connectionChanged, this, &AtemPipDock::onConnectionChanged);
    connect(m_session, &AtemSession::pipChanged, this, &AtemPipDock::refreshFromDevice);
    // Thumbnails only change with the settings; render() runs on every echo.
    auto settingsChanged = [this]() {
        for (int i = 0; i < PipSettings::kPresetCount; ++i)
            m_presetButtons[i]->setPreset(m_settings->preset(i));
        render();
        renderSettingsPage();
    };
    connect(m_settings, &PipSettings::changed, this, settingsChanged);

    settingsChanged();
    onConnectionChanged(m_session->state());
}

void AtemPipDock::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_header = new PanelHeader("ATEM PIP", this);
    auto* reloadBtn = m_header->addToolButton("⟳", "Reload PiP settings from the ATEM");
    connect(reloadBtn, &QToolButton::clicked, this, [this]() {
        m_holds.clear();
        refreshFromDevice();
    });
    auto* settingsBtn = m_header->addToolButton("⚙", "PiP settings: camera names, colours and pictures");
    connect(settingsBtn, &QToolButton::clicked, this, [this]() {
        renderSettingsPage();
        m_pages->setCurrentWidget(m_settingsPage);
    });
    mainLayout->addWidget(m_header);

    m_pages = new QStackedWidget(this);
    mainLayout->addWidget(m_pages, 1);

    // Offline page
    m_offlinePage = new QWidget(m_pages);
    auto* offLayout = new QVBoxLayout(m_offlinePage);
    offLayout->setContentsMargins(14, 20, 14, 20);
    offLayout->setAlignment(Qt::AlignCenter);
    m_offlineText = new QLabel(m_offlinePage);
    m_offlineText->setObjectName("hintText");
    m_offlineText->setAlignment(Qt::AlignCenter);
    m_offlineText->setWordWrap(true);
    offLayout->addWidget(m_offlineText);
    // Same connection as the macro panel: reconnects the way it last
    // succeeded (USB auto-detect unless an IP was used). The label says which.
    m_connectBtn = new QPushButton(m_offlinePage);
    m_connectBtn->setObjectName("primaryBtn");
    connect(m_connectBtn, &QPushButton::clicked, this, [this]() { m_session->autoConnect(); });
    offLayout->addWidget(m_connectBtn);
    m_pages->addWidget(m_offlinePage);

    m_controlsPage = buildControlsPage();
    m_pages->addWidget(m_controlsPage);

    m_settingsPage = buildSettingsPage();
    m_pages->addWidget(m_settingsPage);
}

QWidget* AtemPipDock::buildControlsPage() {
    auto* scroll = new QScrollArea(m_pages);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_body = new QWidget();
    m_body->setObjectName("scrollBody");
    m_body->installEventFilter(this);   // layoutStage() on resize
    auto* layout = new QVBoxLayout(m_body);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    // ── Camera rows: main, then PiP ──
    for (int row = 0; row < 2; ++row) {
        auto* rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(3);
        for (int cam = 0; cam < PipSettings::kCameraCount; ++cam) {
            BMDSwitcherInputId input = PipSettings::cameraInput(cam);
            auto* btn = new CamButton(m_settings, input, m_body);
            rowLayout->addWidget(btn);
            if (row == 0) {
                m_mainButtons.push_back(btn);
                connect(btn, &QAbstractButton::clicked, this, [this, input]() { onMainClicked(input); });
            } else {
                m_pipButtons.push_back(btn);
                connect(btn, &QAbstractButton::clicked, this, [this, input]() { onPipClicked(input); });
            }
        }
        layout->addLayout(rowLayout);
    }

    // ── Key-type warning ──
    m_dveWarning = new QFrame(m_body);
    m_dveWarning->setObjectName("warnBox");
    auto* warnLayout = new QVBoxLayout(m_dveWarning);
    warnLayout->setContentsMargins(6, 5, 6, 5);
    warnLayout->setSpacing(5);
    m_dveWarningText = new QLabel(m_dveWarning);
    m_dveWarningText->setObjectName("warnText");
    m_dveWarningText->setWordWrap(true);
    warnLayout->addWidget(m_dveWarningText);
    m_makeDveBtn = new QPushButton("Set key type to DVE", m_dveWarning);
    connect(m_makeDveBtn, &QPushButton::clicked, this, [this]() {
        if (m_session->pip().makeDVE()) return;
        // The DVE was held by the transition and is being freed: retry until
        // the switcher confirms (about two tenths of a second).
        m_dveRetries = 0;
        m_makeDveBtn->setEnabled(false);
        m_dveRetryTimer.start();
    });
    warnLayout->addWidget(m_makeDveBtn);
    m_dveWarning->setVisible(false);
    layout->addWidget(m_dveWarning);

    // ── Stage: presets | preview + fields + save ──
    auto* stage = new QHBoxLayout();
    stage->setSpacing(6);

    m_presetColumn = new QWidget(m_body);
    auto* presetLayout = new QVBoxLayout(m_presetColumn);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    presetLayout->setSpacing(3);
    for (int i = 0; i < PipSettings::kPresetCount; ++i) {
        auto* btn = new PresetButton(i + 1, m_presetColumn);
        connect(btn, &QAbstractButton::clicked, this, [this, i]() { recallPreset(i); });
        presetLayout->addWidget(btn);
        m_presetButtons.push_back(btn);
    }
    stage->addWidget(m_presetColumn, 0, Qt::AlignTop);

    // The right column fills the preset column's height and spreads the
    // spare space evenly, so "Save current to" lines up with the last preset.
    auto* right = new QVBoxLayout();
    right->setSpacing(6);

    m_preview = new PipPreview(m_settings, m_body);
    connect(m_preview, &PipPreview::moved, this, [this](double x, double y) {
        edit(Field::PositionX, x);
        edit(Field::PositionY, y);
    });
    connect(m_preview, &PipPreview::resized, this, [this](double size, double x, double y) {
        editSize(size);
        edit(Field::PositionX, x);
        edit(Field::PositionY, y);
    });
    connect(m_preview, &PipPreview::nudged, this, [this](double dx, double dy) {
        edit(Field::PositionX, m_view.positionX + dx);
        edit(Field::PositionY, m_view.positionY + dy);
    });
    right->addWidget(m_preview);
    right->addStretch(1);

    // Field grids: labels right-aligned over their boxes, the group name
    // ("Position", "Crop") on the same line, left-aligned.
    auto addGroup = [&](const QString& groupName,
                        const std::vector<std::pair<Field, QString>>& fields, bool widerFirst) {
        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(5);
        grid->setVerticalSpacing(1);
        auto* group = new QLabel(groupName, m_body);
        group->setObjectName("groupLabel");
        grid->addWidget(group, 0, 0, Qt::AlignLeft | Qt::AlignBottom);
        for (int col = 0; col < static_cast<int>(fields.size()); ++col) {
            Field f = fields[col].first;
            auto* label = new QLabel(fields[col].second, m_body);
            label->setObjectName("fieldLabel");
            grid->addWidget(label, 0, col, Qt::AlignRight | Qt::AlignBottom);

            const FieldSpec& spec =
                f == Field::PositionX ? kPositionX :
                f == Field::PositionY ? kPositionY :
                f == Field::SizeX ? kSize :
                (f == Field::CropTop || f == Field::CropBottom) ? kCropTopBottom : kCropLeftRight;
            auto* box = new PipSpinBox(m_body);
            box->setDecimals(spec.decimals);
            box->setRange(spec.min, spec.max);
            box->setSingleStep(spec.step);
            box->setAccessibleName(groupName + " " + fields[col].second);
            label->setBuddy(box);
            grid->addWidget(box, 1, col);
            grid->setColumnStretch(col, widerFirst && col == 0 ? 6 : 5);
            m_fields[f] = box;

            connect(box, &QDoubleSpinBox::valueChanged, this, [this, f](double v) {
                if (f == Field::SizeX) editSize(v);
                else if (f >= Field::CropTop) editCrop(f, v);
                else edit(f, v);
            });
        }
        right->addLayout(grid);
    };
    addGroup("Position", { { Field::PositionX, "X" }, { Field::PositionY, "Y" }, { Field::SizeX, "Size" } }, false);
    right->addStretch(1);
    // The first crop column is a little wider so "Crop" never touches "Top".
    addGroup("Crop", { { Field::CropTop, "Top" }, { Field::CropBottom, "Bottom" },
                       { Field::CropLeft, "Left" }, { Field::CropRight, "Right" } }, true);
    right->addStretch(1);

    auto* saveRow = new QHBoxLayout();
    saveRow->setSpacing(5);
    auto* saveLabel = new QLabel("Save current to", m_body);
    saveRow->addWidget(saveLabel);
    m_saveSlot = new QComboBox(m_body);
    for (int i = 0; i < PipSettings::kPresetCount; ++i) m_saveSlot->addItem(QString("Button %1").arg(i + 1));
    m_saveSlot->setMinimumWidth(40);
    saveLabel->setBuddy(m_saveSlot);
    saveRow->addWidget(m_saveSlot, 1);
    auto* saveBtn = new QPushButton("Save", m_body);
    connect(saveBtn, &QPushButton::clicked, this, [this]() { savePreset(m_saveSlot->currentIndex()); });
    saveRow->addWidget(saveBtn);
    right->addLayout(saveRow);

    stage->addLayout(right, 1);
    layout->addLayout(stage);
    layout->addStretch(1);

    scroll->setWidget(m_body);
    return scroll;
}

QWidget* AtemPipDock::buildSettingsPage() {
    auto* page = new QScrollArea(m_pages);
    page->setWidgetResizable(true);
    page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget();
    body->setObjectName("scrollBody");
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel("PiP settings", body);
    title->setObjectName("settingsTitle");
    head->addWidget(title, 1);
    auto* doneBtn = new QPushButton("Done", body);
    connect(doneBtn, &QPushButton::clicked, this, &AtemPipDock::showMainPage);
    head->addWidget(doneBtn);
    layout->addLayout(head);

    auto* hint = new QLabel("A name, colour and picture for each camera. The picture is shown on the "
                            "camera buttons, in the preview and in the thumbnails of saved buttons "
                            "(PNG, any size — shown as 16:9). The name and colour are used wherever a "
                            "camera has no picture; the name also in tooltips.", body);
    hint->setObjectName("hintText");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    for (int cam = 0; cam < PipSettings::kCameraCount; ++cam) {
        CameraRow& row = m_cameraRows[cam];
        auto* rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(6);

        row.thumb = new QLabel(body);
        row.thumb->setObjectName("cameraThumb");
        row.thumb->setFixedSize(72, 40);
        rowLayout->addWidget(row.thumb, 0, Qt::AlignTop);

        auto* fields = new QVBoxLayout();
        fields->setSpacing(4);
        row.name = new QLineEdit(body);
        row.name->setMaxLength(20);
        row.name->setAccessibleName(QString("Name for camera %1").arg(cam + 1));
        connect(row.name, &QLineEdit::textEdited, this, [this, cam](const QString& text) {
            m_settings->setCustomName(cam, text);
        });
        fields->addWidget(row.name);

        auto* actions = new QHBoxLayout();
        actions->setSpacing(4);
        row.color = new QToolButton(body);
        row.color->setToolTip("Colour (used where there is no picture)");
        row.color->setAccessibleName(QString("Colour for camera %1").arg(cam + 1));
        row.color->setIconSize(QSize(16, 14));
        connect(row.color, &QToolButton::clicked, this, [this, cam]() { chooseColor(cam); });
        actions->addWidget(row.color);
        auto* chooseBtn = new QPushButton("Choose PNG…", body);
        connect(chooseBtn, &QPushButton::clicked, this, [this, cam]() { choosePicture(cam); });
        actions->addWidget(chooseBtn);
        auto* clearBtn = new QPushButton("Clear picture", body);
        connect(clearBtn, &QPushButton::clicked, this, [this, cam]() { m_settings->setPicturePath(cam, QString()); });
        actions->addWidget(clearBtn);
        actions->addStretch(1);
        fields->addLayout(actions);

        rowLayout->addLayout(fields, 1);
        layout->addLayout(rowLayout);
    }

    m_showNames = new QCheckBox("Show names on camera buttons", body);
    connect(m_showNames, &QCheckBox::toggled, m_settings, &PipSettings::setShowNames);
    layout->addWidget(m_showNames);
    layout->addStretch(1);

    page->setWidget(body);
    return page;
}

void AtemPipDock::renderSettingsPage() {
    for (int cam = 0; cam < PipSettings::kCameraCount; ++cam) {
        CameraRow& row = m_cameraRows[cam];
        BMDSwitcherInputId input = PipSettings::cameraInput(cam);
        QPixmap pic = m_settings->picture(input);
        row.thumb->setPixmap(pic.isNull()
            ? swatch(m_settings->color(input), row.thumb->size())
            : pic.scaled(row.thumb->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        row.name->setPlaceholderText(m_settings->defaultName(cam));
        if (!row.name->hasFocus()) row.name->setText(m_settings->customName(cam));
        row.color->setIcon(QIcon(swatch(m_settings->color(input), QSize(16, 14))));
    }
    QSignalBlocker block(m_showNames);
    m_showNames->setChecked(m_settings->showNames());
}

void AtemPipDock::chooseColor(int camera) {
    BMDSwitcherInputId input = PipSettings::cameraInput(camera);
    QColor color = QColorDialog::getColor(m_settings->color(input), this,
                                          QString("Colour for %1").arg(m_settings->name(input)));
    if (color.isValid()) m_settings->setColor(camera, color);
}

void AtemPipDock::choosePicture(int camera) {
    BMDSwitcherInputId input = PipSettings::cameraInput(camera);
    QString start = m_settings->picturePath(camera);
    QString path = QFileDialog::getOpenFileName(this, QString("Picture for %1").arg(m_settings->name(input)),
                                                start.isEmpty() ? QString() : QFileInfo(start).absolutePath(),
                                                "PNG images (*.png)");
    if (path.isEmpty()) return;
    if (!m_settings->setPicturePath(camera, path))
        QMessageBox::warning(this, "ATEM PiP", QString("Could not read %1 as an image.").arg(path));
}

void AtemPipDock::showMainPage() {
    // refreshFromDevice() switches to the offline page if needed.
    m_pages->setCurrentWidget(m_controlsPage);
    refreshFromDevice();
}

bool AtemPipDock::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_body && event->type() == QEvent::Resize) layoutStage();
    return QWidget::eventFilter(watched, event);
}

void AtemPipDock::layoutStage() {
    // Everything keeps a 16:9 shape and follows the dock width. The preset
    // column widens with the dock because the preview's height grows with
    // it; that keeps the save row level with the last preset button.
    int w = m_body->width() - 16;   // body margins
    if (w <= 0) return;

    int camHeight = static_cast<int>(std::lround((w - 3 * 3) / 4.0 * 9 / 16));
    for (auto* b : m_mainButtons) b->setFixedHeight(camHeight);
    for (auto* b : m_pipButtons) b->setFixedHeight(camHeight);

    int presetWidth = std::max(64, static_cast<int>(std::lround(0.125 * w + 27)));
    m_presetColumn->setFixedWidth(presetWidth);
    int presetHeight = static_cast<int>(std::lround(presetWidth * 9.0 / 16));
    for (auto* b : m_presetButtons) b->setFixedHeight(presetHeight);

    m_preview->setFixedHeight(static_cast<int>(std::lround((w - presetWidth - 6) * 9.0 / 16)));
}

// ── Connection / device state ────────────────────────────────

void AtemPipDock::onConnectionChanged(AtemState state) {
    updateStatus();
    if (state == AtemState::Connecting) return;
    m_pending.clear();
    m_holds.clear();
    m_flushTimer.stop();
    m_viewLoaded = false;
    refreshFromDevice();
}

void AtemPipDock::updateStatus() {
    AtemState state = m_session->isBusy() ? AtemState::Connecting : m_session->state();
    QString detail;
    if (state == AtemState::Connected)
        detail = QString::fromStdString(m_session->atem().modelName());
    else if (state == AtemState::Connecting)
        detail = "connecting…";
    m_header->setConnectionState(state, detail);
}

void AtemPipDock::refreshFromDevice() {
    if (m_session->isBusy()) return;
    bool onSettings = m_pages->currentWidget() == m_settingsPage;

    if (!m_session->isConnected()) {
        QString error = QString::fromStdString(m_session->atem().lastError());
        m_offlineText->setText("ATEM not connected." + (error.isEmpty() ? QString() : "\n" + error) +
                               "\nFor a different IP address use the ATEM Macros panel (⚙).");
        m_connectBtn->setText(m_session->lastWasIP()
            ? QString("Connect to %1").arg(m_session->lastIP())
            : QString("Connect via USB (auto-detect)"));
        m_connectBtn->setVisible(true);
        if (!onSettings) m_pages->setCurrentWidget(m_offlinePage);
        return;
    }

    AtemPip& pip = m_session->pip();
    AtemPipState s = pip.state();
    if (!s.available) {
        m_offlineText->setText("This switcher has no upstream keyer, so PiP is not available.");
        m_connectBtn->setVisible(false);
        if (!onSettings) m_pages->setCurrentWidget(m_offlinePage);
        return;
    }
    if (!onSettings) m_pages->setCurrentWidget(m_controlsPage);
    m_settings->setDeviceInputs(pip.inputs());

    if (!m_viewLoaded) {
        m_view = s;
        m_viewLoaded = true;
    } else {
        // Merge the device state into the view field by field. Keep the
        // local value while it is unsent, awaiting its echo, being typed
        // or being dragged.
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < kFieldCount; ++i) {
            Field f = static_cast<Field>(i);
            double device = getField(s, i);

            if (isContinuous(i) && m_pending.count(toPipField(i))) continue;

            auto hold = m_holds.find(f);
            if (hold != m_holds.end()) {
                if (std::abs(device - hold->second.value) < 1e-4 || now > hold->second.until)
                    m_holds.erase(hold);
                else
                    continue;
            }

            auto box = m_fields.find(f == Field::SizeY ? Field::SizeX : f);
            if (box != m_fields.end() && box->second->hasFocus()) continue;
            if (m_preview->isDragging() && i >= 4 && i <= 7) continue;

            setField(m_view, i, device);
        }
        m_view.available = s.available;
        m_view.canBeDVE = s.canBeDVE;
        m_view.dveUsedByTransition = s.dveUsedByTransition;
        m_view.isDVE = s.isDVE;
        m_view.canScaleUp = s.canScaleUp;
        m_view.borderEnabled = s.borderEnabled;
    }
    render();
}

void AtemPipDock::render() {
    m_preview->setState(m_view);

    for (auto* b : m_mainButtons) {
        b->setLit(m_view.programInput == b->input());
        b->setToolTip("Main: " + m_settings->name(b->input()));
        b->update();
    }
    for (auto* b : m_pipButtons) {
        b->setLit(m_view.onAir && m_view.pipInput == b->input());
        b->setToolTip("PiP: " + m_settings->name(b->input()) +
                      (b->isLit() ? " (click to take the PiP off air)" : ""));
        b->update();
    }

    m_dveWarning->setVisible(m_viewLoaded && !m_view.isDVE);
    // The button stays available even when canBeDVE is false: that is the
    // case where a DVE transition holds the DVE, and makeDVE frees it.
    m_dveWarningText->setText(m_view.dveUsedByTransition
        ? "Upstream key 1 is not a DVE key: the DVE is in use by the DVE transition. "
          "Setting the key to DVE switches the next transition to Mix."
        : "Upstream key 1 is not a DVE key, so position and size have no effect.");

    m_fields[Field::SizeX]->setMaximum(m_view.canScaleUp ? kSizeScaleUpMax : kSize.max);
    for (auto& [f, box] : m_fields) {
        if (box->hasFocus()) continue;   // don't overwrite what the operator is typing
        QSignalBlocker block(box);
        box->setValue(getField(m_view, static_cast<int>(f)));
    }

    for (int i = 0; i < PipSettings::kPresetCount; ++i) {
        const PipPreset& p = m_settings->preset(i);
        m_presetButtons[i]->setLit(p.valid && presetMatches(p));
        m_presetButtons[i]->setToolTip(p.valid
            ? QString("Button %1: main %2 · PiP %3 · X %4 Y %5 · size %6")
                  .arg(i + 1).arg(m_settings->name(p.programInput))
                  .arg(p.onAir ? m_settings->name(p.pipInput) : QString("off"))
                  .arg(p.positionX, 0, 'f', 1).arg(p.positionY, 0, 'f', 1).arg(p.sizeX, 0, 'f', 2)
            : QString("Button %1 — empty. Use \"Save current to\".").arg(i + 1));
    }
}

// ── Operator edits ───────────────────────────────────────────

void AtemPipDock::edit(Field field, double value) {
    if (m_session->isBusy() || !m_session->isConnected() || !m_viewLoaded) return;
    int i = static_cast<int>(field);

    setField(m_view, i, value);
    m_holds[field] = { getField(m_view, i), QDateTime::currentMSecsSinceEpoch() + kHoldMs };
    m_holdTimer.start(kHoldMs + 50);

    AtemPip& pip = m_session->pip();
    if (isContinuous(i)) {
        m_pending[toPipField(i)] = value;
        if (!m_flushTimer.isActive()) m_flushTimer.start();
    } else if (field == Field::Program) {
        pip.setProgramInput(m_view.programInput);
    } else if (field == Field::Pip) {
        pip.setPipInput(m_view.pipInput);
    } else if (field == Field::OnAir) {
        pip.setOnAir(m_view.onAir);
    } else if (field == Field::CropEnabled) {
        pip.setCropEnabled(m_view.cropEnabled);
    }
    render();
}

void AtemPipDock::editSize(double size) {
    // The aspect ratio is always kept: width and height move together.
    edit(Field::SizeX, size);
    edit(Field::SizeY, size);
}

void AtemPipDock::editCrop(Field field, double value) {
    edit(field, value);
    syncMask();
}

void AtemPipDock::syncMask() {
    // No crop checkbox: the mask is on whenever any edge is non-zero.
    bool want = m_view.cropTop > 0 || m_view.cropBottom > 0 || m_view.cropLeft > 0 || m_view.cropRight > 0;
    if (want != m_view.cropEnabled) edit(Field::CropEnabled, want ? 1.0 : 0.0);
}

void AtemPipDock::onMainClicked(BMDSwitcherInputId input) {
    // One main camera is always lit; pressing the lit one changes nothing.
    if (m_view.programInput != input) edit(Field::Program, static_cast<double>(input));
}

void AtemPipDock::onPipClicked(BMDSwitcherInputId input) {
    if (m_view.onAir && m_view.pipInput == input) {
        edit(Field::OnAir, 0.0);   // lit PiP camera: take the PiP off air
        return;
    }
    if (m_view.pipInput != input) edit(Field::Pip, static_cast<double>(input));
    if (!m_view.onAir) edit(Field::OnAir, 1.0);
}

void AtemPipDock::flushPending() {
    if (m_session->isBusy() || !m_session->isConnected()) {
        m_pending.clear();
        return;
    }
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (const auto& [field, value] : pending) m_session->pip().setValue(field, value);
}

// ── Presets ──────────────────────────────────────────────────

QImage AtemPipDock::snapshotProgram() const {
    QImage img(kPresetThumbWidth, kPresetThumbHeight, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter p(&img);
    PipPreview::paintProgram(p, QRectF(img.rect()), m_view, *m_settings, PipPreview::Style::Thumbnail);
    return img;
}

void AtemPipDock::savePreset(int index) {
    if (!m_viewLoaded) return;
    PipPreset p;
    p.valid = true;
    p.programInput = m_view.programInput;
    p.pipInput = m_view.pipInput;
    p.onAir = m_view.onAir;
    p.positionX = m_view.positionX;
    p.positionY = m_view.positionY;
    p.sizeX = m_view.sizeX;
    p.sizeY = m_view.sizeY;
    p.cropTop = m_view.cropTop;
    p.cropBottom = m_view.cropBottom;
    p.cropLeft = m_view.cropLeft;
    p.cropRight = m_view.cropRight;
    p.thumbnail = snapshotProgram();
    m_settings->setPreset(index, p);
}

bool AtemPipDock::presetMatches(const PipPreset& p) const {
    auto same = [](double a, double b) { return std::abs(a - b) < kMatchTolerance; };
    return m_view.programInput == p.programInput && m_view.pipInput == p.pipInput &&
           m_view.onAir == p.onAir &&
           same(m_view.positionX, p.positionX) && same(m_view.positionY, p.positionY) &&
           same(m_view.sizeX, p.sizeX) && same(m_view.sizeY, p.sizeY) &&
           same(m_view.cropTop, p.cropTop) && same(m_view.cropBottom, p.cropBottom) &&
           same(m_view.cropLeft, p.cropLeft) && same(m_view.cropRight, p.cropRight);
}

void AtemPipDock::recallPreset(int index) {
    const PipPreset& p = m_settings->preset(index);
    if (!p.valid || !m_viewLoaded || !m_session->isConnected()) return;

    // Send only what differs. Sources and geometry first, the on-air change
    // last, so the PiP appears already in its new place.
    if (m_view.pipInput != p.pipInput) edit(Field::Pip, static_cast<double>(p.pipInput));
    if (m_view.programInput != p.programInput) edit(Field::Program, static_cast<double>(p.programInput));
    const std::pair<Field, double> values[] = {
        { Field::PositionX, p.positionX }, { Field::PositionY, p.positionY },
        { Field::SizeX, p.sizeX }, { Field::SizeY, p.sizeY },
        { Field::CropTop, p.cropTop }, { Field::CropBottom, p.cropBottom },
        { Field::CropLeft, p.cropLeft }, { Field::CropRight, p.cropRight },
    };
    for (const auto& [f, v] : values) {
        if (getField(m_view, static_cast<int>(f)) != v) edit(f, v);
    }
    syncMask();
    flushPending();
    if (m_view.onAir != p.onAir) edit(Field::OnAir, p.onAir ? 1.0 : 0.0);
}
