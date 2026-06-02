// service/src/hfp_guard.cpp
#include "hfp_guard.h"
#include "com_ptr.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

namespace owb {

// SessionNotifier: COM callback — receives events for new audio sessions.
//
// NOTE: This class uses COM ref-counting + self-delete in Release() rather
// than std::unique_ptr. This is a deliberate exception to the project's
// RAII rule: IAudioSessionManager2::RegisterSessionNotification requires a
// COM object that stays alive until UnregisterSessionNotification is called,
// and the COM lifetime contract (AddRef/Release) is the correct ownership
// model here. Do not convert this to unique_ptr.
class SessionNotifier final : public IAudioSessionNotification {
public:
    explicit SessionNotifier(IMMDeviceEnumerator* enumerator)
        : enumerator_(enumerator) {}

    // IUnknown
    ULONG   STDMETHODCALLTYPE AddRef()  override { return ++ref_count_; }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG ref = --ref_count_;
        if (ref == 0) delete this;
        return ref;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IAudioSessionNotification)) {
            *ppv = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // IAudioSessionNotification — fired when any new session is created.
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* /*session*/) override {
        // Phase 2b will add per-session inspection to detect Communications-category
        // streams and reroute the default communications endpoint.
        return S_OK;
    }

private:
    IMMDeviceEnumerator* enumerator_; // non-owning reference
    ULONG ref_count_ = 1;
};

struct HfpGuard::Impl {
    ComPtr<IMMDeviceEnumerator>   enumerator;
    ComPtr<IAudioSessionManager2> session_mgr;
    SessionNotifier*              notifier = nullptr; // COM-managed lifetime (see above)
    bool                          active   = false;
};

HfpGuard::HfpGuard() : impl_(std::make_unique<Impl>()) {}

HfpGuard::~HfpGuard() { stop(); }

bool HfpGuard::start() {
    if (impl_->active) return true;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&impl_->enumerator.p)
    );
    if (FAILED(hr)) return false;

    IMMDevice* device = nullptr;
    hr = impl_->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr) || !device) return false;

    hr = device->Activate(
        __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&impl_->session_mgr.p)
    );
    device->Release();
    if (FAILED(hr)) return false;

    impl_->notifier = new SessionNotifier(impl_->enumerator.p);
    hr = impl_->session_mgr->RegisterSessionNotification(impl_->notifier);
    if (FAILED(hr)) {
        impl_->notifier->Release();
        impl_->notifier = nullptr;
        return false;
    }

    impl_->active = true;
    return true;
}

void HfpGuard::stop() {
    if (!impl_->active) return;
    if (impl_->session_mgr && impl_->notifier) {
        impl_->session_mgr->UnregisterSessionNotification(impl_->notifier);
        impl_->notifier->Release();
        impl_->notifier = nullptr;
    }
    impl_->session_mgr.reset();
    impl_->enumerator.reset();
    impl_->active = false;
}

bool HfpGuard::is_active() const noexcept { return impl_->active; }

} // namespace owb
