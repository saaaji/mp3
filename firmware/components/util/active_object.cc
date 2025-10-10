#include "include/active_object.hpp"

extern "C" {

#include "esp_log.h"
#include "esp_task_wdt.h"

}

namespace {

constexpr const char* kComponentTag = "ActiveObject";
constexpr std::uint32_t kJoinWaitMs = 500;

}

ActiveObject::ActiveObject(const std::string_view name, 
                     const MemoryLoad load, 
                     const Priority priority,
                     const std::optional<std::uint32_t> thread_period_ms,
                     const CorePreference core_pref) 
  : load_(load), priority_(priority), thread_period_ms_(thread_period_ms), core_pref_(core_pref) {
  // trim name if necessary
  const std::size_t copy_length = std::min(
    name.size(),
    ActiveObject::kMaxComponentNameLength - 1);
  
  std::size_t i{};
  for (i = 0; i < copy_length; i++) {
    name_[i] = name[i];
  }

  name_[i] = '\0';
}

ActiveObject::~ActiveObject() {
  ESP_LOGI(kComponentTag, "Ending task: '%s'", get_name().data());
  mark_as_done();
  join();
}

bool ActiveObject::start() {
  if (task_handle_) {
    return false;  // already running
  }

  // create join semaphore
  join_sem_handle_ = xSemaphoreCreateBinaryStatic(&join_sem_buffer_);
  if (!join_sem_handle_) {
    ESP_LOGE(kComponentTag, "Failed to create join semaphore");
    return false;
  }

  // create the task
  const auto task_wrapper = [](void* self_arg) -> void {
    const auto self = static_cast<ActiveObject*>(self_arg);
    
    // add task to watchdog to track scheduling conflicts
    esp_task_wdt_add(nullptr);
    
    // single-run initialization routine
    self->initialize();
    esp_task_wdt_reset();
    
    // initialize timing for drift-free periodic execution
    TickType_t last_wake_time = xTaskGetTickCount();
    
    // interior loop
    while (!self->done_.load()) {
      // run the task and pet the watchdog
      self->task();
      esp_task_wdt_reset();
      
      // precise delay accounting for period and task iteration execution time
      if (self->thread_period_ms_) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(*self->thread_period_ms_));
      }
    }

    size_t high_mark = static_cast<size_t>(uxTaskGetStackHighWaterMark(nullptr));
    ESP_LOGI(kComponentTag, "stack watermark for '%s': %zu", self->name_.data(), high_mark);

    // release the binary join semaphore
    if (self->join_sem_handle_) {
      xSemaphoreGive(self->join_sem_handle_);
    }

    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
  };

  TaskHandle_t handle;
  const BaseType_t result = xTaskCreatePinnedToCore(
    task_wrapper,
    name_.data(),
    static_cast<configSTACK_DEPTH_TYPE>(load_),
    static_cast<void*>(this),
    static_cast<UBaseType_t>(priority_), 
    &handle,
    static_cast<BaseType_t>(core_pref_)
  );
  
  if (handle && result == pdPASS) {
    task_handle_ = handle;
  } else {
    ESP_LOGE(kComponentTag, "Could not create RTOS task");
    return false;
  }

  // verify result
  return result == pdPASS;
}

bool ActiveObject::join() {
  // either task never started or was already joined
  if (!join_sem_handle_ || !task_handle_) {
    return false;
  }

  // wait until the task is finished
  while (!xSemaphoreTake(join_sem_handle_, pdMS_TO_TICKS(kJoinWaitMs))) {
    esp_task_wdt_reset();
  }

  // clear the guards; should invalidate any other methods
  task_handle_ = std::nullopt;
  join_sem_handle_ = nullptr;
  return true;
}

std::string_view ActiveObject::get_name() const {
  return std::string_view(name_.data());
}