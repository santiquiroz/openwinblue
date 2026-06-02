// service/src/a2dp_stream.h
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <memory>
#include "owb_ioctl.h"

namespace owb {

// A2dpStream — user-mode DeviceIoControl interface to owb_a2dp.sys.
//
// Stub mode: when the kernel driver is not installed, is_open() returns false
// and all operations return false immediately — the service pipeline can run
// without the driver during development.
class A2dpStream {
public:
    A2dpStream();
    ~A2dpStream();

    // Open the kernel device. Returns false if driver not installed.
    bool open();

    // Close the device handle. Safe to call when not open.
    void close();

    bool is_open() const noexcept;

    // Send one encoded audio frame. codec_id: OWB_CODEC_* from owb_ioctl.h.
    // Returns false in stub mode or on IOCTL failure.
    bool send_frame(uint32_t codec_id, std::span<const uint8_t> frame);

    // Query RF link quality. Returns false in stub mode.
    bool get_rf_quality(int32_t* rssi_dbm, uint32_t* retransmit_per_mille);

    // Push a codec parameter to the driver.
    bool set_codec_config(uint32_t codec_id, std::string_view key, int64_t value);

    // Query the driver's A2DP connection state.
    // Fills *state. Returns false in stub mode or if IOCTL fails.
    bool get_device_state(OWB_DEVICE_STATE* state);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
