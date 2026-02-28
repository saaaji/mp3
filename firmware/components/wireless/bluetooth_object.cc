#include "bluetooth_object.hpp"
#include "util.hpp"

extern "C" {

#include "assert.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

}

namespace {

constexpr const char* kComponentTag = "BluetoothObject";
constexpr const char* kDeviceName = "esp32-mp3-player";
constexpr const std::uint8_t kInquiryDuration = 10; // in 1.28sec units
constexpr const int kThreadPeriodMs = 20;

// placeholder until HTTP discovery interface is working
constexpr std::string_view kTargetDeviceName = "OpenRun by Shokz";

std::optional<std::string_view> get_device_name(uint8_t *eir) {
  if (!eir) return std::nullopt;

  uint8_t *name{nullptr};
  uint8_t name_len{0};

  name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &name_len);
  
  if (!name) {
    name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &name_len);
  }

  if (!name || name_len == 0) {
    return std::nullopt;
  }

  // return a view to the name (must not outlive the GAP event)
  return std::string_view(reinterpret_cast<const char*>(name), name_len);
}

}

namespace bt {

// null-initialize the singleton pointer
BluetoothObject* BluetoothObject::instance_ = nullptr;

BluetoothObject::BluetoothObject(
  std::shared_ptr<BtMessageQueue> my_queue,
  std::shared_ptr<rtos::StreamingQueue> audio_queue
)
  : ActiveObject("BluetoothObject", ActiveObject::MemoryLoad::kHeavy, ActiveObject::Priority::kHigh, kThreadPeriodMs), 
    my_queue_(my_queue),
    audio_queue_(audio_queue) {
      assert(!instance_ && "only one BluetoothObject instance allowed");
      instance_ = this;
    }

void BluetoothObject::initialize() {
  // initialize NVS for pairing info
  esp_err_t stat = nvs_flash_init();
  if (stat == ESP_ERR_NVS_NO_FREE_PAGES || stat == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    stat = nvs_flash_init();
  }
  ESP_ERROR_CHECK(stat);

  // release controller memory for BLE 
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

  // initialize BT classic controller
  esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
  ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

  esp_bluedroid_config_t bd_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bd_config));
  ESP_ERROR_CHECK(esp_bluedroid_enable());

  // configure SSP to advertise IO capability
  esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
  esp_bt_io_cap_t io_cap = ESP_BT_IO_CAP_NONE;
  ESP_ERROR_CHECK(esp_bt_gap_set_security_param(param_type, &io_cap, sizeof(uint8_t)));

  // configure variable pin code for legacy pairing
  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
  esp_bt_pin_code_t pin_code; // typedef'd as integer array so keep uninitialized
  ESP_ERROR_CHECK(esp_bt_gap_set_pin(pin_type, 0, pin_code));

  // initialize stack
  ESP_ERROR_CHECK(esp_bt_gap_set_device_name(kDeviceName));
  ESP_ERROR_CHECK(esp_bt_gap_register_callback(&BluetoothObject::gap_callback));

  ESP_ERROR_CHECK(esp_avrc_ct_init());
  ESP_ERROR_CHECK(esp_avrc_ct_register_callback(&BluetoothObject::avrcp_callback));

  // configure control notifications
  esp_avrc_rn_evt_cap_mask_t evt_set = {0};
  esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
  ESP_ERROR_CHECK(esp_avrc_tg_set_rn_evt_cap(&evt_set));

  ESP_ERROR_CHECK(esp_a2d_source_init());
  ESP_ERROR_CHECK(esp_a2d_register_callback(&BluetoothObject::a2dp_callback));
  ESP_ERROR_CHECK(esp_a2d_source_register_data_callback(&BluetoothObject::a2dp_data_callback));

  // always initiate connection
  ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE));
  ESP_ERROR_CHECK(esp_bt_gap_get_device_name());
}

void BluetoothObject::transition(State next) {
  ESP_LOGI(kComponentTag, "state transition: %d -> %d", static_cast<int>(state_), static_cast<int>(next));
  state_ = next;
}

