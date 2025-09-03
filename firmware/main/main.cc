#include <cstdio>
#include <memory>

#include "esp_log.h"

#include "component.hpp"
#include "sd_card.hpp"
#include "util.hpp"

namespace {

constexpr std::size_t kMaxComponentCount = 8;

}

class TestComponent : public Component {
public:
  TestComponent() : Component(
    "TestComponent", Component::MemoryLoad::kMinimal, Component::Priority::kLow, false) {}

  void task_impl() {
    const std::string_view name = get_name();
    for (int i = 0; i < 5; i++) {
      printf("%s: Print Statement #%d\n", name.data(), i);
      vTaskDelay(pdMS_TO_TICKS(1000));
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
  // Create a test component to demonstrate basic task functionality
  const auto test_component = std::make_unique<TestComponent>();
  if (!test_component->start()) {
    LOG("Failed to start test component");
    return;
  }

  // Initialize SD card component
  const SDCard::Config sd_config{
    .miso = GPIO_NUM_19,
    .mosi = GPIO_NUM_23,
    .sck  = GPIO_NUM_18,
    .cs   = GPIO_NUM_5
  };

  const auto sd_card = std::make_unique<SDCard>(sd_config);
  if (!sd_card->start()) {
    LOG("Failed to start SD card task");
    return;
  }

  if (!sd_card->mount()) {
    LOG("Failed to mount SD card");
    return;
  }

  // Read Bluetooth configuration
  std::array<uint8_t, 6> mac_address;
  if (!sd_card->read_bluetooth_config(mac_address)) {
    LOG("Failed to read Bluetooth configuration");
    return;
  }

  // List available MP3 files in order
  const auto files = sd_card->get_ordered_mp3_files();
  LOG("Found MP3 files in playback order:");
  for (const auto& file : files) {
    if (file[0] != '\0') {  // Only print valid entries
      LOG("  %s", file.data());
    }
  }

  LOG("SD card initialization complete");
  
  // Wait for test component to finish its demonstration
  test_component->join();
  LOG("Test component completed");

  // Keep the SD card task running
  sd_card->join();
  LOG("All joinable components done");
}