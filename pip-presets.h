#pragma once

#include "atem-pip.h"
#include "pip-settings.h"

// Saved PiP setups ("views"), shared by the PiP panel (which builds them) and
// the Views panel (which recalls them, like the macro panel runs macros).

// True when the switcher shows exactly this preset.
bool pipPresetMatches(const PipPreset& preset, const AtemPipState& state);

// Sends the switcher only what differs from `current`: sources and geometry
// first, the on-air change last, so the PiP appears already in its new place.
void recallPipPreset(AtemPip& pip, const PipPreset& preset, const AtemPipState& current);
