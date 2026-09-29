#pragma once

#include "atem-pip.h"
#include "pip-settings.h"

// Saved PiP setups ("views"), shared by the PiP panel (which builds them) and
// the Views panel (which recalls them, like the macro panel runs macros).

// True when the switcher shows exactly this preset (a PiP on air counts
// only as a working DVE key: a luma or chroma key on air is not the view).
bool pipPresetMatches(const PipPreset& preset, const AtemPipState& state);

// Why this preset can't put its PiP on air right now (empty: it can). The
// PiP needs upstream key 1 to be a DVE key, and the DVE itself, which a DVE
// transition can hold.
QString pipPresetBlocked(const PipPreset& preset, const AtemPipState& state);

struct PipRecallResult {
    bool ok = true;
    QString error;   // what stopped the recall; nothing after that step was sent
};

// True when the preset changes the PiP box itself: its camera, place, size or
// crop (not the main camera, not on/off).
bool pipPresetChangesBox(const PipPreset& preset, const AtemPipState& state);

// Sends the switcher only what differs from `current`: sources and geometry
// first, the on-air change last, so the PiP appears already in its new place.
// A PiP that is on air and changes box is taken off air first: the switcher
// applies each change as it arrives, so it would otherwise show the new camera
// in the old box and then jump through position and size.
// Stops at the first command the switcher refuses, so a failed "off air"
// never leaves the new camera in the old box.
PipRecallResult recallPipPreset(AtemPip& pip, const PipPreset& preset, const AtemPipState& current);
