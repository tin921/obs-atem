#include "pip-settings.h"

#include <QBuffer>
#include <QSettings>

namespace {

constexpr const char* kSettingsOrg = "obs-atem";
constexpr const char* kSettingsApp = "obs-atem";

// Placeholder colours for cameras without a picture (same as the mockup).
const char* const kDefaultColors[PipSettings::kCameraCount] = {
    "#35618f", "#8f5a35", "#3f7d4e", "#6f4a8f",
};

// Pictures are kept at this size: plenty for buttons, preview and thumbnails.
constexpr int kPictureWidth = 320;
constexpr int kPictureHeight = 180;

QString cameraKey(int camera, const char* field) {
    return QString("pip/camera%1/%2").arg(camera + 1).arg(field);
}

QString presetKey(int index, const char* field) {
    return QString("pip/preset%1/%2").arg(index + 1).arg(field);
}

} // namespace

int PipSettings::cameraIndex(BMDSwitcherInputId input) {
    return (input >= 1 && input <= kCameraCount) ? static_cast<int>(input - 1) : -1;
}

PipSettings::PipSettings(QObject* parent)
    : QObject(parent)
{
    load();
}

void PipSettings::load() {
    QSettings s(kSettingsOrg, kSettingsApp);
    for (int i = 0; i < kCameraCount; ++i) {
        Camera& cam = m_cameras[i];
        cam.name = s.value(cameraKey(i, "name")).toString();
        cam.color = QColor(s.value(cameraKey(i, "color")).toString());
        cam.picturePath = s.value(cameraKey(i, "picture")).toString();
        cam.picture = loadPicture(cam.picturePath);
    }
    m_showNames = s.value("pip/showNames", false).toBool();

    m_viewSlots.clear();
    if (s.contains("views/slots")) {
        for (const QString& n : s.value("views/slots").toString().split(',', Qt::SkipEmptyParts)) {
            int index = n.toInt() - 1;   // stored 1-based, like the button numbers
            if (index >= 0 && index < kPresetCount && !m_viewSlots.contains(index)) m_viewSlots.append(index);
        }
    } else {
        for (int i = 0; i < kVisiblePresets; ++i) m_viewSlots.append(i);
    }

    for (int i = 0; i < kPresetCount; ++i) {
        PipPreset& p = m_presets[i];
        p.valid = s.value(presetKey(i, "valid"), false).toBool();
        if (!p.valid) continue;
        p.programInput = s.value(presetKey(i, "programInput")).toLongLong();
        p.pipInput = s.value(presetKey(i, "pipInput")).toLongLong();
        p.onAir = s.value(presetKey(i, "onAir")).toBool();
        p.positionX = s.value(presetKey(i, "positionX")).toDouble();
        p.positionY = s.value(presetKey(i, "positionY")).toDouble();
        p.sizeX = s.value(presetKey(i, "sizeX")).toDouble();
        p.sizeY = s.value(presetKey(i, "sizeY")).toDouble();
        p.cropTop = s.value(presetKey(i, "cropTop")).toDouble();
        p.cropBottom = s.value(presetKey(i, "cropBottom")).toDouble();
        p.cropLeft = s.value(presetKey(i, "cropLeft")).toDouble();
        p.cropRight = s.value(presetKey(i, "cropRight")).toDouble();
        // Presets saved before the flag existed: crop was on when an edge was set.
        bool anyEdge = p.cropTop > 0 || p.cropBottom > 0 || p.cropLeft > 0 || p.cropRight > 0;
        p.cropEnabled = s.value(presetKey(i, "cropEnabled"), anyEdge).toBool();
        p.thumbnail.loadFromData(s.value(presetKey(i, "thumbnail")).toByteArray(), "PNG");
    }
}

QPixmap PipSettings::loadPicture(const QString& path) {
    if (path.isEmpty()) return QPixmap();
    QImage img(path);
    if (img.isNull()) return QPixmap();
    // Scale to cover 16:9, then crop the centre.
    QImage scaled = img.scaled(kPictureWidth, kPictureHeight, Qt::KeepAspectRatioByExpanding,
                               Qt::SmoothTransformation);
    int x = (scaled.width() - kPictureWidth) / 2;
    int y = (scaled.height() - kPictureHeight) / 2;
    return QPixmap::fromImage(scaled.copy(x, y, kPictureWidth, kPictureHeight));
}

QString PipSettings::defaultName(int camera) const {
    auto it = m_deviceNames.find(cameraInput(camera));
    if (it != m_deviceNames.end() && !it->second.isEmpty()) return it->second;
    return QString("Camera %1").arg(camera + 1);
}

QColor PipSettings::defaultColor(int camera) const {
    return QColor(kDefaultColors[camera]);
}

