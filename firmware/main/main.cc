#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>

#include "util.hpp"
#include "active_object.hpp"
#include "mailbox.hpp"
#include "sd_card.hpp"

extern "C" {

#include "esp_log.h"
#include "esp_task_wdt.h"

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

class TestComponent : public ActiveObject {
public:
  TestComponent(std::optional<mp3::mailbox::MailboxSender<int, float>> sender = std::nullopt) 
    : ActiveObject(sender ? "TestSend" : "TestRecv", ActiveObject::MemoryLoad::kMinimal, ActiveObject::Priority::kLow, 1000),
      sender_(sender) {}

  mp3::mailbox::MailboxSender<int, float> get_sender() {
    return mp3::mailbox::MailboxSender(mailbox_);
  }

private:
  mp3::mailbox::Mailbox<int, float> mailbox_{1024};
  std::optional<mp3::mailbox::MailboxSender<int, float>> sender_;
  
  int counter_{0};

  void task() override {
    const std::string_view name = get_name();
    
    if (sender_) {
      printf("%s: sending int (%d)\n", name.data(), counter_);
      sender_->send_message<float>(counter_/5.0);

      if (counter_++ >= 5) {
        sender_->send_message<int>(-1);
        mark_as_done();
      }
    } else {
      auto handle = mailbox_.acquire_recv_handle();
      if (handle) {
        handle->visit(overloads{
          [&](float f) {
            printf("%s: message received (%d%%)\n", name.data(), (int)(f*100));
          },
          [&](int i) {
            if (i < 0) {
              mark_as_done();
            }
          },
          [](std::span<const std::uint8_t> blob) {}
        });
      }
    }
  }
};

/// @brief restart system software whenever RTOS task stack overflows
/// @param handle handle for the RTOS task
/// @param name name of the RTOS task
extern "C" void vApplicationStackOverflowHook(TaskHandle_t handle, char *name) {
  constexpr const char* const fmt_str = "error: stack overflow in %s, triggering software restart";
  /// TODO: log crash report to NVS
  CHECK(false, fmt_str, name);
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
  std::vector<std::shared_ptr<ActiveObject>> components;

  components.push_back(std::make_shared<SdCardObject>(kSdConfig));

  auto obj1 = std::make_shared<TestComponent>();
  auto obj2 = std::make_shared<TestComponent>(obj1->get_sender());
  components.push_back(obj2);
  components.push_back(obj1);

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