#include "active_object.hpp"
#include "mailbox.hpp"

extern "C" {

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

}

class WifiObject : public ActiveObject {
public:
  enum class Command : uint8_t {
    kSpinUp,
    kSpinDown
  };

  WifiObject(mp3::Mailbox<WifiObject::Command>* mailbox);

private:
  enum class State : uint8_t {
    kUp,
    kDown
  };

  mp3::Mailbox<WifiObject::Command>* mailbox_;
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