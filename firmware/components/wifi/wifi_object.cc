#include <string_view>
#include <cstring>

#include "include/wifi_object.hpp"
#include "util.hpp"

extern "C" {

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"

}

namespace {

constexpr const char* kComponentTag = "WifiObject";
constexpr std::string_view kSsid = "esp32-mp3-player";
constexpr std::string_view kPwd = "esp32mp3";
constexpr std::uint16_t kPort = 8080;

constexpr const char* kIndexHtml = R"html(
<html>
  <body>
    <h1>Hello World</h1>
  </body>
</html>
)html";

}

WifiObject::WifiObject(mp3::Mailbox<WifiObject::Command>* mailbox)
  : ActiveObject("WifiObject", ActiveObject::MemoryLoad::kStandard, ActiveObject::Priority::kLow, 1000), 
    mailbox_(mailbox) {}

void WifiObject::spin_up() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));

  wifi_ap_config_t ap_config = {};

  // set SSID
  static_assert(kSsid.size() <= sizeof(ap_config.ssid) - 1);
  auto copy_len = std::min(kSsid.size(), sizeof(ap_config.ssid) - 1);
  std::memcpy(ap_config.ssid, kSsid.data(), copy_len);
  ap_config.ssid[copy_len] = '\0';
  ap_config.ssid_len = copy_len;

  // set password
  static_assert(kPwd.size() <= sizeof(ap_config.password) - 1);
  copy_len = std::min(kPwd.size(), sizeof(ap_config.password) - 1);
  std::memcpy(ap_config.password, kPwd.data(), copy_len);

  // set remaining config values
  ap_config.authmode = kPwd.size() > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  ap_config.max_connection = 1;

  // init wifi config
  wifi_config_t config;
  config.ap = ap_config;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // setup HTTP
  httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
  http_config.server_port = kPort;
  ESP_ERROR_CHECK(httpd_start(&handle_, &http_config));

  httpd_uri_t uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = [](httpd_req_t* req) -> esp_err_t {
      return static_cast<WifiObject*>(req->user_ctx)->get_index(req);
    },
    .user_ctx = this
  };
  httpd_register_uri_handler(handle_, &uri);

  // change state
  state_ = WifiObject::State::kUp;
}

void WifiObject::spin_down() {
  if (handle_) {
    ESP_ERROR_CHECK(httpd_stop(handle_));
  }

  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_deinit());
  ESP_ERROR_CHECK(esp_netif_deinit());
}

void WifiObject::task() {
  // check for a message, otherwise do nothing
  if (auto msg = mailbox_->acquire_recv_handle(pdMS_TO_TICKS(0))) {
    msg->visit(overloads{
      [this](WifiObject::Command cmd) {
        switch (cmd) {
          case WifiObject::Command::kSpinUp:
            if (state_ == WifiObject::State::kUp) {
              break;
            }

            ESP_LOGI(kComponentTag, "spinning up AP");
            spin_up();
            break;
          case WifiObject::Command::kSpinDown:
            if (state_ == WifiObject::State::kDown) {
              break;
            }

            ESP_LOGI(kComponentTag, "spinning down AP");
            spin_down();
            break;
        }
      },
      [](std::span<const std::uint8_t> blob) {},
    });
  }
}

esp_err_t WifiObject::get_index(httpd_req_t* req) {
  httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}