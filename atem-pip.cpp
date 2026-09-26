#include "atem-pip.h"
#include "bmd-util.h"

namespace {

using MixEffectCallback = BmdCallback<IBMDSwitcherMixEffectBlockCallback, BMDSwitcherMixEffectBlockEventType>;
using KeyCallback       = BmdCallback<IBMDSwitcherKeyCallback, BMDSwitcherKeyEventType>;
using FlyCallback       = BmdCallback<IBMDSwitcherKeyFlyParametersCallback,
                                      BMDSwitcherKeyFlyParametersEventType, BMDSwitcherFlyKeyFrame>;
using DveCallback       = BmdCallback<IBMDSwitcherKeyDVEParametersCallback, BMDSwitcherKeyDVEParametersEventType>;
using InputCallback     = BmdCallback<IBMDSwitcherInputCallback, BMDSwitcherInputEventType>;
using TransitionCallback = BmdCallback<IBMDSwitcherTransitionParametersCallback, BMDSwitcherTransitionParametersEventType>;

std::string callText(const char* name, double value) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s(%.3f)", name, value);
    return buf;
}

std::string callText(const char* name, BMDSwitcherInputId value) {
    return std::string(name) + "(" + std::to_string(value) + ")";
}

std::string callText(const char* name, bool value) {
    return std::string(name) + (value ? "(TRUE)" : "(FALSE)");
}

} // namespace

AtemPip::~AtemPip() {
    detach();
}

bool AtemPip::attach(IBMDSwitcher* switcher) {
    std::lock_guard<std::mutex> lock(m_mutex);
    detachLocked();
    if (!switcher) return false;

    // M/E 1 — the ATEM Mini has exactly one.
    IBMDSwitcherMixEffectBlockIterator* meIt = nullptr;
    if (SUCCEEDED(switcher->CreateIterator(__uuidof(IBMDSwitcherMixEffectBlockIterator),
                                           reinterpret_cast<void**>(&meIt)))) {
        meIt->Next(&m_mixEffect);
        meIt->Release();
    }
    if (!m_mixEffect) {
        trace("[ATEM PiP] no mix effect block found");
        return false;
    }

    // The transition shares the ATEM Mini's one DVE with the key.
    m_mixEffect->QueryInterface(__uuidof(IBMDSwitcherTransitionParameters), reinterpret_cast<void**>(&m_transition));

    // Upstream key 1 — the only keyer on the ATEM Mini, and the one with DVE.
    IBMDSwitcherKeyIterator* keyIt = nullptr;
    if (SUCCEEDED(m_mixEffect->CreateIterator(__uuidof(IBMDSwitcherKeyIterator),
                                              reinterpret_cast<void**>(&keyIt)))) {
        keyIt->Next(&m_key);
        keyIt->Release();
    }
    if (m_key) {
        m_key->QueryInterface(__uuidof(IBMDSwitcherKeyFlyParameters), reinterpret_cast<void**>(&m_fly));
        m_key->QueryInterface(__uuidof(IBMDSwitcherKeyDVEParameters), reinterpret_cast<void**>(&m_dve));
    } else {
        trace("[ATEM PiP] no upstream key found");
    }

    IBMDSwitcherInputIterator* inIt = nullptr;
    if (SUCCEEDED(switcher->CreateIterator(__uuidof(IBMDSwitcherInputIterator),
                                           reinterpret_cast<void**>(&inIt)))) {
        IBMDSwitcherInput* input = nullptr;
        while (inIt->Next(&input) == S_OK && input) {
            m_inputs.push_back(input);
            input = nullptr;
        }
        inIt->Release();
    }

    // Transition position/frames events fire every frame during a transition;
    // only program and availability changes matter here.
    m_mixEffectCallback = new MixEffectCallback([this](BMDSwitcherMixEffectBlockEventType type) {
        if (type == bmdSwitcherMixEffectBlockEventTypeProgramInputChanged ||
            type == bmdSwitcherMixEffectBlockEventTypeInputAvailabilityMaskChanged)
            notifyChanged();
    });
    m_mixEffect->AddCallback(m_mixEffectCallback);

    if (m_transition) {
        m_transitionCallback = new TransitionCallback([this](BMDSwitcherTransitionParametersEventType) { notifyChanged(); });
        m_transition->AddCallback(m_transitionCallback);
    }

    if (m_key) {
        m_keyCallback = new KeyCallback([this](BMDSwitcherKeyEventType) { notifyChanged(); });
        m_key->AddCallback(m_keyCallback);
    }
    if (m_fly) {
        m_flyCallback = new FlyCallback([this](BMDSwitcherKeyFlyParametersEventType type, BMDSwitcherFlyKeyFrame) {
            if (type != bmdSwitcherKeyFlyParametersEventTypeRateChanged)
                notifyChanged();
        });
        m_fly->AddCallback(m_flyCallback);
    }
    if (m_dve) {
        m_dveCallback = new DveCallback([this](BMDSwitcherKeyDVEParametersEventType) { notifyChanged(); });
        m_dve->AddCallback(m_dveCallback);
    }
    m_inputCallback = new InputCallback([this](BMDSwitcherInputEventType type) {
        if (type == bmdSwitcherInputEventTypeShortNameChanged ||
            type == bmdSwitcherInputEventTypeLongNameChanged)
            notifyChanged();
    });
    for (auto* input : m_inputs) input->AddCallback(m_inputCallback);

    trace("[ATEM PiP] attached: key=" + std::string(m_key ? "yes" : "no") +
          " fly=" + (m_fly ? "yes" : "no") + " dve=" + (m_dve ? "yes" : "no") +
          " inputs=" + std::to_string(m_inputs.size()));
    return m_key != nullptr;
}

