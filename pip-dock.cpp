#include "pip-dock.h"
#include "panel-common.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <cmath>

namespace {

// ── Value ranges ─────────────────────────────────────────────
// The SDK manual documents no numeric ranges. These are ATEM Software
// Control's DVE ranges for a 16:9 format (frame edge at X ±16, Y ±9) and
// MUST be verified on the real ATEM Mini. The sliders cover the on-screen
// range; the number boxes allow the wider range the switcher accepts.
struct Limits {
    double sliderMin, sliderMax, spinMin, spinMax;
};
constexpr Limits kPositionX  { -16.0, 16.0, -32.0, 32.0 };
constexpr Limits kPositionY  {  -9.0,  9.0, -18.0, 18.0 };
constexpr Limits kSize       {   0.0,  1.0,   0.0,  1.0 };
constexpr double kSizeScaleUpMax = 2.0;   // only when GetCanScaleUp() is true
constexpr Limits kCropTopBot {   0.0, 18.0,   0.0, 38.0 };
constexpr Limits kCropLeftRt {   0.0, 32.0,   0.0, 52.0 };

// Slider edits are coalesced and sent at most this often (~30 Hz).
constexpr int kFlushIntervalMs = 33;

const char* const kPipExtraStyle = R"(
    QGroupBox {
        border: 1px solid #3c3c3c;
        border-radius: 4px;
        margin-top: 14px;
        padding: 6px 6px 4px 6px;
        font-size: 10px;
        font-weight: 600;
        color: #888888;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 8px;
        padding: 0 3px;
    }
    QPushButton#onAirBtn {
        font-weight: bold;
        padding: 6px;
    }
    QPushButton#onAirBtn:checked {
        background: #c83232;
        border-color: #c83232;
        color: white;
    }
    QSlider::groove:horizontal {
        height: 4px;
        background: #3c3c3c;
        border-radius: 2px;
    }
    QSlider::handle:horizontal {
        width: 12px;
        margin: -5px 0;
        border-radius: 6px;
        background: #cccccc;
    }
    QSlider::handle:horizontal:hover {
        background: #ffffff;
    }
    #warnText {
        color: #cca832;
    }
)";

QString inputLabel(const AtemInputInfo& in) {
    if (!in.longName.empty()) return QString::fromStdString(in.longName);
    if (!in.shortName.empty()) return QString::fromStdString(in.shortName);
    return QString("Input %1").arg(in.id);
}

bool sameInputs(const std::vector<AtemInputInfo>& a, const std::vector<AtemInputInfo>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || a[i].longName != b[i].longName ||
            a[i].shortName != b[i].shortName || a[i].canBeProgram != b[i].canBeProgram ||
            a[i].canBePip != b[i].canBePip)
            return false;
    }
    return true;
}

void selectInput(QComboBox* combo, BMDSwitcherInputId id) {
    if (combo->view()->isVisible()) return; // operator has the list open
    QSignalBlocker block(combo);
    combo->setCurrentIndex(combo->findData(QVariant::fromValue<qlonglong>(id)));
}

} // namespace

// ── PipValueControl ──────────────────────────────────────────

PipValueControl::PipValueControl(const QString& label, int decimals, QWidget* parent)
    : QWidget(parent), m_scale(std::pow(10.0, decimals))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_label = new QLabel(label, this);
    m_label->setFixedWidth(46);
    layout->addWidget(m_label);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_slider, 1);

    m_spin = new QDoubleSpinBox(this);
    m_spin->setDecimals(decimals);
    m_spin->setSingleStep(1.0 / m_scale * 10.0);
    m_spin->setKeyboardTracking(false); // emit on Enter / focus-out, not per keystroke
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setFixedWidth(64);
    layout->addWidget(m_spin);

    connect(m_slider, &QSlider::valueChanged, this, [this](int ticks) {
        if (m_silent) return;
        double v = ticks / m_scale;
        QSignalBlocker block(m_spin);
        m_spin->setValue(v);
        emit valueEdited(v);
    });
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_silent) return;
        QSignalBlocker block(m_slider);
        m_slider->setValue(toTicks(v));
        emit valueEdited(v);
    });
}

void PipValueControl::setLabel(const QString& label) {
    m_label->setText(label);
}

void PipValueControl::setLimits(double sliderMin, double sliderMax, double spinMin, double spinMax) {
    m_silent = true;
    m_slider->setRange(toTicks(sliderMin), toTicks(sliderMax));
    m_slider->setPageStep(toTicks((sliderMax - sliderMin) / 20.0));
    m_spin->setRange(spinMin, spinMax);
    m_silent = false;
}

