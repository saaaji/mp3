#pragma once

#include "message_queue.hpp"
#include "commands.hpp"
#include "active_object.hpp"

extern "C" {

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

}

namespace bt {

class BluetoothObject : public ActiveObject {
public:
  BluetoothObject(
    BtQueue *my_queue,
    rtos::MessageQueue<::wifi::Command>* wifi_queue
  );

private:
  enum class State : uint8_t {
    kIdle,
    kDiscovery,
    kDiscovered,
    kConnecting, 
    kConnected,
    // kAuthComplete,

    // subset of connected
    kReady,
    kStarting,
    kStreaming,
    kStopping
    
    /**
     * can map out the state machine:
     *  Action[event | current_state] = ..
     * 
     * Action[start discovery | kIdle] = start discovery, transition to kDiscovery
     * Action[device found | kDiscovery] = cancel discovery, transition to kDiscovered
     * Action[discovery stopped | kDiscovered] = connect, transition to kConnecting
     * Action[connection complete | kConnecting] = wait for auth, stay in kConnecting
     * Action[auth successful | kConnecting] = log and notify, transition to kAuthComplete
     * Action[auth failed | kConnecting] = log and retry discovery, transition to kDiscovery
     */
  };

  BtQueue *my_queue_{nullptr};
  rtos::MessageQueue<::wifi::Command>* wifi_queue_{nullptr};

  State state_{State::kIdle};
  esp_bd_addr_t bda_{0};

  void initialize() override;
  void task() override;

  void transition(State next);
  esp_err_t start_discovery();
  esp_err_t cancel_discovery();
  esp_err_t respond_pin(GapPinRequest&);
  esp_err_t respond_ssp(GapSspRequest&);

  static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param);
  static void avrcp_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t* param);
  static void a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param);
  static int32_t a2dp_data_callback(unsigned char* data, int32_t len);

  /**
   * To handle BT events must store pointer to the singleton.
   * This is probably safe because only one BT object is 
   * (i.e., should be) created at a time, and its lifetime 
   * should be static or span the main function at least.
   * When calling BT handlers, we could check for NULL but 
   * it should never be NULL if a singleton has been created.
   */
  static BluetoothObject* instance_;
};

} // namespace bt