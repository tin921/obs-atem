#include "pip-presets.h"

#include <cmath>

namespace {

// Values read back from the switcher are rounded to its own resolution.
constexpr double kMatchTolerance = 0.005;

bool same(double a, double b) { return std::abs(a - b) < kMatchTolerance; }

} // namespace

bool pipPresetMatches(const PipPreset& p, const AtemPipState& s) {
    return p.valid && (!p.onAir || (s.isDVE && !s.dveUsedByTransition)) &&
           s.programInput == p.programInput && s.pipInput == p.pipInput && s.onAir == p.onAir &&
           same(s.positionX, p.positionX) && same(s.positionY, p.positionY) &&
           same(s.sizeX, p.sizeX) && same(s.sizeY, p.sizeY) &&
           same(s.cropTop, p.cropTop) && same(s.cropBottom, p.cropBottom) &&
           same(s.cropLeft, p.cropLeft) && same(s.cropRight, p.cropRight) &&
           s.cropEnabled == p.cropEnabled;
}

bool pipPresetChangesBox(const PipPreset& p, const AtemPipState& s) {
    return s.pipInput != p.pipInput ||
           !same(s.positionX, p.positionX) || !same(s.positionY, p.positionY) ||
           !same(s.sizeX, p.sizeX) || !same(s.sizeY, p.sizeY) ||
           !same(s.cropTop, p.cropTop) || !same(s.cropBottom, p.cropBottom) ||
           !same(s.cropLeft, p.cropLeft) || !same(s.cropRight, p.cropRight) ||
           s.cropEnabled != p.cropEnabled;
}

QString pipPresetBlocked(const PipPreset& p, const AtemPipState& s) {
    if (!p.valid || !p.onAir) return {};
    if (!s.isDVE)
        return "Upstream key 1 is not a DVE key, so this view's PiP can't go on air. "
               "Use \"Set key type to DVE\" in the ATEM PiP panel.";
    if (s.dveUsedByTransition)
        return "The DVE transition is using the DVE, so this view's PiP can't go on air. "
               "Use \"Switch next transition to Mix\" in the ATEM PiP panel.";
    return {};
}

PipRecallResult recallPipPreset(AtemPip& pip, const PipPreset& p, const AtemPipState& s) {
    PipRecallResult r;
    if (!p.valid) return { false, "This button has no saved view." };
    if (QString why = pipPresetBlocked(p, s); !why.isEmpty()) return { false, why };
    auto step = [&r](bool ok, const char* what) {
        if (!ok) r = { false, QString("The switcher refused %1; the rest of the view was not sent.").arg(what) };
        return ok;
    };
    bool onAir = s.onAir;
    if (onAir && pipPresetChangesBox(p, s)) {
        if (!step(pip.setOnAir(false), "taking the PiP off air")) return r;
        onAir = false;
    }
    if (s.pipInput != p.pipInput && !step(pip.setPipInput(p.pipInput), "the PiP camera")) return r;
    if (s.programInput != p.programInput && !step(pip.setProgramInput(p.programInput), "the main camera")) return r;
    const std::pair<AtemPipField, std::pair<double, double>> values[] = {
        { AtemPipField::PositionX, { s.positionX, p.positionX } },
        { AtemPipField::PositionY, { s.positionY, p.positionY } },
        { AtemPipField::SizeX, { s.sizeX, p.sizeX } },
        { AtemPipField::SizeY, { s.sizeY, p.sizeY } },
        { AtemPipField::CropTop, { s.cropTop, p.cropTop } },
        { AtemPipField::CropBottom, { s.cropBottom, p.cropBottom } },
        { AtemPipField::CropLeft, { s.cropLeft, p.cropLeft } },
        { AtemPipField::CropRight, { s.cropRight, p.cropRight } },
    };
    for (const auto& [field, v] : values)
        if (v.first != v.second && !step(pip.setValue(field, v.second), "a position, size or crop value")) return r;
    if (s.cropEnabled != p.cropEnabled && !step(pip.setCropEnabled(p.cropEnabled), "the crop")) return r;
    if (onAir != p.onAir) step(pip.setOnAir(p.onAir), p.onAir ? "putting the PiP on air" : "taking the PiP off air");
    return r;
}
