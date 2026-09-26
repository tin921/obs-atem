#include "pip-preview.h"
#include "pip-settings.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

using namespace PipGeometry;

namespace {

constexpr double kHalfW = kFrameWidth / 2;
constexpr double kHalfH = kFrameHeight / 2;

// Snapping while dragging: the visible box edges stick to the 90% safe area,
// the box centre to the frame centre lines.
constexpr double kSafeX = kHalfW * 0.9;
constexpr double kSafeY = kHalfH * 0.9;
constexpr double kSnap = 0.4;

constexpr double kMinSize = 0.05;
constexpr double kHandleSize = 10.0;

struct Crop {
    double top, bottom, left, right;
};

Crop activeCrop(const AtemPipState& s) {
    if (!s.cropEnabled) return { 0, 0, 0, 0 };
    return { s.cropTop, s.cropBottom, s.cropLeft, s.cropRight };
}

// Draws an input's picture (the given part of it) or its colour + name.
void drawInput(QPainter& p, const QRectF& dst, const QRectF& src, BMDSwitcherInputId input,
               const PipSettings& settings, bool upperCaseName) {
    QPixmap pix = settings.picture(input);
    if (!pix.isNull()) {
        QRectF from(src.x() * pix.width(), src.y() * pix.height(),
                    src.width() * pix.width(), src.height() * pix.height());
        p.drawPixmap(dst, pix, from);
        return;
    }

    if (settings.isColorBars(input)) {
        static const QColor bars[] = { "#c0c0c0", "#c0c000", "#00c0c0", "#00c000",
                                       "#c000c0", "#c00000", "#0000c0" };
        double w = dst.width() / 7;
        for (int i = 0; i < 7; ++i)
            p.fillRect(QRectF(dst.x() + i * w, dst.y(), w + 1, dst.height()), bars[i]);
    } else {
        p.fillRect(dst, settings.color(input));
    }

    QString name = settings.name(input);
    if (upperCaseName) name = name.toUpper();
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(std::clamp(static_cast<int>(dst.height() / 4), 7, 14));
    p.setFont(font);
    p.setPen(QColor(255, 255, 255, 170));
    p.drawText(dst, Qt::AlignCenter, p.fontMetrics().elidedText(name, Qt::ElideRight,
                                                                static_cast<int>(dst.width()) - 4));
}

} // namespace

// ── Geometry ─────────────────────────────────────────────────

QRectF PipGeometry::visibleRect(const AtemPipState& s) {
    Crop c = activeCrop(s);
    double w = kFrameWidth * s.sizeX;
    double h = kFrameHeight * s.sizeY;
    double x0 = s.positionX - w / 2 + c.left * s.sizeX;
    double x1 = s.positionX + w / 2 - c.right * s.sizeX;
    double yTop = s.positionY + h / 2 - c.top * s.sizeY;
    double yBottom = s.positionY - h / 2 + c.bottom * s.sizeY;
    return QRectF((x0 + kHalfW) / kFrameWidth, (kHalfH - yTop) / kFrameHeight,
                  std::max(0.0, x1 - x0) / kFrameWidth, std::max(0.0, yTop - yBottom) / kFrameHeight);
}

QRectF PipGeometry::sourceRect(const AtemPipState& s) {
    Crop c = activeCrop(s);
    return QRectF(c.left / kFrameWidth, c.top / kFrameHeight,
                  std::max(0.0, kFrameWidth - c.left - c.right) / kFrameWidth,
                  std::max(0.0, kFrameHeight - c.top - c.bottom) / kFrameHeight);
}

// ── Painting ─────────────────────────────────────────────────