QString PipSettings::name(BMDSwitcherInputId input) const {
    int cam = cameraIndex(input);
    if (cam >= 0) {
        return m_cameras[cam].name.isEmpty() ? defaultName(cam) : m_cameras[cam].name;
    }
    auto it = m_deviceNames.find(input);
    if (it != m_deviceNames.end() && !it->second.isEmpty()) return it->second;
    if (input == 0) return "Black";
    return QString("Input %1").arg(input);
}

QColor PipSettings::color(BMDSwitcherInputId input) const {
    int cam = cameraIndex(input);
    if (cam >= 0) {
        return m_cameras[cam].color.isValid() ? m_cameras[cam].color : defaultColor(cam);
    }
    if (input == 0) return Qt::black;
    return QColor("#333333");
}

QPixmap PipSettings::picture(BMDSwitcherInputId input) const {
    int cam = cameraIndex(input);
    return cam >= 0 ? m_cameras[cam].picture : QPixmap();
}

void PipSettings::setCustomName(int camera, const QString& name) {
    QString clean = name.trimmed();
    if (m_cameras[camera].name == clean) return;
    m_cameras[camera].name = clean;
    QSettings s(kSettingsOrg, kSettingsApp);
    if (clean.isEmpty()) s.remove(cameraKey(camera, "name"));
    else s.setValue(cameraKey(camera, "name"), clean);
    emit changed();
}

void PipSettings::setColor(int camera, const QColor& color) {
    m_cameras[camera].color = color;
    QSettings s(kSettingsOrg, kSettingsApp);
    if (color.isValid()) s.setValue(cameraKey(camera, "color"), color.name());
    else s.remove(cameraKey(camera, "color"));
    emit changed();
}

bool PipSettings::setPicturePath(int camera, const QString& path) {
    QPixmap picture = loadPicture(path);
    if (!path.isEmpty() && picture.isNull()) return false;
    m_cameras[camera].picturePath = path;
    m_cameras[camera].picture = picture;
    QSettings s(kSettingsOrg, kSettingsApp);
    if (path.isEmpty()) s.remove(cameraKey(camera, "picture"));
    else s.setValue(cameraKey(camera, "picture"), path);
    emit changed();
    return true;
}

void PipSettings::setShowNames(bool show) {
    if (m_showNames == show) return;
    m_showNames = show;
    QSettings(kSettingsOrg, kSettingsApp).setValue("pip/showNames", show);
    emit changed();
}

void PipSettings::setViewSlots(const QList<int>& order) {
    QList<int> clean;
    for (int index : order)
        if (index >= 0 && index < kPresetCount && !clean.contains(index)) clean.append(index);
    if (clean == m_viewSlots) return;
    m_viewSlots = clean;
    QStringList numbers;
    for (int index : clean) numbers << QString::number(index + 1);
    QSettings(kSettingsOrg, kSettingsApp).setValue("views/slots", numbers.join(','));
    emit changed();
}

void PipSettings::setDeviceInputs(const std::vector<AtemInputInfo>& inputs) {
    std::map<BMDSwitcherInputId, QString> names;
    for (const auto& in : inputs) names[in.id] = QString::fromStdString(in.longName);
    if (names == m_deviceNames) return;
    m_deviceNames = std::move(names);
    emit changed();
}

void PipSettings::setPreset(int index, const PipPreset& preset) {
    m_presets[index] = preset;
    QSettings s(kSettingsOrg, kSettingsApp);
    s.remove(QString("pip/preset%1").arg(index + 1));
    if (preset.valid) {
        s.setValue(presetKey(index, "valid"), true);
        s.setValue(presetKey(index, "programInput"), static_cast<qlonglong>(preset.programInput));
        s.setValue(presetKey(index, "pipInput"), static_cast<qlonglong>(preset.pipInput));
        s.setValue(presetKey(index, "onAir"), preset.onAir);
        s.setValue(presetKey(index, "positionX"), preset.positionX);
        s.setValue(presetKey(index, "positionY"), preset.positionY);
        s.setValue(presetKey(index, "sizeX"), preset.sizeX);
        s.setValue(presetKey(index, "sizeY"), preset.sizeY);
        s.setValue(presetKey(index, "cropTop"), preset.cropTop);
        s.setValue(presetKey(index, "cropBottom"), preset.cropBottom);
        s.setValue(presetKey(index, "cropLeft"), preset.cropLeft);
        s.setValue(presetKey(index, "cropRight"), preset.cropRight);
        s.setValue(presetKey(index, "cropEnabled"), preset.cropEnabled);

        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        preset.thumbnail.save(&buffer, "PNG");
        s.setValue(presetKey(index, "thumbnail"), png);
    }
    emit changed();
}
