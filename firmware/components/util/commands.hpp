#pragma once

#include <cstdint>
#include "message_queue.hpp"

extern "C" {

#include "esp_bt_defs.h"
#include "esp_a2dp_api.h"

}

namespace bt {

enum class Command {
  DUMMY // placeholder for wifi module
};

// command
struct StartDiscovery {};

// internal
struct GapDiscoveryStarted {};
struct GapDiscoveryStopped {
  bool has_bda;
  esp_bd_addr_t bda;
};
struct GapDeviceFound {};
struct GapPinRequest {
  bool min_16_digits;
  esp_bd_addr_t bda;
};
struct GapSspRequest {
  esp_bd_addr_t bda;
};
struct GapAuthResult {
  bool success;
};

struct A2dpConnectionStatus {
  bool connected;
  esp_bd_addr_t bda;
};

struct A2dpMediaControlAck {
  esp_a2d_media_ctrl_t cmd;
  esp_a2d_media_ctrl_ack_t status;
};

using BtQueue = rtos::MessageQueue<
  StartDiscovery,
  GapDiscoveryStarted,
  GapDiscoveryStopped,
  GapDeviceFound,
  GapPinRequest,
  GapSspRequest,
  GapAuthResult,
  A2dpConnectionStatus,
  A2dpMediaControlAck
>;

} // namespace bt

namespace wifi {

enum class Command : std::uint8_t {
    kSpinUp,
    kSpinDown
};

} // namespace wifi