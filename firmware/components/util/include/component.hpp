#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

extern "C" {

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

}

class ActiveObject {
public:
  enum class MemoryLoad : configSTACK_DEPTH_TYPE {
    kMinimal = 2048,
    kStandard = 4096,
    kHeavy = 8192
  };

  enum class Priority : UBaseType_t {
    kLow = 1,
    kMedium = 3,
    kHigh = 5
  };

  enum class CorePreference : BaseType_t {
    kZero = 0,
    kOne = 1,
    kNone = tskNO_AFFINITY
  };

  static constexpr std::size_t kMaxComponentNameLength = 32;

  /// @brief create a ActiveObject, which encapsulates the logic of an RTOS task
  /// @param name component identifier
  /// @param load memory/stack requirements
  /// @param priority task priority
  /// @param thread_period_ms how often the task function runs (milliseconds)
  /// @param core_pref core preference (0, 1, or none)
  ActiveObject(const std::string_view name, 
            const MemoryLoad load, 
            const Priority priority, 
            const std::optional<std::uint32_t> thread_period_ms = std::nullopt,
            const CorePreference core_pref = CorePreference::kNone);

  /// @brief ends the RTOS task
  ~ActiveObject();

  /// @brief launch RTOS task
  /// @return boolean indicating successful launch
  bool start();

  /// @brief a name to identify this component
  std::string_view get_name() const;

  /**
   * @brief Wait for the RTOS task to complete. It is the 
   * responsibility of the task that generated the ActiveObject 
   * to join it again.
   */
  bool join();

protected:
  /// @brief mark this task as complete (see docs on done_)
  void mark_as_done() { done_.store(true); }

private:
  /// @brief perform any initializations necessary for the task function
  virtual void initialize() {}

  /**
   * @brief Iteration of work to complete when this task occupies the CPU.
   * This function should never loop indefinitely and instead track state.
   */
  virtual void task() = 0;

  /**
   * @brief The RTOS task handle. 
   * If the handle has a non-null value, it is 
   * assumed that the task is "running."
   * If the handle is empty, it is assumed that
   * the task does not exist. This convention is 
   * important because the handle is used as a guard
   * in the destructor to check if the task must be
   * joined (i.e. block) before it is freed. 
   * 
   * IMPORTANT: This must not be written to by the 
   * task function itself. All writes to this must 
   * be made by the task that created it (e.g. main)
   */
  std::optional<TaskHandle_t> task_handle_{std::nullopt};

  /// @brief internal component name
  std::array<char, kMaxComponentNameLength> name_{'\0'};

  /// @brief estimated component load
  const MemoryLoad load_{MemoryLoad::kMinimal};

  /// @brief component priority
  const Priority priority_{Priority::kLow};

  /**
   * @brief Thread period in milliseconds. In at attempt 
   * to future proof, this is made optional to allow custom
   * Components to manage themselves when sub-millisecond 
   * latency is required (potentially for streaming purposes).
   * In such cases the responsiblity is on the implementation to 
   * use higher resolution timers and yield accordingly to keep 
   * the watchdog happy
   */
  const std::optional<std::uint32_t> thread_period_ms_{std::nullopt};

  /// @brief core preference
  const CorePreference core_pref_{CorePreference::kNone};

  /// @brief Boolean indicating whether the task has completed or not.
  std::atomic<bool> done_{false};

  /// @brief buffer to store join semaphore on stack
  StaticSemaphore_t join_sem_buffer_{};

  /// @brief semaphore that releases on task completion
  SemaphoreHandle_t join_sem_handle_{nullptr};
};