void PipValueControl::setValue(double value) {
    m_silent = true;
    m_spin->setValue(value);
    m_slider->setValue(toTicks(value)); // clamps to the slider range
    m_silent = false;
}

double PipValueControl::value() const {
    return m_spin->value();
}

bool PipValueControl::isEditing() const {
    return m_slider->isSliderDown() || m_spin->hasFocus();
}

int PipValueControl::toTicks(double value) const {
    return static_cast<int>(std::lround(value * m_scale));
}

// ── AtemPipDock ──────────────────────────────────────────────

AtemPipDock::AtemPipDock(AtemSession* session, QWidget* parent)
    : QWidget(parent), m_session(session)
{
    applyPanelStyle(this, kPipExtraStyle);

    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &AtemPipDock::flushPending);

    buildUI();

    connect(m_session, &AtemSession::connectionChanged, this, &AtemPipDock::onConnectionChanged);
    connect(m_session, &AtemSession::pipChanged, this, &AtemPipDock::refreshFromDevice);

    onConnectionChanged(m_session->state());
}

void AtemPipDock::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_header = new PanelHeader("ATEM PIP", this);
    auto* reloadBtn = m_header->addToolButton("⟳", "Reload PiP settings from the ATEM");
    connect(reloadBtn, &QToolButton::clicked, this, &AtemPipDock::refreshFromDevice);
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
    auto* connectBtn = new QPushButton("Connect", m_offlinePage);
    connectBtn->setObjectName("primaryBtn");
    connectBtn->setToolTip("Connect the same way as last time (USB or IP)");
    connect(connectBtn, &QPushButton::clicked, this, [this]() { m_session->autoConnect(); });
    offLayout->addWidget(connectBtn);
    m_pages->addWidget(m_offlinePage);

    // Controls page
    auto* scroll = new QScrollArea(m_pages);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_controlsPage = buildControls();
    scroll->setWidget(m_controlsPage);
    m_pages->addWidget(scroll);
}

