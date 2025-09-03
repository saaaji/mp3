#include <cstdio>
#include <memory>

#include "esp_log.h"

#include "component.hpp"
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
  const auto test_component = std::make_unique<TestComponent>();
  test_component->start();
  test_component->join();
  printf("All joinable components done.\n");
}