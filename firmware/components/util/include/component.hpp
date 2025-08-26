#pragma once

#include <array>
#include <cstddef>
#include <string_view>

extern "C" {

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

}

class Component {
public:
  enum class Load : std::size_t {
    kMinimal = 2048,
    kStandard = 4096,
    kHeavy = 8192
  };

  static constexpr std::size_t kMaxComponentNameLength = 32;

  /// @brief create a Component, which encapsulates the logic of an RTOS task
  Component(const std::string_view name, Load load);

  /// @brief destructor should always destroy the RTOS task
  virtual ~Component() { stop(); }

  /// @brief launch RTOS task
  /// @return boolean indicating successful launch
  bool start();

  /// @brief perform any initializations necessary for the task function
  virtual void initialize() {}

  /// @brief work to complete whenever this task occupies the CPU
  virtual void task_impl() = 0;

  /// @brief a name to identify this component
  std::string_view get_name() const;

private:
  /// @brief destroy the RTOS task
  void stop();

  /// @brief RTOS task handle
  TaskHandle_t task_handle_{nullptr};

  /// @brief internal component name
  std::array<char, kMaxComponentNameLength> name_{'\0'};

  /// @brief estimated component load
  Load load_{Load::kMinimal};
};