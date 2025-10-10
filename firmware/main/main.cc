#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>

#include "util.hpp"
#include "active_object.hpp"
#include "sd_card.hpp"
#include "wifi_object.hpp"
#include "mailbox.hpp"

extern "C" {

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

}

namespace {

constexpr const char* kComponentTag = "AppMain";
constexpr std::uint32_t kWatchdogTimeoutMs = 10 * 1000;

const SdCardObject::Config kSdConfig = {
  .interface = SdCardObject::Interface::SPI,
  .miso = GPIO_NUM_19,
  .mosi = GPIO_NUM_23,
  .sck = GPIO_NUM_18,
  .cs = GPIO_NUM_5,
  .max_frequency_khz = 400,
  .max_open_files = 3,
  .format_if_mount_failed = false,
};

}

/**
 * MAIN
 */

/// @brief restart system software whenever RTOS task stack overflows
/// @param handle handle for the RTOS task
/// @param name name of the RTOS task
extern "C" void vApplicationStackOverflowHook(TaskHandle_t handle, char *name) {
  ESP_LOGE(kComponentTag, "error: stack overflow in %s, triggering software restart", name);
}

/// @brief firmware entrypoint
extern "C" void app_main() {
  /**
   * LOGGING CONFIGURATION
   */
  esp_log_level_set("*", ESP_LOG_VERBOSE);

  /**
   * WATCHDOG CONFIGURATION
   */
  const esp_task_wdt_config_t wdt_config = {
    .timeout_ms = kWatchdogTimeoutMs,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  
  // initialize the watchdog if necessary
  switch (esp_task_wdt_status(nullptr)) {
    case ESP_ERR_INVALID_STATE:
      ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_config));
      [[fallthrough]];
    case ESP_ERR_NOT_FOUND:
      esp_task_wdt_add(nullptr);
      break;
  }

  /**
   * ACTIVE OBJECT INITIALIZATION
   */
  mp3::Mailbox<WifiObject::Command> wifi_mailbox(64);
  if (!wifi_mailbox.send_message<WifiObject::Command>(WifiObject::Command::kSpinUp)) {
    ESP_LOGE(kComponentTag, "unable to send Wifi message");
  }

  SdCardObject sd_object(kSdConfig);
  WifiObject wifi_object(&wifi_mailbox);
  
  std::array<ActiveObject*, 2> components = {&sd_object, &wifi_object};

  // start all components
  for (auto component : components) {
    const auto status = component->start();
    if (!status) {
      ESP_LOGE(kComponentTag, "ActiveObject failed to initialize: '%s'", component->get_name().data());
      assert(false);
    }
  }

  // join all components
  for (auto component : components) {
    component->join();
  }
  ESP_LOGI(kComponentTag, "Components joined");
  
  /**
   * CLEANUP
   */
  esp_task_wdt_delete(nullptr);
}