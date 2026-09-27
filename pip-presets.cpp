#include "pip-presets.h"

#include <cmath>

namespace {

// Values read back from the switcher are rounded to its own resolution.
constexpr double kMatchTolerance = 0.005;

bool same(double a, double b) { return std::abs(a - b) < kMatchTolerance; }

} // namespace

bool pipPresetMatches(const PipPreset& p, const AtemPipState& s) {
    return p.valid &&
           s.programInput == p.programInput && s.pipInput == p.pipInput && s.onAir == p.onAir &&
           same(s.positionX, p.positionX) && same(s.positionY, p.positionY) &&
           same(s.sizeX, p.sizeX) && same(s.sizeY, p.sizeY) &&
           same(s.cropTop, p.cropTop) && same(s.cropBottom, p.cropBottom) &&
           same(s.cropLeft, p.cropLeft) && same(s.cropRight, p.cropRight) &&
           s.cropEnabled == p.cropEnabled;
}

void recallPipPreset(AtemPip& pip, const PipPreset& p, const AtemPipState& s) {
    if (!p.valid) return;
    if (s.pipInput != p.pipInput) pip.setPipInput(p.pipInput);
    if (s.programInput != p.programInput) pip.setProgramInput(p.programInput);
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
        if (v.first != v.second) pip.setValue(field, v.second);
    if (s.cropEnabled != p.cropEnabled) pip.setCropEnabled(p.cropEnabled);
    if (s.onAir != p.onAir) pip.setOnAir(p.onAir);
}
