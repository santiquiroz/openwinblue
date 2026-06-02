// service/src/a2dp_stream.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>    // CTL_CODE — must come before owb_ioctl.h with WIN32_LEAN_AND_MEAN

#include "a2dp_stream.h"
#include "owb_ioctl.h"   // included via CMake target_include_directories for driver/

#include <cstring>
#include <vector>

namespace owb {

static constexpr wchar_t kDeviceName[] = L"\\\\.\\OpenWinBlue";

struct A2dpStream::Impl {
    HANDLE device = INVALID_HANDLE_VALUE;
};

A2dpStream::A2dpStream() : impl_(std::make_unique<Impl>()) {}
A2dpStream::~A2dpStream() { close(); }

bool A2dpStream::open() {
    if (impl_->device != INVALID_HANDLE_VALUE) return true;
    impl_->device = CreateFileW(
        kDeviceName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr
    );
    return impl_->device != INVALID_HANDLE_VALUE;
}

void A2dpStream::close() {
    if (impl_->device != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->device);
        impl_->device = INVALID_HANDLE_VALUE;
    }
}

bool A2dpStream::is_open() const noexcept {
    return impl_->device != INVALID_HANDLE_VALUE;
}

bool A2dpStream::send_frame(uint32_t codec_id, std::span<const uint8_t> frame) {
    if (!is_open() || frame.empty()) return false;

    const DWORD input_size = static_cast<DWORD>(
        OWB_SEND_FRAME_INPUT_SIZE(static_cast<DWORD>(frame.size()))
    );
    std::vector<BYTE> buf(input_size);

    auto* input     = reinterpret_cast<OWB_SEND_FRAME_INPUT*>(buf.data());
    input->codec_id = static_cast<unsigned long>(codec_id);
    input->data_len = static_cast<unsigned long>(frame.size());
    std::memcpy(input->data, frame.data(), frame.size());

    DWORD bytes_returned = 0;
    return DeviceIoControl(
        impl_->device,
        OWB_IOCTL_SEND_AUDIO_FRAME,
        buf.data(), input_size,
        nullptr, 0,
        &bytes_returned, nullptr
    ) != FALSE;
}

bool A2dpStream::get_rf_quality(int32_t* rssi_dbm, uint32_t* retransmit_per_mille) {
    if (!is_open() || !rssi_dbm || !retransmit_per_mille) return false;

    OWB_RF_QUALITY quality{};
    DWORD bytes_returned = 0;
    BOOL ok = DeviceIoControl(
        impl_->device,
        OWB_IOCTL_GET_RF_QUALITY,
        nullptr, 0,
        &quality, static_cast<DWORD>(sizeof(quality)),
        &bytes_returned, nullptr
    );

    if (!ok || bytes_returned < sizeof(quality)) return false;
    *rssi_dbm             = quality.rssi_dbm;
    *retransmit_per_mille = quality.retransmit_per_mille;
    return true;
}

bool A2dpStream::set_codec_config(uint32_t codec_id, std::string_view key, int64_t value) {
    if (!is_open()) return false;

    OWB_CODEC_CONFIG cfg{};
    cfg.codec_id    = static_cast<unsigned long>(codec_id);
    cfg.param_value = value;
    const size_t key_len = std::min(key.size(), sizeof(cfg.param_key) - 1);
    std::memcpy(cfg.param_key, key.data(), key_len);

    DWORD bytes_returned = 0;
    return DeviceIoControl(
        impl_->device,
        OWB_IOCTL_SET_CODEC_CONFIG,
        &cfg, static_cast<DWORD>(sizeof(cfg)),
        nullptr, 0,
        &bytes_returned, nullptr
    ) != FALSE;
}

bool A2dpStream::get_device_state(OWB_DEVICE_STATE* state) {
    if (!is_open() || !state) return false;

    DWORD bytes_returned = 0;
    BOOL ok = DeviceIoControl(
        impl_->device,
        OWB_IOCTL_GET_DEVICE_STATE,
        nullptr, 0,
        state, static_cast<DWORD>(sizeof(OWB_DEVICE_STATE)),
        &bytes_returned, nullptr
    );
    return ok && bytes_returned >= sizeof(OWB_DEVICE_STATE);
}

} // namespace owb
