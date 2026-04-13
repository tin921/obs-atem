#pragma once

#include <windows.h>
#include <comutil.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

// Forward-declare BMD COM interfaces (from ATEM SDK headers)
// User must have the SDK installed; these come from BMDSwitcherAPI.h
#include "BMDSwitcherAPI.h"

// ── Data Types ───────────────────────────────────────────────

struct AtemMacroInfo {
    uint32_t index;
    std::string name;
    std::string description;
    bool isUsed;
    bool hasUnsupportedOps;
};

enum class AtemState {
    Disconnected,
    Connecting,
    Connected
};

// ── Callback interface for macro player state changes ────────

class MacroPlayerCallback : public IBMDSwitcherMacroPoolCallback {
public:
    using OnChangeFunc = std::function<void()>;

    MacroPlayerCallback(OnChangeFunc onChange);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IBMDSwitcherMacroPoolCallback
    HRESULT STDMETHODCALLTYPE Notify(
        BMDSwitcherMacroPoolEventType eventType,
        unsigned int index,
        IBMDSwitcherTransferMacro* macroTransfer) override;

private:
    OnChangeFunc m_onChange;
    std::atomic<ULONG> m_refCount{1};
};

// ── Main ATEM Controller ─────────────────────────────────────

class AtemController {
public:
    using StateChangeCallback = std::function<void(AtemState)>;
    using MacroUpdateCallback = std::function<void()>;
    using TraceCallback = std::function<void(const std::string&)>;

    AtemController();
    ~AtemController();

    // Connection
    bool connectUSB();
    bool connectIP(const std::string& address);
    void disconnect();

    AtemState state() const { return m_state; }
    std::string connectedAddress() const { return m_address; }
    std::string modelName() const { return m_modelName; }
    std::string lastError() const { return m_lastError; }

    // Macros
    std::vector<AtemMacroInfo> getMacros();
    bool runMacro(uint32_t index);
    bool stopMacro();
    bool isRunning() const;
    int runningMacroIndex() const;

    // Callbacks
    void setStateChangeCallback(StateChangeCallback cb) { m_onStateChange = cb; }
    void setMacroUpdateCallback(MacroUpdateCallback cb) { m_onMacroUpdate = cb; }
    void setTraceCallback(TraceCallback cb) { m_onTrace = cb; }

private:
    void trace(const char* format, ...);
    bool connectToAddress(const std::string& address);
    void cleanup();

    IBMDSwitcherDiscovery*    m_discovery = nullptr;
    IBMDSwitcher*             m_switcher = nullptr;
    IBMDSwitcherMacroPool*    m_macroPool = nullptr;
    IBMDSwitcherMacroControl* m_macroControl = nullptr;

    MacroPlayerCallback*      m_poolCallback = nullptr;

    AtemState m_state = AtemState::Disconnected;
    std::string m_address;
    std::string m_modelName;
    std::string m_lastError;
    mutable std::mutex m_mutex;

    StateChangeCallback m_onStateChange;
    MacroUpdateCallback m_onMacroUpdate;
    TraceCallback m_onTrace;
};
