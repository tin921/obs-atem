#include "atem-controller.h"
#include "bmd-util.h"
#include "obs-log.h"

#include <cstdarg>
#include <cstdio>

namespace {

using MacroPoolCallback = BmdCallback<IBMDSwitcherMacroPoolCallback,
                                      BMDSwitcherMacroPoolEventType, unsigned int, IBMDSwitcherTransferMacro*>;
using SwitcherCallback  = BmdCallback<IBMDSwitcherCallback, BMDSwitcherEventType, BMDSwitcherVideoMode>;

std::string connectFailureText(BMDSwitcherConnectToFailure reason) {
    switch (reason) {
    case bmdSwitcherConnectToFailureNoResponse:
        return "No response from ATEM. Check USB/network connection.";
    case bmdSwitcherConnectToFailureIncompatibleFirmware:
        return "Incompatible firmware. Update the ATEM (ATEM Setup) or ATEM Software Control.";
    case bmdSwitcherConnectToFailureCorruptData:
        return "Corrupt data received from ATEM.";
    case bmdSwitcherConnectToFailureStateSync:
    case bmdSwitcherConnectToFailureStateSyncTimedOut:
        return "ATEM answered but the initial state sync failed. Try reconnecting.";
    default:
        return "Connection failed (code: " + std::to_string(static_cast<long>(reason)) + ")";
    }
}

} // namespace

// ── AtemController ───────────────────────────────────────────

AtemController::AtemController() {
    // Inside OBS the UI thread is already an STA, so this returns
    // RPC_E_CHANGED_MODE and changes nothing. atem-cli has no COM yet.
    m_comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    HRESULT hr = CoCreateInstance(
        __uuidof(CBMDSwitcherDiscovery), nullptr,
        CLSCTX_ALL,
        __uuidof(IBMDSwitcherDiscovery),
        reinterpret_cast<void**>(&m_discovery)
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create BMDSwitcherDiscovery. "
                      "Is the ATEM Software installed?";
        m_discovery = nullptr;
    }

    m_pip.setTraceCallback([this](const std::string& msg) { trace("%s", msg.c_str()); });
}

AtemController::~AtemController() {
    shutdown();
    if (m_comInitialized) CoUninitialize();
}

void AtemController::setTraceCallback(TraceCallback cb) {
    m_onTrace = std::move(cb);
}

void AtemController::trace(const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    blog(LOG_INFO, "%s", buf);
    if (m_onTrace) {
        m_onTrace(std::string(buf));
    }
}

void AtemController::notifyState() {
    if (m_onStateChange) m_onStateChange(m_state);
}

bool AtemController::connectUSB() {
    // USB connection: pass empty string — the SDK auto-detects USB-connected ATEMs
    return connectToAddress("");
}

bool AtemController::connectIP(const std::string& address) {
    return connectToAddress(address);
}

bool AtemController::connectToAddress(const std::string& address) {
    std::lock_guard<std::mutex> lock(m_mutex);

    trace("[ATEM Macros] connectToAddress: '%s'", address.c_str());

    if (!m_discovery) {
        m_lastError = "BMD SDK not available. Install ATEM Software Control.";
        trace("[ATEM Macros] ERROR: no discovery object");
        return false;
    }

    cleanup();

    m_state = AtemState::Connecting;
    m_address = address.empty() ? "USB" : address;
    notifyState();

    BSTR bstrAddr = bmdMakeString(address);
    BMDSwitcherConnectToFailure failReason = bmdSwitcherConnectToFailureNoResponse;

    trace("[ATEM Macros] calling ConnectTo...");
    HRESULT hr = m_discovery->ConnectTo(bstrAddr, &m_switcher, &failReason);
    SysFreeString(bstrAddr);
    trace("[ATEM Macros] ConnectTo hr=0x%08X switcher=%p failReason=0x%08X",
          (unsigned)hr, (void*)m_switcher, (unsigned)failReason);

    if (FAILED(hr) || !m_switcher) {
        cleanup();
        m_lastError = connectFailureText(failReason);
        trace("[ATEM Macros] ERROR: %s", m_lastError.c_str());
        notifyState();
        return false;
    }

    BSTR productName = nullptr;
    if (SUCCEEDED(m_switcher->GetProductName(&productName))) {
        m_modelName = bmdTakeString(productName);
    }
    trace("[ATEM Macros] model='%s'", m_modelName.c_str());

    hr = m_switcher->QueryInterface(__uuidof(IBMDSwitcherMacroPool),
                                    reinterpret_cast<void**>(&m_macroPool));
    if (FAILED(hr)) {
        cleanup();
        m_lastError = "Failed to get macro pool interface.";
        trace("[ATEM Macros] ERROR: %s hr=0x%08X", m_lastError.c_str(), (unsigned)hr);
        notifyState();
        return false;
    }

    hr = m_switcher->QueryInterface(__uuidof(IBMDSwitcherMacroControl),
                                    reinterpret_cast<void**>(&m_macroControl));
    if (FAILED(hr)) {
        cleanup();
        m_lastError = "Failed to get macro control interface.";
        trace("[ATEM Macros] ERROR: %s hr=0x%08X", m_lastError.c_str(), (unsigned)hr);
        notifyState();
        return false;
    }

    m_poolCallback = new MacroPoolCallback(
        [this](BMDSwitcherMacroPoolEventType, unsigned int, IBMDSwitcherTransferMacro*) {
            if (m_onMacroUpdate) m_onMacroUpdate();
        });
    m_macroPool->AddCallback(m_poolCallback);

    m_switcherCallback = new SwitcherCallback(
        [this](BMDSwitcherEventType type, BMDSwitcherVideoMode) {
            if (type == bmdSwitcherEventTypeDisconnected && m_onConnectionLost)
                m_onConnectionLost();
        });
    m_switcher->AddCallback(m_switcherCallback);

    // PiP is optional: a switcher without a DVE keyer still runs macros.
    m_pip.attach(m_switcher);

    m_state = AtemState::Connected;
    m_lastError.clear();
    trace("[ATEM Macros] connected successfully");
    notifyState();
    return true;
}

