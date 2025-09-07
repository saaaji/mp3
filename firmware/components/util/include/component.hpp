#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

extern "C" {

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

}

class Component {
public:
  enum class MemoryLoad : std::size_t {
    kMinimal = 2048,
    kStandard = 4096,
    kHeavy = 8192
  };

  enum class Priority : std::uint8_t {
    kLow = 1,
    kMedium = 3,
    kHigh = 5
  };

  static constexpr std::size_t kMaxComponentNameLength = 32;

  /// @brief create a Component, which encapsulates the logic of an RTOS task
  /// @param name component identifier
  /// @param load memory/stack requirements
  /// @param priority task priority
  /// @param thread_period_ms how often task_impl() runs (milliseconds)
  /// @param detached whether task is joinable
  Component(const std::string_view name, const MemoryLoad load, const Priority priority, 
            uint32_t thread_period_ms, const bool detached = false);

  /// @brief destructor should always destroy the RTOS task
  ~Component();

  /// @brief launch RTOS task
  /// @return boolean indicating successful launch
  bool start();

  /// @brief a name to identify this component
  std::string_view get_name() const;

  /// @brief wait for task to complete
  /// @return boolean indicating successful task completion
  bool join();

protected:
  /// @brief reset the watchdog timer for this task (call regularly in task_impl)
  void pet_watchdog();

  /// @brief check if component should continue running (used by framework)
  virtual bool is_running() const { return task_handle_.has_value(); }

  /// @brief stop the component task (callable from task_impl)
  void stop();

private:

  /// @brief perform any initializations necessary for the task function
  virtual void initialize() {}

  /// @brief work to complete whenever this task occupies the CPU
  virtual void task_impl() = 0;

  /// @brief RTOS task handle
  std::optional<TaskHandle_t> task_handle_{std::nullopt};

  /// @brief internal component name
  std::array<char, kMaxComponentNameLength> name_{'\0'};

  /// @brief estimated component load
  MemoryLoad load_{MemoryLoad::kMinimal};

  /// @brief component priority
  Priority priority_{Priority::kLow};

  /// @brief task execution period in milliseconds
  uint32_t thread_period_ms_{1000};


  /// @brief boolean indicating whether this task is joinable or not
  bool detached_{};

  /// @brief buffer to store join semaphore on stack
  StaticSemaphore_t join_sem_buffer_{};

  /// @brief semaphore that releases on task completion
  SemaphoreHandle_t join_sem_handle_{nullptr};
};