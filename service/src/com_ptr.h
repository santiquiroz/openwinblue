// service/src/com_ptr.h
// Minimal RAII COM pointer — avoids ATL/WRL dependency.
// Used by audio_capture.cpp, hfp_guard.cpp, and future COM-heavy modules.
#pragma once

namespace owb {

template<typename T>
struct ComPtr {
    T* p = nullptr;

    ~ComPtr() { reset(); }

    void reset() {
        if (p) { p->Release(); p = nullptr; }
    }

    T** operator&() { return &p; }
    T*  operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }

    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
};

} // namespace owb
