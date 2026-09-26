#pragma once

#include <windows.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "BMDSwitcherAPI.h"

// ── Picture-in-picture on the ATEM Mini ──────────────────────
//
// The ATEM Mini has no SuperSource; PiP is upstream key 1 of M/E 1 set to the
// DVE key type:
//   main input  = M/E 1 program input      (IBMDSwitcherMixEffectBlock)
//   PiP input   = key fill source          (IBMDSwitcherKey)
//   PiP visible = key on air               (IBMDSwitcherKey)
//   position    = key fly position/size    (IBMDSwitcherKeyFlyParameters)
//   crop        = DVE mask                 (IBMDSwitcherKeyDVEParameters)
//
// Units are the switcher's own. The SDK manual gives no numeric ranges; the
// panel's value limits live in pip-dock.cpp and must be checked against the
// real device.

struct AtemInputInfo {
    BMDSwitcherInputId id = 0;
    std::string shortName;
    std::string longName;
    BMDSwitcherPortType portType{};
    bool canBeProgram = false;   // selectable as the main (program) input
    bool canBePip = false;       // selectable as the PiP (key fill) source
};

struct AtemPipState {
    bool available = false;      // M/E 1 and upstream key 1 were found
    bool canBeDVE = false;       // false while the one DVE is held elsewhere
    bool dveUsedByTransition = false;  // current or next transition style is DVE
    bool isDVE = false;
    bool onAir = false;

    BMDSwitcherInputId programInput = 0;
    BMDSwitcherInputId pipInput = 0;

    double positionX = 0.0;
    double positionY = 0.0;
    double sizeX = 1.0;
    double sizeY = 1.0;
    bool canScaleUp = false;     // size may exceed 1.0

    bool cropEnabled = false;
    double cropTop = 0.0;
    double cropBottom = 0.0;
    double cropLeft = 0.0;
    double cropRight = 0.0;

    bool borderEnabled = false;
};

// Continuous values the panel drives from the preview and number boxes.
enum class AtemPipField {
    PositionX,
    PositionY,
    SizeX,
    SizeY,
    CropTop,
    CropBottom,
    CropLeft,
    CropRight,
};

class AtemPip {
public:
    using ChangeCallback = std::function<void()>;
    using TraceCallback = std::function<void(const std::string&)>;

    AtemPip() = default;
    ~AtemPip();
    AtemPip(const AtemPip&) = delete;
    AtemPip& operator=(const AtemPip&) = delete;

    // Called by AtemController once connected / before disconnecting.
    bool attach(IBMDSwitcher* switcher);
    void detach();
    bool isAttached() const;

    std::vector<AtemInputInfo> inputs() const;
    AtemPipState state() const;

    bool setProgramInput(BMDSwitcherInputId input);
    bool setPipInput(BMDSwitcherInputId input);
    bool setOnAir(bool onAir);
    // Makes upstream key 1 a working DVE key. The ATEM Mini has one DVE,
    // shared with the DVE transition: while a DVE transition holds it, even a
    // DVE key can't move or resize. Then this switches the next transition to
    // Mix and returns false — call again once the switcher has confirmed.
    // Returns true once the key is a DVE key and the DVE is free.
    bool makeDVE();
    bool setValue(AtemPipField field, double value);
    bool setCropEnabled(bool enabled);
    bool setBorderEnabled(bool enabled);
    bool resetPositionAndSize();
    bool resetCrop();

    // Fires on a BMD SDK thread whenever PiP-relevant switcher state changes.
    void setChangeCallback(ChangeCallback cb) { m_onChange = std::move(cb); }
    void setTraceCallback(TraceCallback cb) { m_onTrace = std::move(cb); }
    // Also trace every successful SDK call (atem-harness turns this on).
    void setLogCalls(bool on) { m_logCalls = on; }

private:
    void notifyChanged();
    void trace(const std::string& msg);
    bool check(HRESULT hr, const std::string& call);
    void detachLocked();

    IBMDSwitcherMixEffectBlock*    m_mixEffect = nullptr;
    IBMDSwitcherTransitionParameters* m_transition = nullptr;
    IBMDSwitcherKey*               m_key = nullptr;
    IBMDSwitcherKeyFlyParameters*  m_fly = nullptr;
    IBMDSwitcherKeyDVEParameters*  m_dve = nullptr;
    std::vector<IBMDSwitcherInput*> m_inputs;

    IBMDSwitcherMixEffectBlockCallback*    m_mixEffectCallback = nullptr;
    IBMDSwitcherTransitionParametersCallback* m_transitionCallback = nullptr;
    IBMDSwitcherKeyCallback*               m_keyCallback = nullptr;
    IBMDSwitcherKeyFlyParametersCallback*  m_flyCallback = nullptr;
    IBMDSwitcherKeyDVEParametersCallback*  m_dveCallback = nullptr;
    IBMDSwitcherInputCallback*             m_inputCallback = nullptr;

    mutable std::mutex m_mutex;
    ChangeCallback m_onChange;
    TraceCallback m_onTrace;
    bool m_logCalls = false;
};