QWidget* AtemPipDock::buildControls() {
    auto* body = new QWidget();
    body->setObjectName("scrollBody");
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(4);

    // ── Sources ──
    auto* sources = new QGroupBox("SOURCES", body);
    auto* srcGrid = new QGridLayout(sources);
    srcGrid->setHorizontalSpacing(6);
    srcGrid->addWidget(new QLabel("Main", sources), 0, 0);
    m_mainInput = new QComboBox(sources);
    m_mainInput->setToolTip("Program input (hard cut)");
    srcGrid->addWidget(m_mainInput, 0, 1);
    srcGrid->addWidget(new QLabel("PiP", sources), 1, 0);
    m_pipInput = new QComboBox(sources);
    m_pipInput->setToolTip("Upstream key fill source shown in the PiP box");
    srcGrid->addWidget(m_pipInput, 1, 1);
    srcGrid->setColumnStretch(1, 1);

    m_onAirBtn = new QPushButton("PiP OFF", sources);
    m_onAirBtn->setObjectName("onAirBtn");
    m_onAirBtn->setCheckable(true);
    srcGrid->addWidget(m_onAirBtn, 2, 0, 1, 2);

    m_notDveText = new QLabel("Upstream key 1 is not a DVE key, so position and size have no effect.", sources);
    m_notDveText->setObjectName("warnText");
    m_notDveText->setWordWrap(true);
    srcGrid->addWidget(m_notDveText, 3, 0, 1, 2);
    m_makeDveBtn = new QPushButton("Set key type to DVE", sources);
    srcGrid->addWidget(m_makeDveBtn, 4, 0, 1, 2);
    layout->addWidget(sources);

    connect(m_mainInput, &QComboBox::activated, this, [this](int i) {
        m_session->pip().setProgramInput(m_mainInput->itemData(i).toLongLong());
    });
    connect(m_pipInput, &QComboBox::activated, this, [this](int i) {
        m_session->pip().setPipInput(m_pipInput->itemData(i).toLongLong());
    });
    connect(m_onAirBtn, &QPushButton::clicked, this, [this](bool on) {
        m_session->pip().setOnAir(on);
    });
    connect(m_makeDveBtn, &QPushButton::clicked, this, [this]() { m_session->pip().makeDVE(); });

    // ── Position ──
    auto* position = new QGroupBox("POSITION", body);
    auto* posLayout = new QVBoxLayout(position);
    posLayout->setSpacing(4);
    m_posX = new PipValueControl("X", 2, position);
    m_posX->setLimits(kPositionX.sliderMin, kPositionX.sliderMax, kPositionX.spinMin, kPositionX.spinMax);
    m_posY = new PipValueControl("Y", 2, position);
    m_posY->setLimits(kPositionY.sliderMin, kPositionY.sliderMax, kPositionY.spinMin, kPositionY.spinMax);
    posLayout->addWidget(m_posX);
    posLayout->addWidget(m_posY);
    layout->addWidget(position);

    connect(m_posX, &PipValueControl::valueEdited, this, [this](double v) { queueValue(AtemPipField::PositionX, v); });
    connect(m_posY, &PipValueControl::valueEdited, this, [this](double v) { queueValue(AtemPipField::PositionY, v); });

    // ── Size ──
    auto* size = new QGroupBox("SIZE", body);
    auto* sizeLayout = new QVBoxLayout(size);
    sizeLayout->setSpacing(4);
    m_sizeX = new PipValueControl("Size", 3, size);
    m_sizeY = new PipValueControl("Height", 3, size);
    for (auto* c : { m_sizeX, m_sizeY })
        c->setLimits(kSize.sliderMin, kSize.sliderMax, kSize.spinMin, kSize.spinMax);
    m_lockAspect = new QCheckBox("Keep aspect ratio", size);
    m_lockAspect->setChecked(true);
    m_sizeY->setVisible(false);
    sizeLayout->addWidget(m_sizeX);
    sizeLayout->addWidget(m_sizeY);
    sizeLayout->addWidget(m_lockAspect);
    layout->addWidget(size);

    connect(m_sizeX, &PipValueControl::valueEdited, this, [this](double v) {
        queueValue(AtemPipField::SizeX, v);
        if (m_lockAspect->isChecked()) {
            m_sizeY->setValue(v);
            queueValue(AtemPipField::SizeY, v);
        }
    });
    connect(m_sizeY, &PipValueControl::valueEdited, this, [this](double v) { queueValue(AtemPipField::SizeY, v); });
    connect(m_lockAspect, &QCheckBox::toggled, this, [this](bool locked) {
        m_sizeX->setLabel(locked ? "Size" : "Width");
        m_sizeY->setVisible(!locked);
        if (locked) {
            m_sizeY->setValue(m_sizeX->value());
            queueValue(AtemPipField::SizeY, m_sizeX->value());
        }
    });

    // ── Crop ──
    auto* crop = new QGroupBox("CROP", body);
    auto* cropLayout = new QVBoxLayout(crop);
    cropLayout->setSpacing(4);
    m_cropEnabled = new QCheckBox("Crop enabled", crop);
    cropLayout->addWidget(m_cropEnabled);
    m_cropTop = new PipValueControl("Top", 2, crop);
    m_cropBottom = new PipValueControl("Bottom", 2, crop);
    m_cropLeft = new PipValueControl("Left", 2, crop);
    m_cropRight = new PipValueControl("Right", 2, crop);
    for (auto* c : { m_cropTop, m_cropBottom })
        c->setLimits(kCropTopBot.sliderMin, kCropTopBot.sliderMax, kCropTopBot.spinMin, kCropTopBot.spinMax);
    for (auto* c : { m_cropLeft, m_cropRight })
        c->setLimits(kCropLeftRt.sliderMin, kCropLeftRt.sliderMax, kCropLeftRt.spinMin, kCropLeftRt.spinMax);
    for (auto* c : { m_cropTop, m_cropBottom, m_cropLeft, m_cropRight })
        cropLayout->addWidget(c);
    layout->addWidget(crop);

    connect(m_cropEnabled, &QCheckBox::clicked, this, [this](bool on) { m_session->pip().setCropEnabled(on); });
    connect(m_cropTop, &PipValueControl::valueEdited, this, [this](double v) { queueValue(AtemPipField::CropTop, v); });
    connect(m_cropBottom, &PipValueControl::valueEdited, this, [this](double v) { queueValue(AtemPipField::CropBottom, v); });
    connect(m_cropLeft, &PipValueControl::valueEdited, this, [this](double v) { queueValue(AtemPipField::CropLeft, v); });
    connect(m_cropRight, &PipValueControl::valueEdited, this, [this](double v) { queueValue(AtemPipField::CropRight, v); });

    // ── Resets ──
    auto* resetRow = new QHBoxLayout();
    auto* resetDve = new QPushButton("Reset position/size", body);
    auto* resetCrop = new QPushButton("Reset crop", body);
    resetRow->addWidget(resetDve);
    resetRow->addWidget(resetCrop);
    layout->addLayout(resetRow);
    layout->addStretch(1);

    connect(resetDve, &QPushButton::clicked, this, [this]() { m_session->pip().resetPositionAndSize(); });
    connect(resetCrop, &QPushButton::clicked, this, [this]() { m_session->pip().resetCrop(); });

    return body;
}

