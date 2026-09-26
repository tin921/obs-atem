#pragma once

#include <QAbstractButton>
#include <QDoubleSpinBox>

#include "pip-settings.h"

// Button that shows a picture (or colour) with a fixed 2px border:
// translucent orange on hover, solid orange when lit. The border never
// changes the button's size.
class PictureButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit PictureButton(QWidget* parent = nullptr);

    void setLit(bool lit);
    bool isLit() const { return m_lit; }

    QSize sizeHint() const override { return { 64, 36 }; }
    QSize minimumSizeHint() const override { return { 32, 18 }; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    virtual void paintContent(QPainter& p, const QRectF& r) = 0;
    virtual bool isEmptySlot() const { return false; }

private:
    bool m_lit = false;
};

// One of the four camera buttons (main row or PiP row).
class CamButton : public PictureButton {
public:
    CamButton(const PipSettings* settings, BMDSwitcherInputId input, QWidget* parent = nullptr);
    BMDSwitcherInputId input() const { return m_input; }

protected:
    void paintContent(QPainter& p, const QRectF& r) override;

private:
    const PipSettings* m_settings;
    BMDSwitcherInputId m_input;
};

// A preset button: thumbnail of the saved program, number badge.
class PresetButton : public PictureButton {
public:
    PresetButton(int number, QWidget* parent = nullptr);
    void setPreset(const PipPreset& preset);

protected:
    void paintContent(QPainter& p, const QRectF& r) override;
    bool isEmptySlot() const override { return !m_valid; }

private:
    int m_number;
    bool m_valid = false;
    QPixmap m_thumbnail;
};

// Number box: typed values commit on Enter / focus-out; arrow keys and the
// mouse wheel step it, the wheel only while the box has focus so scrolling
// the dock never changes a value by accident. Shift = 10 steps.
class PipSpinBox : public QDoubleSpinBox {
public:
    explicit PipSpinBox(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
};
