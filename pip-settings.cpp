#include "pip-settings.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalBlocker>
#include <cmath>

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

// The export file: {"format": kExportFormat, "version": kExportVersion, ...}.
constexpr const char* kExportFormat = "obs-atem-pip";
constexpr int kExportVersion = 1;

// Reads the fields of one imported object; keeps the first problem in `error`.
struct JsonReader {
    const QJsonObject& obj;
    QString where;
    QString& error;

    void fail(const QString& key, const QString& expected) {
        if (error.isEmpty()) error = QString("%1: \"%2\" must be %3.").arg(where, key, expected);
    }
    QString string(const QString& key) {
        QJsonValue v = obj.value(key);
        if (v.isUndefined() || v.isNull()) return QString();
        if (!v.isString()) fail(key, "text");
        return v.toString();
    }
    bool boolean(const QString& key, bool fallback) {
        QJsonValue v = obj.value(key);
        if (v.isUndefined()) return fallback;
        if (!v.isBool()) fail(key, "true or false");
        return v.toBool(fallback);
    }
    double number(const QString& key, double fallback, bool required = false) {
        QJsonValue v = obj.value(key);
        if (v.isUndefined() && !required) return fallback;
        if (!v.isDouble() || !std::isfinite(v.toDouble())) {
            fail(key, "a number");
            return fallback;
        }
        return v.toDouble();
    }
    qint64 integer(const QString& key, qint64 min, qint64 max) {
        QJsonValue v = obj.value(key);
        double d = v.toDouble(-1);
        if (!v.isDouble() || d != std::floor(d) || d < min || d > max) {
            fail(key, QString("a whole number from %1 to %2").arg(min).arg(max));
            return min;
        }
        return static_cast<qint64>(d);
    }
};

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