void BluetoothObject::task() {
  // do nothing right now
  if (auto msg = my_queue_->acquire_recv_handle(kNoWait)) {
    msg->visit(overloads{
      [this](const StartDiscovery&) {
        // can only start discovery if idle
        if (state_ != State::kIdle) return;

        if (auto err = start_discovery(); err != ESP_OK) {
          ESP_LOGE(kComponentTag, "start_discovery failed: %s", esp_err_to_name(err));
          return; // stay idle
        }

        transition(State::kDiscovery);
      },

      [this](const GapDeviceFound&) {
        if (state_ != State::kDiscovery) return;

        if (auto err = cancel_discovery(); err != ESP_OK) {
          ESP_LOGE(kComponentTag, "cancel_discovery failed: %s", esp_err_to_name(err));
          return; // stay discovering
        }

        transition(State::kDiscovered);
      },

      [](const GapDiscoveryStarted& event) {},

      [this](const GapDiscoveryStopped& event) {
        if (state_ != State::kDiscovered || !event.has_bda) return;

        esp_bd_addr_t bda = {0};
        std::memcpy(bda, event.bda, ESP_BD_ADDR_LEN);

        if (auto err = esp_a2d_source_connect(bda); err != ESP_OK) {
          ESP_LOGE(kComponentTag, "esp_a2d_source_connect failed: %s", esp_err_to_name(err));
          transition(State::kIdle);
          return;
        }

        transition(State::kConnecting);
      },

      [this](const GapPinRequest& event) {
        if (state_ != State::kConnecting) return;

        GapPinRequest mut_event;
        std::memcpy(&mut_event, &event, sizeof(GapPinRequest));

        if (auto err = respond_pin(mut_event); err != ESP_OK) {
          ESP_LOGE(kComponentTag, "respond_pin failed: %s", esp_err_to_name(err));
          transition(State::kIdle);
          return;
        }
      },

      [this](const GapSspRequest& event) {
        if (state_ != State::kConnecting) return;

        GapSspRequest mut_event;
        std::memcpy(&mut_event, &event, sizeof(GapSspRequest));

        if (auto err = respond_ssp(mut_event); err != ESP_OK) {
          ESP_LOGE(kComponentTag, "respond_ssp failed: %s", esp_err_to_name(err));
          transition(State::kIdle);
          return;
        }
      },

      [this](const GapAuthResult& result) {
        if (state_ != State::kConnecting) return;

        if (result.success) {
          ESP_LOGI(kComponentTag, "auth successful");
          // stay connecting, final connection resolution determined by a2dp callback
        } else {
          ESP_LOGI(kComponentTag, "auth failed");
          transition(State::kIdle);
        }
      },

      [this](const A2dpConnectionStatus& status) {
        if (state_ != State::kConnecting) return;

        if (status.connected) {
          transition(State::kConnected);
          std::memcpy(bda_, status.bda, ESP_BD_ADDR_LEN);
          esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
        } else {
          transition(State::kIdle);
        }
      },

      [this](const A2dpMediaControlAck& ack) {
        if (state_ != State::kConnected &&
            state_ != State::kStarting &&
            state_ != State::kStreaming && 
            state_ != State::kStopping &&
            state_ != State::kReady) return;

        if (ack.status == ESP_A2D_MEDIA_CTRL_ACK_BUSY) return;
        if (ack.status == ESP_A2D_MEDIA_CTRL_ACK_FAILURE) {
          if (esp_a2d_source_disconnect(bda_) != ESP_OK) {
            transition(State::kIdle); // force back to idle
          }
          return;
        }

        switch (ack.cmd) {
          case ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY:
            if (state_ != State::kConnected) return;
            transition(State::kStarting);
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            break;
          
          case ESP_A2D_MEDIA_CTRL_START:
            if (state_ != State::kStarting) return;
            transition(State::kStreaming);
            break;

          case ESP_A2D_MEDIA_CTRL_STOP:
            if (state_ != State::kStreaming) return;
            transition(State::kReady);
            break;

          default:
            // ignore other acks for now
            break;
        }
      },

      [](std::span<const std::uint8_t> blob) {}
    });
  }
}

esp_err_t BluetoothObject::start_discovery() {
  ESP_LOGI(kComponentTag, "starting discovery");
  return esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquiryDuration, 0);
}

esp_err_t BluetoothObject::cancel_discovery() {
  ESP_LOGI(kComponentTag, "cancelling discovery");
  return esp_bt_gap_cancel_discovery();
}