void AtemPip::detach() {
    std::lock_guard<std::mutex> lock(m_mutex);
    detachLocked();
}

void AtemPip::detachLocked() {
    if (m_inputCallback) {
        for (auto* input : m_inputs) input->RemoveCallback(m_inputCallback);
    }
    if (m_dve && m_dveCallback) m_dve->RemoveCallback(m_dveCallback);
    if (m_fly && m_flyCallback) m_fly->RemoveCallback(m_flyCallback);
    if (m_key && m_keyCallback) m_key->RemoveCallback(m_keyCallback);
    if (m_mixEffect && m_mixEffectCallback) m_mixEffect->RemoveCallback(m_mixEffectCallback);
    if (m_transition && m_transitionCallback) m_transition->RemoveCallback(m_transitionCallback);

    bmdRelease(m_inputCallback);
    bmdRelease(m_dveCallback);
    bmdRelease(m_flyCallback);
    bmdRelease(m_keyCallback);
    bmdRelease(m_mixEffectCallback);
    bmdRelease(m_transitionCallback);
    bmdRelease(m_transition);

    for (auto*& input : m_inputs) bmdRelease(input);
    m_inputs.clear();
    bmdRelease(m_dve);
    bmdRelease(m_fly);
    bmdRelease(m_key);
    bmdRelease(m_mixEffect);
}

bool AtemPip::isAttached() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mixEffect != nullptr;
}

std::vector<AtemInputInfo> AtemPip::inputs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AtemInputInfo> result;

    BMDSwitcherInputAvailability fillMask = static_cast<BMDSwitcherInputAvailability>(0);
    if (m_key) m_key->GetFillInputAvailabilityMask(&fillMask);

    for (auto* input : m_inputs) {
        AtemInputInfo info;
        input->GetInputId(&info.id);
        input->GetPortType(&info.portType);

        BSTR name = nullptr;
        if (SUCCEEDED(input->GetShortName(&name))) info.shortName = bmdTakeString(name);
        name = nullptr;
        if (SUCCEEDED(input->GetLongName(&name))) info.longName = bmdTakeString(name);

        BMDSwitcherInputAvailability avail = static_cast<BMDSwitcherInputAvailability>(0);
        input->GetInputAvailability(&avail);
        info.canBeProgram = (avail & bmdSwitcherInputAvailabilityMixEffectBlock0) != 0;
        info.canBePip = (avail & fillMask) != 0;

        if (info.canBeProgram || info.canBePip) result.push_back(std::move(info));
    }
    return result;
}

AtemPipState AtemPip::state() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    AtemPipState s;
    if (!m_mixEffect || !m_key) return s;

    s.available = true;
    m_mixEffect->GetProgramInput(&s.programInput);

    BOOL b = FALSE;
    if (SUCCEEDED(m_key->CanBeDVEKey(&b))) s.canBeDVE = b != FALSE;
    if (m_transition) {
        BMDSwitcherTransitionStyle style{};
        if (SUCCEEDED(m_transition->GetTransitionStyle(&style)) && style == bmdSwitcherTransitionStyleDVE)
            s.dveUsedByTransition = true;
        if (SUCCEEDED(m_transition->GetNextTransitionStyle(&style)) && style == bmdSwitcherTransitionStyleDVE)
            s.dveUsedByTransition = true;
    }
    BMDSwitcherKeyType type{};
    if (SUCCEEDED(m_key->GetType(&type))) s.isDVE = type == bmdSwitcherKeyTypeDVE;
    if (SUCCEEDED(m_key->GetOnAir(&b))) s.onAir = b != FALSE;
    m_key->GetInputFill(&s.pipInput);

    if (m_fly) {
        m_fly->GetPositionX(&s.positionX);
        m_fly->GetPositionY(&s.positionY);
        m_fly->GetSizeX(&s.sizeX);
        m_fly->GetSizeY(&s.sizeY);
        if (SUCCEEDED(m_fly->GetCanScaleUp(&b))) s.canScaleUp = b != FALSE;
    }
    if (m_dve) {
        if (SUCCEEDED(m_dve->GetMasked(&b))) s.cropEnabled = b != FALSE;
        m_dve->GetMaskTop(&s.cropTop);
        m_dve->GetMaskBottom(&s.cropBottom);
        m_dve->GetMaskLeft(&s.cropLeft);
        m_dve->GetMaskRight(&s.cropRight);
        if (SUCCEEDED(m_dve->GetBorderEnabled(&b))) s.borderEnabled = b != FALSE;
    }
    return s;
}

