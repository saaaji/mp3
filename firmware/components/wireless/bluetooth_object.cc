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

}

namespace bt {

// null-initialize the singleton pointer
BluetoothObject* BluetoothObject::instance_ = nullptr;

BluetoothObject::BluetoothObject(
  rtos::MessageQueue<Command>* my_queue,
  rtos::MessageQueue<::wifi::Command>* wifi_queue
)
  : ActiveObject("BluetoothObject", ActiveObject::MemoryLoad::kStandard, ActiveObject::Priority::kHigh, 1000), 
    my_queue_(my_queue),
    wifi_queue_(wifi_queue) {
      assert(!instance_ && "only one BluetoothObject instance allowed");
      instance_ = this;

      // start discovery
      my_queue_->send_message<>(Command::kStartDiscovery);
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

  // configure SSP to advertise as input/output capable device
  esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
  esp_bt_io_cap_t io_cap = ESP_BT_IO_CAP_IO;
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

void BluetoothObject::task() {
  // do nothing right now
  if (auto msg = my_queue_->acquire_recv_handle(kNoWait)) {
    msg->visit(overloads{
      [this](Command cmd) {
        switch (cmd) {
          case Command::kStartDiscovery:
            // zero indicates unlimited responses
            esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquiryDuration, 0);
            state_ = State::kDiscovery;
            break;
        }
      },
      [](std::span<const std::uint8_t> blob) {}
    });
  }

  switch (state_) {
    case State::kDiscovery:
      // nothing right now
      /// TODO: send discovered devices to wifi queue
      break;
  }
}

void BluetoothObject::gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
  switch (event) {
    // device discovered
    case ESP_BT_GAP_DISC_RES_EVT: {
      if (instance_->state_ == State::kDiscovery) {
        uint32_t device_class{};
        uint8_t* extended_inquiry_response{nullptr};

        // search properties
        for (size_t i = 0; i < param->disc_res.num_prop; i++) {
          const esp_bt_gap_dev_prop_t* prop = &param->disc_res.prop[i];

          switch (prop->type) {
            case ESP_BT_GAP_DEV_PROP_COD:
              device_class = *static_cast<uint32_t*>(prop->val);
              break;
            
            case ESP_BT_GAP_DEV_PROP_EIR:
              extended_inquiry_response = static_cast<uint8_t*>(prop->val);
              break;

            default:
              // don't care about other properties
              break;
          }
        }

        // filter by rendering class =~ speakers
        if (!esp_bt_gap_is_valid_cod(device_class) || (!esp_bt_gap_get_cod_srvc(device_class) & ESP_BT_COD_SRVC_RENDERING)) {
          return;
        }

        if (extended_inquiry_response) {
          
        }
      }
      break;
    }

    // GAP state change
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
      if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        /// TODO: change state to connect
      } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
        ESP_LOGI(kComponentTag, "discovery started");
      }
      break;
    }

    // auth
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
      if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
        ESP_LOGI(kComponentTag, "auth succeeded: %s", param->auth_cmpl.device_name);
        ESP_LOG_BUFFER_HEX(kComponentTag, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
      } else {
        ESP_LOGE(kComponentTag, "auth failed with status: %d", param->auth_cmpl.stat);
      }
      break;
    }

    // legacy pairing via PIN
    case ESP_BT_GAP_PIN_REQ_EVT: {
      esp_bt_pin_code_t pin_code = {0};
      const uint8_t code_len = param->pin_req.min_16_digit ? 16 : 4;
      ESP_ERROR_CHECK(esp_bt_gap_pin_reply(param->pin_req.bda, true, code_len, pin_code));
      break;
    }

    // SSP
    case ESP_BT_GAP_CFM_REQ_EVT:
      ESP_ERROR_CHECK(esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true));
      break;

    default:
      // ignore other events for now
      break;
  }
}

void BluetoothObject::avrcp_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t* param) {}

void BluetoothObject::a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {}

signed int BluetoothObject::a2dp_data_callback(unsigned char* data, int32_t len) {}

} // namespace bt