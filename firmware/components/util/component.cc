#include "include/component.hpp"

extern "C" {
#include "esp_task_wdt.h"
}

Component::Component(const std::string_view name, const MemoryLoad load, const Priority priority, 
                     uint32_t thread_period_ms, const bool detached) 
  : load_(load), priority_(priority), thread_period_ms_(thread_period_ms), detached_(detached) {
  // trim name if necessary
  const std::size_t copy_length = std::min(
    name.size(),
    Component::kMaxComponentNameLength - 1);
  
  std::size_t i{};
  for (i = 0; i < copy_length; i++) {
    name_[i] = name[i];
  }

  name_[i] = '\0';
}

Component::~Component() {
  if (!detached_) {
    request_stop();
    join();
  }
}

bool Component::start() {
  if (task_handle_) {
    return false;  // already running
  }

  // create join semaphore
  join_sem_handle_ = xSemaphoreCreateBinaryStatic(&join_sem_buffer_);

  // create task - move initialization INTO the task to prevent main task blocking
  const auto task_wrapper = [](void* param) -> void {
    const auto self = static_cast<Component*>(param);
    
    // Give system a moment to stabilize before initializing
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Add this task to watchdog monitoring - detects task starvation
    esp_task_wdt_add(NULL);
    
    // Initialize in the task context (not main task)
    self->initialize();
    
    // Reset watchdog after potentially slow initialization
    esp_task_wdt_reset();
    
    // Initialize timing for drift-free periodic execution
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(self->thread_period_ms_);
    
    // Framework-controlled periodic execution loop
    while (self->is_running()) {
      self->task_impl();  // Single iteration - no loops in components
      esp_task_wdt_reset();  // Automatic watchdog petting
      
      // Use vTaskDelayUntil for precise periodic timing without drift
      vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
    
    if (self->join_sem_handle_) {
      xSemaphoreGive(self->join_sem_handle_);
    }

    // Remove from watchdog before deletion
    // esp_task_wdt_delete(NULL);
    self->task_handle_ = std::nullopt;
    vTaskDelete(nullptr);
  };

  const std::size_t stack_size = static_cast<std::size_t>(load_);
  const std::uint8_t priority = static_cast<std::uint8_t>(priority_);

  TaskHandle_t handle;
  const BaseType_t result = xTaskCreate(
    task_wrapper,
    name_.data(),
    static_cast<configSTACK_DEPTH_TYPE>(stack_size),
    static_cast<void*>(this),
    static_cast<UBaseType_t>(priority), 
    &handle
  );
  
  if (handle && result == pdPASS) {
    task_handle_ = handle;
  }

  // verify result
  return result == pdPASS;
}

void Component::request_stop() {
  should_terminate_ = true;
}

std::string_view Component::get_name() const {
  return std::string_view(name_.data());
}

void Component::pet_watchdog() {
  esp_task_wdt_reset();
}

bool Component::join() {
  // either task never started or was already joined or is detached 
  if (!task_handle_ || detached_) {
    return false;
  }

  // wait on the join semaphore if it exists
  if (join_sem_handle_) {
    xSemaphoreTake(join_sem_handle_, portMAX_DELAY);
    return true;
  }

  return false;
}