bool AtemPip::setProgramInput(BMDSwitcherInputId input) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mixEffect && check(m_mixEffect->SetProgramInput(input), callText("SetProgramInput", input));
}

bool AtemPip::setPipInput(BMDSwitcherInputId input) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_key && check(m_key->SetInputFill(input), callText("SetInputFill", input));
}

bool AtemPip::setOnAir(bool onAir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_key && check(m_key->SetOnAir(onAir ? TRUE : FALSE), callText("SetOnAir", onAir));
}

bool AtemPip::makeDVE() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_key) return false;
    // A DVE transition holds the one DVE: a DVE key keeps its type but can't
    // fly, and SetType(DVE) is refused. Free it by switching the next
    // transition to Mix; the SDK only sees the DVE free once the switcher
    // confirms, so the caller retries.
    if (m_transition) {
        BMDSwitcherTransitionStyle current{}, next{};
        m_transition->GetTransitionStyle(&current);
        m_transition->GetNextTransitionStyle(&next);
        if (current == bmdSwitcherTransitionStyleDVE || next == bmdSwitcherTransitionStyleDVE) {
            if (next == bmdSwitcherTransitionStyleDVE)
                check(m_transition->SetNextTransitionStyle(bmdSwitcherTransitionStyleMix),
                      "SetNextTransitionStyle(Mix) to free the DVE");
            return false;
        }
    }
    BMDSwitcherKeyType type{};
    if (SUCCEEDED(m_key->GetType(&type)) && type == bmdSwitcherKeyTypeDVE) return true;
    return check(m_key->SetType(bmdSwitcherKeyTypeDVE), "SetType(DVE)");
}

bool AtemPip::setValue(AtemPipField field, double value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    switch (field) {
    case AtemPipField::PositionX:  return m_fly && check(m_fly->SetPositionX(value), callText("SetPositionX", value));
    case AtemPipField::PositionY:  return m_fly && check(m_fly->SetPositionY(value), callText("SetPositionY", value));
    case AtemPipField::SizeX:      return m_fly && check(m_fly->SetSizeX(value), callText("SetSizeX", value));
    case AtemPipField::SizeY:      return m_fly && check(m_fly->SetSizeY(value), callText("SetSizeY", value));
    case AtemPipField::CropTop:    return m_dve && check(m_dve->SetMaskTop(value), callText("SetMaskTop", value));
    case AtemPipField::CropBottom: return m_dve && check(m_dve->SetMaskBottom(value), callText("SetMaskBottom", value));
    case AtemPipField::CropLeft:   return m_dve && check(m_dve->SetMaskLeft(value), callText("SetMaskLeft", value));
    case AtemPipField::CropRight:  return m_dve && check(m_dve->SetMaskRight(value), callText("SetMaskRight", value));
    }
    return false;
}

bool AtemPip::setCropEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dve && check(m_dve->SetMasked(enabled ? TRUE : FALSE), callText("SetMasked", enabled));
}

bool AtemPip::setBorderEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dve && check(m_dve->SetBorderEnabled(enabled ? TRUE : FALSE), callText("SetBorderEnabled", enabled));
}

bool AtemPip::resetPositionAndSize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fly && check(m_fly->ResetDVE(), "ResetDVE()");
}

bool AtemPip::resetCrop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dve && check(m_dve->ResetMask(), "ResetMask()");
}

void AtemPip::notifyChanged() {
    if (m_onChange) m_onChange();
}

void AtemPip::trace(const std::string& msg) {
    if (m_onTrace) m_onTrace(msg);
}

bool AtemPip::check(HRESULT hr, const std::string& call) {
    if (SUCCEEDED(hr)) {
        if (m_logCalls) trace("[ATEM PiP] " + call);
        return true;
    }
    char code[16];
    snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(hr));
    trace("[ATEM PiP] " + call + " failed hr=" + code);
    return false;
}
