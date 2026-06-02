// service/src/hfp_guard.h
#pragma once
#include <memory>

namespace owb {

// HFP Guard — prevents Windows from automatically switching the Bluetooth
// headset from A2DP stereo to HFP mono when a Communications audio stream
// is opened by an application (e.g. VoIP call).
//
// Level 2: registers IAudioSessionNotification via COM. When a new session
// is created, the guard can intercept HFP-triggering behaviour.
// Requires COM initialized by the caller (CoInitializeEx).
class HfpGuard {
public:
    HfpGuard();
    ~HfpGuard();

    // Register session notifications. Returns false on COM failure.
    bool start();

    // Unregister notifications and release COM objects.
    void stop();

    bool is_active() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