esp_err_t BluetoothObject::respond_pin(GapPinRequest& req) {
  ESP_LOGI(kComponentTag, "responding to PIN request");

  esp_bt_pin_code_t pin_code = {0};  
  const size_t pin_len = req.min_16_digits ? 16 : 4;

  // zero-initialize the PIN code
  for (size_t i = 0; i < pin_len; i++) {
    pin_code[i] = '0';
  }

  return esp_bt_gap_pin_reply(req.bda, true /* accept */, static_cast<uint8_t>(pin_len), pin_code);
}

esp_err_t BluetoothObject::respond_ssp(GapSspRequest& req) {
  ESP_LOGI(kComponentTag, "confirming SSP");
  return esp_bt_gap_ssp_confirm_reply(req.bda, true /* confirm */);
}

void BluetoothObject::gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
  static esp_bd_addr_t bda{};
  static bool bda_pending{};

  switch (event) {
    // device discovered during inquiry scan
    case ESP_BT_GAP_DISC_RES_EVT: {
      uint8_t *eir{nullptr}; // looking for an extended inquiry response

      // search properties for EIR (and CoD in the future)
      for (size_t i = 0; i < param->disc_res.num_prop; i++) {
        const esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
        if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
          eir = static_cast<uint8_t*>(prop->val);
          break;
        }
      }

      if (auto name = get_device_name(eir)) {
        ESP_LOGI(kComponentTag, "found device: %.*s", static_cast<int>(name->size()), name->data());

        if (*name == kTargetDeviceName && !bda_pending) {
          std::memcpy(bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
          bda_pending = true;
          instance_->my_queue_->send_message<>(GapDeviceFound {});
        }
      }

      break;
    }

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
      if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        GapDiscoveryStopped event ;

        event.has_bda = bda_pending;
        if (bda_pending) {
          std::memcpy(event.bda, bda, ESP_BD_ADDR_LEN);
        }

        instance_->my_queue_->send_message<>(event);
      } else {
        instance_->my_queue_->send_message<>(GapDiscoveryStarted {});
      }

      bda_pending = false;
      break;
    }

    case ESP_BT_GAP_AUTH_CMPL_EVT: {
      bool success = param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS;
      ESP_LOGI(kComponentTag, "GAP: auth status = %d", (int) success);
      instance_->my_queue_->send_message<>(GapAuthResult { .success = success });
      break;
    }

    case ESP_BT_GAP_PIN_REQ_EVT: {
      ESP_LOGI(kComponentTag, "GAP: got PIN request");
      GapPinRequest event;
      event.min_16_digits = param->pin_req.min_16_digit;
      std::memcpy(event.bda, param->pin_req.bda, ESP_BD_ADDR_LEN);
      instance_->my_queue_->send_message<>(event);
      break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT: {
      ESP_LOGI(kComponentTag, "GAP: got SSP confirm");
      GapSspRequest event;
      std::memcpy(event.bda, param->cfm_req.bda, ESP_BD_ADDR_LEN);
      instance_->my_queue_->send_message<>(event);
      break;
    }

    default: 
      // ignore other events for now
      break;
  }
}

void BluetoothObject::avrcp_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t* param) {}

void BluetoothObject::a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
  switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
      A2dpConnectionStatus event;

      if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        event.connected = true;
        std::memcpy(event.bda, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
        instance_->my_queue_->send_message<>(event);
      } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        event.connected = false;
        instance_->my_queue_->send_message<>(event);
      }
      break;
    }

    case ESP_A2D_AUDIO_STATE_EVT: {
      ESP_LOGI(kComponentTag, "A2DP streaming started: %d", (int)(param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED));
      break;
    }

    case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
      instance_->my_queue_->send_message<>(A2dpMediaControlAck {
        .cmd = param->media_ctrl_stat.cmd,
        .status = param->media_ctrl_stat.status
      });
      break;
    }
    
    default:
      // don't care about others for now
      break;
  }
}

int32_t BluetoothObject::a2dp_data_callback(unsigned char* data, int32_t len) {
  if (!data || len < 0) return len;

  std::size_t bytes_received = instance_->audio_queue_->read(std::span<uint8_t>(data, len));
  if (bytes_received < len) {
    std::memset(data + bytes_received, 0, len - bytes_received);
  }

  // ESP_LOGI(kComponentTag, "data callback: %d bytes (got %zu)", len, bytes_received);

  return len;
}

} // namespace bt