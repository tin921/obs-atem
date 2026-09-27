#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <array>
#include <map>
#include <vector>

#include "atem-pip.h"

// A saved PiP setup, recalled by one of the preset buttons.
struct PipPreset {
    bool valid = false;
    BMDSwitcherInputId programInput = 0;
    BMDSwitcherInputId pipInput = 0;
    bool onAir = false;
    double positionX = 0.0;
    double positionY = 0.0;
    double sizeX = 0.0;
    double sizeY = 0.0;
    double cropTop = 0.0;
    double cropBottom = 0.0;
    double cropLeft = 0.0;
    double cropRight = 0.0;
    // Crop (the DVE mask) on or off. Separate from the edges: another client
    // can leave edges set with the mask off.
    bool cropEnabled = false;
    QImage thumbnail;   // program as the panel drew it when saved
};

// Operator settings for the PiP panel: a name, colour and picture per camera,
// and the preset buttons. Stored with QSettings next to the connection
// settings (HKCU\Software\obs-atem\obs-atem, group "pip"), so the plugin and
// atem-harness share them.
class PipSettings : public QObject {
    Q_OBJECT
public:
    static constexpr int kCameraCount = 4;
    static constexpr int kPresetCount = 7;

    // ATEM Mini HDMI inputs 1–4.
    static BMDSwitcherInputId cameraInput(int camera) { return camera + 1; }
    static int cameraIndex(BMDSwitcherInputId input);   // -1 if not a camera

    explicit PipSettings(QObject* parent = nullptr);

    // Look of any input: custom name / colour / picture for cameras, the
    // switcher's own name (or a sensible default) for everything else.
    QString name(BMDSwitcherInputId input) const;
    QColor color(BMDSwitcherInputId input) const;
    QPixmap picture(BMDSwitcherInputId input) const;   // null if none
    bool isColorBars(BMDSwitcherInputId input) const { return input == 1000; }

    QString defaultName(int camera) const;
    QString customName(int camera) const { return m_cameras[camera].name; }
    QColor defaultColor(int camera) const;
    QString picturePath(int camera) const { return m_cameras[camera].picturePath; }
    bool showNames() const { return m_showNames; }

    void setCustomName(int camera, const QString& name);
    void setColor(int camera, const QColor& color);
    bool setPicturePath(int camera, const QString& path);  // false if it can't be loaded
    void setShowNames(bool show);
    void setDeviceInputs(const std::vector<AtemInputInfo>& inputs);

    const PipPreset& preset(int index) const { return m_presets[index]; }
    void setPreset(int index, const PipPreset& preset);

signals:
    void changed();

private:
    struct Camera {
        QString name;
        QColor color;
        QString picturePath;
        QPixmap picture;   // scaled/cropped to 16:9
    };

    static QPixmap loadPicture(const QString& path);
    void load();

    std::array<Camera, kCameraCount> m_cameras;
    std::array<PipPreset, kPresetCount> m_presets;
    std::map<BMDSwitcherInputId, QString> m_deviceNames;
    bool m_showNames = false;
};
