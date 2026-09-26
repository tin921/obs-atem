#pragma once

#include <windows.h>
#include <oleauto.h>
#include <atomic>
#include <functional>
#include <string>
#include <utility>

// Generic COM sink for the BMDSwitcherAPI *Callback interfaces. Every one of
// them is IUnknown + a single Notify(...) method; only the argument list
// differs, so Args... is Notify's exact parameter list.
//
// Notify runs on a BMD SDK thread. The handler must not touch Qt widgets
// directly — marshal to the UI thread (AtemSession does this).
//
// Ownership follows COM rules: created with refcount 1, the SDK AddRefs in
// AddCallback and Releases in RemoveCallback; the creator calls Release()
// once after RemoveCallback.
template <class Iface, class... Args>
class BmdCallback : public Iface {
public:
    explicit BmdCallback(std::function<void(Args...)> handler)
        : m_handler(std::move(handler)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == __uuidof(Iface)) {
            *ppv = static_cast<Iface*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = --m_refCount;
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE Notify(Args... args) override {
        if (m_handler) m_handler(args...);
        return S_OK;
    }

private:
    virtual ~BmdCallback() = default;

    std::function<void(Args...)> m_handler;
    std::atomic<ULONG> m_refCount{1};
};

// Releases a COM pointer and nulls it.
template <class T>
inline void bmdRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Converts (and frees) a BSTR returned by the SDK to UTF-8, which is what
// QString::fromStdString expects. Names typed in ATEM Software Control can
// contain non-ASCII characters, so the ANSI conversion of _bstr_t is not enough.
inline std::string bmdTakeString(BSTR bstr) {
    std::string out;
    if (!bstr) return out;
    int wlen = static_cast<int>(SysStringLen(bstr));
    if (wlen > 0) {
        int len = WideCharToMultiByte(CP_UTF8, 0, bstr, wlen, nullptr, 0, nullptr, nullptr);
        out.resize(static_cast<size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, bstr, wlen, out.data(), len, nullptr, nullptr);
    }
    SysFreeString(bstr);
    return out;
}

// UTF-8 → newly allocated BSTR; the caller frees it with SysFreeString.
inline BSTR bmdMakeString(const std::string& utf8) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    BSTR bstr = SysAllocStringLen(nullptr, static_cast<UINT>(wlen));
    if (bstr && wlen > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), bstr, wlen);
    return bstr;
}
