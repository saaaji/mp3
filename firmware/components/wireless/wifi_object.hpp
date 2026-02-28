#pragma once

#include "active_object.hpp"
#include "commands.hpp"
#include "message_queue.hpp"

extern "C" {

#include "esp_err.h"
#include "esp_http_server.h"

}

namespace wifi {

class WifiObject : public ActiveObject {
public:
  WifiObject(
    rtos::MessageQueue<Command>* my_queue,
    rtos::MessageQueue<::bt::Command>* bt_queue
  );

private:
  enum class State : uint8_t {
    kUp,
    kDown
  };

  rtos::MessageQueue<Command>* my_queue_{nullptr};
  rtos::MessageQueue<::bt::Command>* bt_queue_{nullptr};

  State state_{State::kDown};
  httpd_handle_t handle_{nullptr};

  void spin_up();
  void spin_down();
  void task() override;

  /**
   * HTTP
   */
  esp_err_t get_index(httpd_req_t* req);
};

}