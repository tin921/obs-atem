#include "atem-controller.h"
#include <comdef.h>
#include <sstream>
#include <obs-module.h>

// ── MacroPlayerCallback ──────────────────────────────────────

MacroPlayerCallback::MacroPlayerCallback(OnChangeFunc onChange)
    : m_onChange(std::move(onChange)) {}

HRESULT MacroPlayerCallback::QueryInterface(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IBMDSwitcherMacroPoolCallback) {
        *ppv = static_cast<IBMDSwitcherMacroPoolCallback*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG MacroPlayerCallback::AddRef() {
    return ++m_refCount;
}

ULONG MacroPlayerCallback::Release() {
    ULONG count = --m_refCount;
    if (count == 0) delete this;
    return count;
}

HRESULT MacroPlayerCallback::Notify(
    BMDSwitcherMacroPoolEventType /*eventType*/,
    unsigned int /*index*/,
    IBMDSwitcherTransferMacro* /*macroTransfer*/)
{
    if (m_onChange) m_onChange();
    return S_OK;
}

// ── AtemController ───────────────────────────────────────────

AtemController::AtemController() {
    // COM should already be initialized by OBS (STA), but ensure MTA for our thread
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Create the switcher discovery instance
    HRESULT hr = CoCreateInstance(
        CLSID_CBMDSwitcherDiscovery, nullptr,
        CLSCTX_ALL,
        IID_IBMDSwitcherDiscovery,
        reinterpret_cast<void**>(&m_discovery)
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create BMDSwitcherDiscovery. "
                      "Is the ATEM Software installed?";
        m_discovery = nullptr;
    }
}

AtemController::~AtemController() {
    disconnect();
    if (m_discovery) {
        m_discovery->Release();
        m_discovery = nullptr;
    }
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

    blog(LOG_INFO, "[ATEM Macros] connectToAddress: '%s'", address.c_str());

    if (!m_discovery) {
        m_lastError = "BMD SDK not available. Install ATEM Software Control.";
        blog(LOG_ERROR, "[ATEM Macros] connectToAddress: no discovery object");
        return false;
    }

    cleanup();

    m_state = AtemState::Connecting;
    m_address = address.empty() ? "USB" : address;
    if (m_onStateChange) m_onStateChange(m_state);

    _bstr_t bstrAddr(address.c_str());
    BMDSwitcherConnectToFailure failReason = bmdSwitcherConnectToFailureNoResponse;

    blog(LOG_INFO, "[ATEM Macros] connectToAddress: calling ConnectTo...");
    HRESULT hr = m_discovery->ConnectTo(bstrAddr, &m_switcher, &failReason);
    blog(LOG_INFO, "[ATEM Macros] connectToAddress: ConnectTo hr=0x%08X switcher=%p failReason=%d",
         (unsigned)hr, (void*)m_switcher, (int)failReason);

    if (FAILED(hr) || !m_switcher) {
        m_state = AtemState::Disconnected;
        switch (failReason) {
        case bmdSwitcherConnectToFailureNoResponse:
            m_lastError = "No response from ATEM. Check USB/network connection.";
            break;
        case bmdSwitcherConnectToFailureIncompatibleFirmware:
            m_lastError = "Incompatible firmware. Update ATEM Software Control.";
            break;
        default:
            m_lastError = "Connection failed (code: " + std::to_string(failReason) + ")";
            break;
        }
        blog(LOG_ERROR, "[ATEM Macros] connectToAddress: failed - %s", m_lastError.c_str());
        if (m_onStateChange) m_onStateChange(m_state);
        return false;
    }

    // Get model name
    BSTR productName = nullptr;
    if (SUCCEEDED(m_switcher->GetProductName(&productName)) && productName) {
        _bstr_t nameWrapper(productName, false);
        m_modelName = static_cast<const char*>(nameWrapper);
    }
    blog(LOG_INFO, "[ATEM Macros] connectToAddress: model='%s'", m_modelName.c_str());

    // Get macro pool interface
    blog(LOG_INFO, "[ATEM Macros] connectToAddress: getting MacroPool...");
    hr = m_switcher->QueryInterface(
        IID_IBMDSwitcherMacroPool,
        reinterpret_cast<void**>(&m_macroPool)
    );
    blog(LOG_INFO, "[ATEM Macros] connectToAddress: MacroPool hr=0x%08X", (unsigned)hr);
    if (FAILED(hr)) {
        m_lastError = "Failed to get macro pool interface.";
        blog(LOG_ERROR, "[ATEM Macros] connectToAddress: %s", m_lastError.c_str());
        cleanup();
        if (m_onStateChange) m_onStateChange(m_state);
        return false;
    }

    // Get macro control interface
    blog(LOG_INFO, "[ATEM Macros] connectToAddress: getting MacroControl...");
    hr = m_switcher->QueryInterface(
        IID_IBMDSwitcherMacroControl,
        reinterpret_cast<void**>(&m_macroControl)
    );
    blog(LOG_INFO, "[ATEM Macros] connectToAddress: MacroControl hr=0x%08X", (unsigned)hr);
    if (FAILED(hr)) {
        m_lastError = "Failed to get macro control interface.";
        blog(LOG_ERROR, "[ATEM Macros] connectToAddress: %s", m_lastError.c_str());
        cleanup();
        if (m_onStateChange) m_onStateChange(m_state);
        return false;
    }

    blog(LOG_INFO, "[ATEM Macros] connectToAddress: registering callback...");
    m_poolCallback = new MacroPlayerCallback([this]() {
        if (m_onMacroUpdate) m_onMacroUpdate();
    });
    m_macroPool->AddCallback(m_poolCallback);

    m_state = AtemState::Connected;
    m_lastError.clear();
    blog(LOG_INFO, "[ATEM Macros] connectToAddress: connected successfully");
    if (m_onStateChange) m_onStateChange(m_state);
    return true;
}

void AtemController::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    cleanup();
    if (m_onStateChange) m_onStateChange(m_state);
}

