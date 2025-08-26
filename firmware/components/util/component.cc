#include "include/component.hpp"

Component::Component(const std::string_view name, Load load) : load_(load) {
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

bool Component::start() {
  // call initialization routine
  initialize();

  // create task
  const auto task_wrapper = [](void* param) -> void {
    static_cast<Component*>(param)->task_impl();
  };

  const std::size_t stack_size = static_cast<std::size_t>(load_);

  return pdPASS == xTaskCreate(
    task_wrapper, 
    name_.data(), 
    static_cast<configSTACK_DEPTH_TYPE>(stack_size),
    static_cast<void*>(this),
    1, 
    &task_handle_);
}

void Component::stop() {

}

std::string_view Component::get_name() const {
  return std::string_view(name_.data());
}