void PipSettings::redrawThumbnails(const std::function<QImage(const PipPreset&)>& draw) {
    {
        QSignalBlocker blocker(this);
        for (int i = 0; i < kPresetCount; ++i) {
            if (!m_presets[i].valid) continue;
            PipPreset p = m_presets[i];
            p.thumbnail = draw(p);
            setPreset(i, p);
        }
    }
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

// ── Export / import ──────────────────────────────────────────

QByteArray PipSettings::exportJson() const {
    QJsonObject root;
    root["format"] = kExportFormat;
    root["version"] = kExportVersion;

    QJsonArray cameras;
    for (int i = 0; i < kCameraCount; ++i) {
        const Camera& cam = m_cameras[i];
        QJsonObject o;
        o["camera"] = i + 1;
        o["name"] = cam.name;                                                // "" = the switcher's name
        o["color"] = cam.color.isValid() ? cam.color.name() : QString();    // "" = the default colour
        o["picture"] = cam.picturePath;                                      // the file's path only
        cameras.append(o);
    }
    root["cameras"] = cameras;
    root["showNames"] = m_showNames;

    QJsonArray presets;
    for (int i = 0; i < kPresetCount; ++i) {
        const PipPreset& p = m_presets[i];
        if (!p.valid) continue;
        QJsonObject o;
        o["button"] = i + 1;
        o["programInput"] = static_cast<qint64>(p.programInput);
        o["pipInput"] = static_cast<qint64>(p.pipInput);
        o["onAir"] = p.onAir;
        o["positionX"] = p.positionX;
        o["positionY"] = p.positionY;
        o["sizeX"] = p.sizeX;
        o["sizeY"] = p.sizeY;
        o["cropTop"] = p.cropTop;
        o["cropBottom"] = p.cropBottom;
        o["cropLeft"] = p.cropLeft;
        o["cropRight"] = p.cropRight;
        o["cropEnabled"] = p.cropEnabled;
        presets.append(o);
    }
    root["presets"] = presets;

    QJsonArray views;
    for (int index : m_viewSlots) views.append(index + 1);
    root["views"] = views;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

PipSettings::ImportResult PipSettings::importJson(const QByteArray& json) {
    ImportResult result;
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        result.error = QString("This is not a PiP settings file (%1).").arg(parseError.errorString());
        return result;
    }
    QJsonObject root = doc.object();
    if (root.value("format").toString() != kExportFormat) {
        result.error = "This is not a PiP settings file exported from the ATEM PiP panel.";
        return result;
    }
    if (root.value("version").toInt() > kExportVersion) {
        result.error = "This file was exported by a newer version of the plugin.";
        return result;
    }

    // Read and check everything before changing anything.
    QString error;
    struct CameraIn { bool present = false; QString name; QColor color; QString picture; };
    std::array<CameraIn, kCameraCount> cameras;
    for (const QJsonValue& v : root.value("cameras").toArray()) {
        QJsonObject o = v.toObject();
        JsonReader r{ o, "Camera", error };
        int cam = static_cast<int>(r.integer("camera", 1, kCameraCount)) - 1;
        if (!error.isEmpty()) break;
        r.where = QString("Camera %1").arg(cam + 1);
        CameraIn& in = cameras[cam];
        in.present = true;
        in.name = r.string("name").trimmed().left(20);
        QString color = r.string("color");
        in.color = QColor(color);
        if (!color.isEmpty() && !in.color.isValid() && error.isEmpty())
            error = QString("Camera %1: \"%2\" is not a colour.").arg(cam + 1).arg(color);
        in.picture = r.string("picture");
    }
    bool showNames = JsonReader{ root, "File", error }.boolean("showNames", m_showNames);

    std::array<PipPreset, kPresetCount> presets{};
    for (const QJsonValue& v : root.value("presets").toArray()) {
        QJsonObject o = v.toObject();
        JsonReader r{ o, "Button", error };
        int index = static_cast<int>(r.integer("button", 1, kPresetCount)) - 1;
        if (!error.isEmpty()) break;
        r.where = QString("Button %1").arg(index + 1);
        if (presets[index].valid && error.isEmpty())
            error = QString("Button %1 is in the file twice.").arg(index + 1);
        PipPreset& p = presets[index];
        p.valid = true;
        p.programInput = r.integer("programInput", 0, 1LL << 40);
        p.pipInput = r.integer("pipInput", 0, 1LL << 40);
        p.onAir = r.boolean("onAir", false);
        p.positionX = r.number("positionX", 0, true);
        p.positionY = r.number("positionY", 0, true);
        p.sizeX = r.number("sizeX", 0, true);
        p.sizeY = r.number("sizeY", p.sizeX);
        p.cropTop = r.number("cropTop", 0);
        p.cropBottom = r.number("cropBottom", 0);
        p.cropLeft = r.number("cropLeft", 0);
        p.cropRight = r.number("cropRight", 0);
        bool anyEdge = p.cropTop > 0 || p.cropBottom > 0 || p.cropLeft > 0 || p.cropRight > 0;
        p.cropEnabled = r.boolean("cropEnabled", anyEdge);
    }

    bool hasViews = root.contains("views");
    QList<int> views;
    for (const QJsonValue& v : root.value("views").toArray()) {
        double n = v.toDouble(0);
        if (!v.isDouble() || n != std::floor(n) || n < 1 || n > kPresetCount) {
            if (error.isEmpty())
                error = QString("\"views\" must list button numbers from 1 to %1.").arg(kPresetCount);
            break;
        }
        views.append(static_cast<int>(n) - 1);
    }
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }

    // Apply, with one changed() at the end instead of one per setting.
    {
        QSignalBlocker blocker(this);
        for (int i = 0; i < kCameraCount; ++i) {
            const CameraIn& in = cameras[i];
            if (!in.present) continue;
            setCustomName(i, in.name);
            setColor(i, in.color);
            if (!setPicturePath(i, in.picture)) {
                result.missingPictures << QString("Camera %1: %2").arg(i + 1).arg(in.picture);
                setPicturePath(i, QString());
            }
        }
        setShowNames(showNames);
        for (int i = 0; i < kPresetCount; ++i) {
            setPreset(i, presets[i]);
            if (presets[i].valid) ++result.presets;
        }
        if (hasViews) setViewSlots(views);
    }
    emit changed();
    result.ok = true;
    return result;
}
