#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

// BMD COM interfaces, from the ATEM SDK include folder
#include "BMDSwitcherAPI.h"

#include "atem-pip.h"

// ── Data Types ───────────────────────────────────────────────

struct AtemMacroInfo {
    uint32_t index;
    std::string name;
    std::string description;
    bool isUsed;
    bool hasUnsupportedOps;
};

struct AtemMacroRunStatus {
    int index = -1;              // -1 when idle
    bool waitingForUser = false; // macro paused on a "user wait" step
};

enum class AtemState {
    Disconnected,
    Connecting,
    Connected
};

// ── Main ATEM Controller ─────────────────────────────────────
//
// Owns the connection to one switcher and the macro API. The PiP API lives in
// AtemPip (see pip()), attached to the same switcher while connected.
//
// Qt-free on purpose so atem-cli can use it. Callbacks may fire on a BMD SDK
// thread; UI code must marshal them (AtemSession does).

class AtemController {
public:
    using StateChangeCallback = std::function<void(AtemState)>;
    using MacroUpdateCallback = std::function<void()>;
    using ConnectionLostCallback = std::function<void()>;
    using TraceCallback = std::function<void(const std::string&)>;

    AtemController();
    ~AtemController();

    // Connection
    bool connectUSB();
    bool connectIP(const std::string& address);
    void disconnect();
    // Called after the switcher reported it disconnected (cable pulled, power).
    void handleConnectionLost();
    // Release every COM object, including the discovery object. Call before
    // COM is torn down (OBS exit); the controller cannot connect afterwards.
    void shutdown();

    AtemState state() const { return m_state; }
    std::string connectedAddress() const { return m_address; }
    std::string modelName() const { return m_modelName; }
    std::string lastError() const { return m_lastError; }

    // Macros
    std::vector<AtemMacroInfo> getMacros();
    bool runMacro(uint32_t index);
    bool stopMacro();
    AtemMacroRunStatus runStatus() const;

    // Picture-in-picture (upstream key 1 as a DVE)
    AtemPip& pip() { return m_pip; }

    // Callbacks
    void setStateChangeCallback(StateChangeCallback cb) { m_onStateChange = std::move(cb); }
    void setMacroUpdateCallback(MacroUpdateCallback cb) { m_onMacroUpdate = std::move(cb); }
    void setConnectionLostCallback(ConnectionLostCallback cb) { m_onConnectionLost = std::move(cb); }
    void setTraceCallback(TraceCallback cb);

private:
    void trace(const char* format, ...);
    bool connectToAddress(const std::string& address);
    void cleanup();
    void notifyState();

    IBMDSwitcherDiscovery*    m_discovery = nullptr;
    IBMDSwitcher*             m_switcher = nullptr;
    IBMDSwitcherMacroPool*    m_macroPool = nullptr;
    IBMDSwitcherMacroControl* m_macroControl = nullptr;

    IBMDSwitcherMacroPoolCallback* m_poolCallback = nullptr;
    IBMDSwitcherCallback*          m_switcherCallback = nullptr;

    AtemPip m_pip;

    bool m_comInitialized = false;
    AtemState m_state = AtemState::Disconnected;
    std::string m_address;
    std::string m_modelName;
    std::string m_lastError;
    mutable std::mutex m_mutex;

    StateChangeCallback m_onStateChange;
    MacroUpdateCallback m_onMacroUpdate;
    ConnectionLostCallback m_onConnectionLost;
    TraceCallback m_onTrace;
};