void PipPreview::paintProgram(QPainter& p, const QRectF& target, const AtemPipState& s,
                              const PipSettings& settings, Style style) {
    p.save();
    p.setClipRect(target);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    drawInput(p, target, QRectF(0, 0, 1, 1), s.programInput, settings, true);

    bool showPip = s.isDVE && (s.onAir || style == Style::Monitor);
    if (showPip) {
        QRectF v = visibleRect(s);
        QRectF dst(target.x() + v.x() * target.width(), target.y() + v.y() * target.height(),
                   v.width() * target.width(), v.height() * target.height());
        if (dst.width() > 0.5 && dst.height() > 0.5) {
            // Off air: a faded ghost shows where the PiP would appear.
            p.setOpacity(s.onAir ? 1.0 : 0.35);
            drawInput(p, dst, sourceRect(s), s.pipInput, settings, false);
            p.setOpacity(1.0);
            QPen outline(QColor(255, 255, 255, s.onAir ? 130 : 255), 1,
                         s.onAir ? Qt::SolidLine : Qt::DashLine);
            p.setPen(outline);
            p.setBrush(Qt::NoBrush);
            p.drawRect(dst.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }

    if (style == Style::Monitor) {
        p.setPen(QPen(QColor(255, 255, 255, 30), 1, Qt::DashLine));
        p.drawRect(target.adjusted(target.width() * 0.05, target.height() * 0.05,
                                   -target.width() * 0.05, -target.height() * 0.05));

        QString tag = s.onAir || !s.isDVE ? "PROGRAM" : "PROGRAM · PiP off air";
        QFont font = p.font();
        font.setBold(false);
        font.setPixelSize(9);
        p.setFont(font);
        QRectF tagRect = p.fontMetrics().boundingRect(tag).adjusted(-4, 0, 4, 0);
        tagRect.moveBottomLeft(QPointF(target.left() + 4, target.bottom() - 3));
        p.fillRect(tagRect, QColor(0, 0, 0, 130));
        p.setPen(QColor(255, 255, 255, 180));
        p.drawText(tagRect, Qt::AlignCenter, tag);
    }
    p.restore();
}

// ── Widget ───────────────────────────────────────────────────

PipPreview::PipPreview(const PipSettings* settings, QWidget* parent)
    : QWidget(parent), m_settings(settings)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumHeight(60);
    setToolTip("Drag the PiP to move it, drag its corner to resize.\n"
               "Arrow keys nudge (Shift = bigger steps), Alt while dragging = no snapping.");
}

void PipPreview::setState(const AtemPipState& state) {
    m_state = state;
    update();
}

QRectF PipPreview::boxRect() const {
    QRectF v = visibleRect(m_state);
    return QRectF(v.x() * width(), v.y() * height(), v.width() * width(), v.height() * height());
}

QRectF PipPreview::handleRect() const {
    QPointF corner = boxRect().bottomRight();
    return QRectF(corner.x() - kHandleSize / 2, corner.y() - kHandleSize / 2, kHandleSize, kHandleSize);
}

QPointF PipPreview::toUnits(const QPointF& pos) const {
    return QPointF(pos.x() / width() * kFrameWidth - kHalfW,
                   kHalfH - pos.y() / height() * kFrameHeight);
}

void PipPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintProgram(p, rect(), m_state, *m_settings, Style::Monitor);

    if (m_state.isDVE) {
        QRectF h = handleRect();
        p.fillRect(h, Qt::white);
        p.setPen(QPen(Qt::black, 1));
        p.drawRect(h.adjusted(0.5, 0.5, -0.5, -0.5));
    }

    p.setPen(QPen(hasFocus() ? QColor("#c83232") : QColor("#3c3c3c"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}

void PipPreview::updateCursor(const QPointF& pos) {
    if (!m_state.isDVE) setCursor(Qt::ArrowCursor);
    else if (handleRect().adjusted(-2, -2, 2, 2).contains(pos)) setCursor(Qt::SizeFDiagCursor);
    else if (boxRect().contains(pos)) setCursor(Qt::SizeAllCursor);
    else setCursor(Qt::ArrowCursor);
}

void PipPreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_state.isDVE) return;
    QPointF pos = e->position();
    if (handleRect().adjusted(-2, -2, 2, 2).contains(pos)) m_drag = Drag::Resize;
    else if (boxRect().contains(pos)) m_drag = Drag::Move;
    else return;
    setFocus(Qt::MouseFocusReason);
    m_dragStart = toUnits(pos);
    m_dragFrom = m_state;
}

void PipPreview::mouseMoveEvent(QMouseEvent* e) {
    QPointF pos = e->position();
    if (m_drag == Drag::None) {
        updateCursor(pos);
        return;
    }

    QPointF u = toUnits(pos);
    const AtemPipState& f = m_dragFrom;
    Crop c = activeCrop(f);

    if (m_drag == Drag::Move) {
        double x = f.positionX + (u.x() - m_dragStart.x());
        double y = f.positionY + (u.y() - m_dragStart.y());
        if (!(e->modifiers() & Qt::AltModifier)) {
            double halfW = kHalfW * f.sizeX, halfH = kHalfH * f.sizeY;
            double left = x - halfW + c.left * f.sizeX, right = x + halfW - c.right * f.sizeX;
            double top = y + halfH - c.top * f.sizeY, bottom = y - halfH + c.bottom * f.sizeY;
            if (std::abs(left + kSafeX) < kSnap) x += -kSafeX - left;
            else if (std::abs(right - kSafeX) < kSnap) x += kSafeX - right;
            else if (std::abs(x) < kSnap) x = 0;
            if (std::abs(top - kSafeY) < kSnap) y += kSafeY - top;
            else if (std::abs(bottom + kSafeY) < kSnap) y += -kSafeY - bottom;
            else if (std::abs(y) < kSnap) y = 0;
        }
        emit moved(x, y);
        return;
    }

    // Resize about the visible top-left corner, aspect ratio kept.
    double visibleW = kFrameWidth - c.left - c.right;
    if (visibleW <= 0) return;
    double topLeftX = f.positionX - kHalfW * f.sizeX + c.left * f.sizeX;
    double topLeftY = f.positionY + kHalfH * f.sizeY - c.top * f.sizeY;
    double maxSize = f.canScaleUp ? 2.0 : 1.0;
    double size = std::clamp((u.x() - topLeftX) / visibleW, kMinSize, maxSize);
    emit resized(size,
                 topLeftX + kHalfW * size - c.left * size,
                 topLeftY - kHalfH * size + c.top * size);
}

void PipPreview::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) m_drag = Drag::None;
    updateCursor(e->position());
}

void PipPreview::keyPressEvent(QKeyEvent* e) {
    double step = (e->modifiers() & Qt::ShiftModifier) ? 1.0 : 0.1;
    switch (e->key()) {
    case Qt::Key_Left:  emit nudged(-step, 0); break;
    case Qt::Key_Right: emit nudged(step, 0); break;
    case Qt::Key_Up:    emit nudged(0, step); break;
    case Qt::Key_Down:  emit nudged(0, -step); break;
    default: QWidget::keyPressEvent(e); return;
    }
}