void AtemPipDock::onConnectionChanged(AtemState state) {
    updateStatus();
    if (state == AtemState::Connecting) return;
    m_syncAspectLock = true;
    m_pending.clear();
    m_flushTimer.stop();
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

    if (!m_session->isConnected()) {
        m_offlineText->setText("ATEM not connected.\nConnect here or from the ATEM Macros panel (⚙).");
        m_pages->setCurrentWidget(m_offlinePage);
        return;
    }

    AtemPip& pip = m_session->pip();
    AtemPipState s = pip.state();
    if (!s.available) {
        m_offlineText->setText("This switcher has no upstream keyer, so PiP is not available.");
        m_pages->setCurrentWidget(m_offlinePage);
        return;
    }
    m_pages->setCurrentIndex(1);

    auto inputs = pip.inputs();
    if (!sameInputs(inputs, m_inputs)) rebuildInputLists(inputs);
    selectInput(m_mainInput, s.programInput);
    selectInput(m_pipInput, s.pipInput);

    m_onAirBtn->setChecked(s.onAir);
    m_onAirBtn->setText(s.onAir ? "PiP ON AIR" : "PiP OFF");
    m_notDveText->setVisible(!s.isDVE);
    m_makeDveBtn->setVisible(!s.isDVE && s.canBeDVE);

    double sizeMax = s.canScaleUp ? kSizeScaleUpMax : kSize.spinMax;
    m_sizeX->setLimits(kSize.sliderMin, kSize.sliderMax, kSize.spinMin, sizeMax);
    m_sizeY->setLimits(kSize.sliderMin, kSize.sliderMax, kSize.spinMin, sizeMax);

    // Never overwrite a control the operator is dragging or typing into, and
    // not one with an edit still waiting to be sent.
    auto apply = [this](PipValueControl* c, AtemPipField f, double v) {
        if (!c->isEditing() && m_pending.find(f) == m_pending.end()) c->setValue(v);
    };
    apply(m_posX, AtemPipField::PositionX, s.positionX);
    apply(m_posY, AtemPipField::PositionY, s.positionY);
    apply(m_sizeX, AtemPipField::SizeX, s.sizeX);
    apply(m_sizeY, AtemPipField::SizeY, s.sizeY);
    apply(m_cropTop, AtemPipField::CropTop, s.cropTop);
    apply(m_cropBottom, AtemPipField::CropBottom, s.cropBottom);
    apply(m_cropLeft, AtemPipField::CropLeft, s.cropLeft);
    apply(m_cropRight, AtemPipField::CropRight, s.cropRight);
    m_cropEnabled->setChecked(s.cropEnabled);

    // The aspect lock follows the device only on (re)connect; afterwards it is
    // the operator's. Checking it on every refresh would unlock it mid-edit,
    // when the SetSizeX echo arrives before the SetSizeY one.
    if (m_syncAspectLock) {
        m_syncAspectLock = false;
        bool locked = std::abs(s.sizeX - s.sizeY) <= 0.001;
        QSignalBlocker block(m_lockAspect);
        m_lockAspect->setChecked(locked);
        m_sizeX->setLabel(locked ? "Size" : "Width");
        m_sizeY->setVisible(!locked);
    }
}

void AtemPipDock::rebuildInputLists(const std::vector<AtemInputInfo>& inputs) {
    m_inputs = inputs;
    QSignalBlocker blockMain(m_mainInput);
    QSignalBlocker blockPip(m_pipInput);
    m_mainInput->clear();
    m_pipInput->clear();
    for (const auto& in : inputs) {
        QVariant id = QVariant::fromValue<qlonglong>(in.id);
        if (in.canBeProgram) m_mainInput->addItem(inputLabel(in), id);
        if (in.canBePip) m_pipInput->addItem(inputLabel(in), id);
    }
}

void AtemPipDock::queueValue(AtemPipField field, double value) {
    m_pending[field] = value;
    if (!m_flushTimer.isActive()) m_flushTimer.start();
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
