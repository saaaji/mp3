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

  // Additional delay for SD card task to initialize
  vTaskDelay(pdMS_TO_TICKS(1000));

  if (!sd_card->mount()) {
    LOG("Failed to mount SD card - check wiring and card format");
    return;
  }
  
  LOG("SD card mounted successfully!");


  // List available MP3 files in order
  // Discover MP3 files
  std::size_t mp3_count = sd_card->discover_mp3_files();
  LOG("Found %zu MP3 files", mp3_count);
  
  // Get and display all MP3 files
  auto mp3_files = sd_card->get_mp3_files();
  LOG("MP3 files on SD card:");
  for (const auto& file : mp3_files) {
    LOG("  %s", file.c_str());
  }
  
  // Read playback order
  auto playback_order = sd_card->read_playback_order();
  LOG("Found %zu files in playback order:", playback_order.size());
  for (const auto& file : playback_order) {
    LOG("  %s", file.c_str());
  }

  LOG("SD card initialization complete");
  
  // Wait for test component to finish its demonstration
  test_component->join();
  LOG("Test component completed");

  // Keep the SD card task running
  sd_card->join();
  LOG("All joinable components done");
}