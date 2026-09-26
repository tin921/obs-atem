#pragma once

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include "atem-pip.h"

class PipSettings;
class QPainter;

// The switcher's DVE coordinate space for a 16:9 format: frame edges at
// X ±16 and Y ±9, +Y up, box position = centre, size 1.0 = full frame.
// (Unverified on hardware — see CLAUDE.md.)
namespace PipGeometry {
constexpr double kFrameWidth = 32.0;
constexpr double kFrameHeight = 18.0;

// Visible (cropped) PiP box in normalised frame coordinates (0..1, y down).
QRectF visibleRect(const AtemPipState& s);
// Part of the PiP source picture shown in the box (normalised, y down).
QRectF sourceRect(const AtemPipState& s);
} // namespace PipGeometry

// Program monitor for the PiP panel: main input full frame with the PiP box
// on top. Drag the box to move it (snaps to the safe area and centre lines,
// Alt = no snap), drag the corner handle to resize it (aspect ratio kept),
// arrow keys nudge (Shift = bigger steps).
class PipPreview : public QWidget {
    Q_OBJECT
public:
    enum class Style {
        Monitor,    // the live preview: safe area, off-air outline, tag
        Thumbnail,  // a preset thumbnail: only what is on program
    };

    PipPreview(const PipSettings* settings, QWidget* parent = nullptr);

    void setState(const AtemPipState& state);
    bool isDragging() const { return m_drag != Drag::None; }

    // Shared by the preview and preset thumbnails.
    static void paintProgram(QPainter& p, const QRectF& target, const AtemPipState& s,
                             const PipSettings& settings, Style style);

signals:
    void moved(double positionX, double positionY);
    void resized(double size, double positionX, double positionY);
    void nudged(double dx, double dy);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    enum class Drag { None, Move, Resize };

    QPointF toUnits(const QPointF& pos) const;
    QRectF boxRect() const;      // widget pixels
    QRectF handleRect() const;   // widget pixels
    void updateCursor(const QPointF& pos);

    const PipSettings* m_settings;
    AtemPipState m_state;
    Drag m_drag = Drag::None;
    QPointF m_dragStart;         // units
    AtemPipState m_dragFrom;
};