void AtemController::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    bool wasConnected = m_state != AtemState::Disconnected;
    cleanup();
    if (wasConnected) notifyState();
}

void AtemController::handleConnectionLost() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == AtemState::Disconnected) return;
    cleanup();
    m_lastError = "Connection to the ATEM was lost.";
    trace("[ATEM Macros] %s", m_lastError.c_str());
    notifyState();
}

void AtemController::shutdown() {
    disconnect();
    std::lock_guard<std::mutex> lock(m_mutex);
    bmdRelease(m_discovery);
}

void AtemController::cleanup() {
    m_pip.detach();

    if (m_switcher && m_switcherCallback) m_switcher->RemoveCallback(m_switcherCallback);
    if (m_macroPool && m_poolCallback) m_macroPool->RemoveCallback(m_poolCallback);
    bmdRelease(m_switcherCallback);
    bmdRelease(m_poolCallback);

    bmdRelease(m_macroControl);
    bmdRelease(m_macroPool);
    bmdRelease(m_switcher);

    m_state = AtemState::Disconnected;
    m_modelName.clear();
    m_address.clear();
}

std::vector<AtemMacroInfo> AtemController::getMacros() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AtemMacroInfo> result;

    if (!m_macroPool) return result;

    uint32_t maxMacros = 0;
    if (FAILED(m_macroPool->GetMaxCount(&maxMacros))) return result;

    for (uint32_t i = 0; i < maxMacros; i++) {
        BOOL valid = FALSE;
        if (FAILED(m_macroPool->IsValid(i, &valid)) || !valid) continue;

        AtemMacroInfo info;
        info.index = i;
        info.isUsed = true;

        BSTR name = nullptr;
        if (SUCCEEDED(m_macroPool->GetName(i, &name))) {
            info.name = bmdTakeString(name);
        }
        if (info.name.empty()) {
            info.name = "Macro " + std::to_string(i + 1);
        }

        BSTR desc = nullptr;
        if (SUCCEEDED(m_macroPool->GetDescription(i, &desc))) {
            info.description = bmdTakeString(desc);
        }

        BOOL hasUnsupported = FALSE;
        m_macroPool->HasUnsupportedOps(i, &hasUnsupported);
        info.hasUnsupportedOps = (hasUnsupported != FALSE);

        result.push_back(std::move(info));
    }

    return result;
}

bool AtemController::runMacro(uint32_t index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_macroControl) return false;
    HRESULT hr = m_macroControl->Run(index);
    if (FAILED(hr)) trace("[ATEM Macros] Run(%u) failed hr=0x%08X", index, (unsigned)hr);
    return SUCCEEDED(hr);
}

bool AtemController::stopMacro() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_macroControl) return false;
    HRESULT hr = m_macroControl->StopRunning();
    if (FAILED(hr)) trace("[ATEM Macros] StopRunning failed hr=0x%08X", (unsigned)hr);
    return SUCCEEDED(hr);
}

AtemMacroRunStatus AtemController::runStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    AtemMacroRunStatus result;
    if (!m_macroControl) return result;

    BMDSwitcherMacroRunStatus status = bmdSwitcherMacroRunStatusIdle;
    BOOL loop = FALSE;
    unsigned int index = 0;
    if (SUCCEEDED(m_macroControl->GetRunStatus(&status, &loop, &index)) &&
        status != bmdSwitcherMacroRunStatusIdle) {
        result.index = static_cast<int>(index);
        result.waitingForUser = status == bmdSwitcherMacroRunStatusWaitingForUser;
    }
    return result;
}
