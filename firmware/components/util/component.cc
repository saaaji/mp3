#include "include/component.hpp"

Component::Component(const std::string_view name, const MemoryLoad load, const Priority priority, const bool detached) 
  : load_(load), priority_(priority), detached_(detached) {
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
    join();
    stop();
  }
}

bool Component::start() {
  // call initialization routine
  initialize();

  // create join semaphore
  join_sem_handle_ = xSemaphoreCreateBinaryStatic(&join_sem_buffer_);

  // create task
  const auto task_wrapper = [](void* param) -> void {
    const auto self = static_cast<Component*>(param);
    self->task_impl();
    if (self->join_sem_handle_) {
      xSemaphoreGive(self->join_sem_handle_);
    }

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
    &handle);
  
  if (handle && result == pdPASS) {
    task_handle_ = handle;
  }

  // verify result
  return result == pdPASS;
}

void Component::stop() {
  if (task_handle_ && xTaskGetCurrentTaskHandle() != *task_handle_) {
    vTaskDelete(*task_handle_);
    task_handle_ = std::nullopt;
  }
}

std::string_view Component::get_name() const {
  return std::string_view(name_.data());
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