void AtemController::cleanup() {
    if (m_macroPool && m_poolCallback) {
        m_macroPool->RemoveCallback(m_poolCallback);
        m_poolCallback->Release();
        m_poolCallback = nullptr;
    }
    if (m_macroControl) { m_macroControl->Release(); m_macroControl = nullptr; }
    if (m_macroPool)    { m_macroPool->Release();    m_macroPool = nullptr; }
    if (m_switcher)     { m_switcher->Release();     m_switcher = nullptr; }

    m_state = AtemState::Disconnected;
    m_modelName.clear();
    m_address.clear();
}

std::vector<AtemMacroInfo> AtemController::getMacros() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AtemMacroInfo> result;

    if (!m_macroPool) return result;

    // Get the number of macro slots
    uint32_t maxMacros = 0;
    if (FAILED(m_macroPool->GetMaxCount(&maxMacros))) return result;

    for (uint32_t i = 0; i < maxMacros; i++) {
        BOOL valid = FALSE;
        if (FAILED(m_macroPool->IsValid(i, &valid))) continue;
        if (!valid) continue;

        AtemMacroInfo info;
        info.index = i;
        info.isUsed = true;

        // Get name
        BSTR name = nullptr;
        if (SUCCEEDED(m_macroPool->GetName(i, &name)) && name) {
            _bstr_t nameWrapper(name, false);
            info.name = static_cast<const char*>(nameWrapper);
        }
        if (info.name.empty()) {
            info.name = "Macro " + std::to_string(i + 1);
        }

        // Get description
        BSTR desc = nullptr;
        if (SUCCEEDED(m_macroPool->GetDescription(i, &desc)) && desc) {
            _bstr_t descWrapper(desc, false);
            info.description = static_cast<const char*>(descWrapper);
        }

        // Check for unsupported ops
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
    return SUCCEEDED(m_macroControl->Run(index));
}

bool AtemController::stopMacro() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_macroControl) return false;
    return SUCCEEDED(m_macroControl->StopRunning());
}

bool AtemController::isRunning() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_macroControl) return false;

    BMDSwitcherMacroRunStatus status;
    BOOL loop = FALSE;
    unsigned int idx = 0;
    if (SUCCEEDED(m_macroControl->GetRunStatus(&status, &loop, &idx))) {
        return status == bmdSwitcherMacroRunStatusRunning;
    }
    return false;
}

int AtemController::runningMacroIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_macroControl) return -1;

    BMDSwitcherMacroRunStatus status;
    BOOL loop = FALSE;
    unsigned int index = 0;
    if (SUCCEEDED(m_macroControl->GetRunStatus(&status, &loop, &index))) {
        if (status == bmdSwitcherMacroRunStatusRunning) {
            return static_cast<int>(index);
        }
    }
    return -1;
}
