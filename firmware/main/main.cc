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
    "TestComponent", Component::MemoryLoad::kMinimal, Component::Priority::kLow, 1000) {}

private:
  int print_count_ = 0;

  void task_impl() override {
    const std::string_view name = get_name();
    printf("%s: Print Statement #%d\n", name.data(), print_count_);
    print_count_++;
    
    // Stop after 5 iterations
    if (print_count_ >= 5) {
      request_stop();
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

  // Initialize SD card component with SPI interface (Arduino-compatible settings)
  const SDCard::Config sd_config{
    .interface = SDCard::Interface::SPI,
    .miso = GPIO_NUM_19,
    .mosi = GPIO_NUM_23,
    .sck  = GPIO_NUM_18,
    .cs   = GPIO_NUM_5,
    .max_frequency_khz = 400,  // Start with very low frequency (400kHz) like Arduino
    .format_if_mount_failed = false,
    .max_open_files = 3
  };

  // Wait a moment for system to stabilize
  vTaskDelay(pdMS_TO_TICKS(500));
  
  const auto sd_card = std::make_unique<SDCard>(sd_config);
  LOG("Creating SD card component with SPI pins - MISO:%d, MOSI:%d, SCK:%d, CS:%d", 
      sd_config.miso, sd_config.mosi, sd_config.sck, sd_config.cs);
      
  if (!sd_card->start()) {
    LOG("Failed to start SD card task");
    return;
  }

  // SD card will automatically initialize, mount, discover files, and read playback order
  // in its initialize() method. Wait for initialization to complete.
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  // Wait for test component to finish its demonstration
  test_component->join();
  LOG("Test component completed");

  // Keep the SD card task running
  sd_card->join();
  LOG("All joinable components done");
}