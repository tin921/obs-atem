#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <array>
#include <functional>
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
    static constexpr int kPresetCount = 20;
    static constexpr int kVisiblePresets = 7;   // the PiP panel's preset column shows 7, scrolls for more

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
    // Draws every saved button's thumbnail again (one changed() at the end):
    // after an import, or when a camera's picture or colour changes.
    void redrawThumbnails(const std::function<QImage(const PipPreset&)>& draw);

    // The Views panel: which presets it shows, in grid order (0-based
    // preset indices; default 0–6).
    const QList<int>& viewSlots() const { return m_viewSlots; }
    void setViewSlots(const QList<int>& order);

    // Export / import of everything above as JSON: camera names, colours
    // and picture file paths (not the pictures), "show names", the preset
    // buttons' values and the Views choice. Preset thumbnails are not
    // included: imported presets have none until they are drawn again.
    QByteArray exportJson() const;
    struct ImportResult {
        bool ok = false;
        QString error;                 // why nothing was imported
        int presets = 0;               // buttons with a saved setup
        QStringList missingPictures;   // "Camera N: path" that couldn't be loaded
    };
    // Checks the whole file first; imports nothing if any of it is invalid.
    ImportResult importJson(const QByteArray& json);

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
    QList<int> m_viewSlots;
};
