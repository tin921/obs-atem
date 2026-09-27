#include "pip-widgets.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>

namespace {

const QColor kLit(0xe0, 0x89, 0x2b);
const QColor kHover(0xe0, 0x89, 0x2b, 140);
constexpr double kRadius = 3.0;
constexpr double kBorder = 2.0;

// Draws a 16:9-ish picture so it covers the rect (crop, no stretching).
void drawCover(QPainter& p, const QRectF& r, const QPixmap& pix) {
    double scale = std::max(r.width() / pix.width(), r.height() / pix.height());
    double w = r.width() / scale, h = r.height() / scale;
    p.drawPixmap(r, pix, QRectF((pix.width() - w) / 2, (pix.height() - h) / 2, w, h));
}

} // namespace

// ── PictureButton ────────────────────────────────────────────

PictureButton::PictureButton(QWidget* parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PictureButton::setLit(bool lit) {
    if (m_lit == lit) return;
    m_lit = lit;
    update();
}

void PictureButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QRectF r = QRectF(rect());
    QPainterPath clip;
    clip.addRoundedRect(r, kRadius, kRadius);
    p.save();
    p.setClipPath(clip);
    paintContent(p, r);
    p.restore();

    QRectF border = r.adjusted(kBorder / 2, kBorder / 2, -kBorder / 2, -kBorder / 2);
    QPen pen(Qt::transparent, kBorder);
    if (m_lit) pen.setColor(kLit);
    else if (underMouse()) pen.setColor(kHover);
    else if (isEmptySlot()) {
        pen.setColor(QColor("#333333"));
        pen.setStyle(Qt::DashLine);
    }
    if (pen.color().alpha() == 0) return;
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(border, kRadius, kRadius);
}

// ── CamButton ────────────────────────────────────────────────

CamButton::CamButton(const PipSettings* settings, BMDSwitcherInputId input, QWidget* parent)
    : PictureButton(parent), m_settings(settings), m_input(input) {}

void CamButton::paintContent(QPainter& p, const QRectF& r) {
    QPixmap pix = m_settings->picture(m_input);
    if (pix.isNull()) p.fillRect(r, m_settings->color(m_input));
    else drawCover(p, r, pix);

    // The picture identifies the camera; the name shows without one, or
    // always when "Show names on camera buttons" is on.
    if (!pix.isNull() && !m_settings->showNames()) return;
    QRectF strip(r.left(), r.bottom() - 14, r.width(), 14);
    p.fillRect(strip, QColor(0, 0, 0, 150));
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(10);
    p.setFont(font);
    p.setPen(Qt::white);
    QString name = p.fontMetrics().elidedText(m_settings->name(m_input), Qt::ElideRight,
                                               static_cast<int>(strip.width()) - 4);
    p.drawText(strip, Qt::AlignCenter, name);
}

// ── PresetButton ─────────────────────────────────────────────

PresetButton::PresetButton(int number, QWidget* parent)
    : PictureButton(parent), m_number(number) {}

void PresetButton::setPreset(const PipPreset& preset) {
    m_valid = preset.valid;
    m_thumbnail = preset.valid ? QPixmap::fromImage(preset.thumbnail) : QPixmap();
    update();
}

void PresetButton::paintContent(QPainter& p, const QRectF& r) {
    if (m_valid && !m_thumbnail.isNull()) drawCover(p, r, m_thumbnail);
    else p.fillRect(r, QColor("#181818"));
    if (!m_showNumber) return;

    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(9);
    p.setFont(font);
    QString text = QString::number(m_number);
    QRectF badge = p.fontMetrics().boundingRect(text).adjusted(-3, 0, 3, 0);
    badge.moveTopLeft(r.topLeft() + QPointF(3, 3));
    if (m_valid) {
        p.fillRect(badge, QColor(0, 0, 0, 165));
        p.setPen(Qt::white);
    } else {
        p.setPen(QColor("#555555"));
    }
    p.drawText(badge, Qt::AlignCenter, text);
}

// ── PipSpinBox ───────────────────────────────────────────────

PipSpinBox::PipSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    // StrongFocus: the wheel must not grab focus (and change values) in passing.
    setFocusPolicy(Qt::StrongFocus);
    setKeyboardTracking(false);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setMinimumWidth(30);
}

void PipSpinBox::wheelEvent(QWheelEvent* e) {
    if (!hasFocus()) {
        e->ignore();   // let the dock scroll instead
        return;
    }
    // Shift+wheel arrives as a horizontal delta on some systems.
    int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    if (delta != 0) stepBy((delta > 0 ? 1 : -1) * ((e->modifiers() & Qt::ShiftModifier) ? 10 : 1));
    e->accept();
}

void PipSpinBox::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && (e->modifiers() & Qt::ShiftModifier)) {
        stepBy(e->key() == Qt::Key_Up ? 10 : -10);
        return;
    }
    QDoubleSpinBox::keyPressEvent(e);
}
