#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome::ac_niu {

class AcNiuClimate;

class AcNiuSleepSwitch : public switch_::Switch {
 public:
  void set_parent(AcNiuClimate *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  AcNiuClimate *parent_{nullptr};
};

class AcNiuClimate : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_child_lock_sensor(binary_sensor::BinarySensor *sensor) { this->child_lock_sensor_ = sensor; }
  void set_pureair_filter_installed_sensor(binary_sensor::BinarySensor *sensor) {
    this->pureair_filter_installed_sensor_ = sensor;
  }
  void set_coil_temperature_sensor(sensor::Sensor *sensor) { this->coil_temperature_sensor_ = sensor; }
  void set_air_quality_raw_sensor(sensor::Sensor *sensor) { this->air_quality_raw_sensor_ = sensor; }
  void set_air_quality_sensor(text_sensor::TextSensor *sensor) { this->air_quality_sensor_ = sensor; }
  void set_filter_status_sensor(text_sensor::TextSensor *sensor) { this->filter_status_sensor_ = sensor; }
  void set_sleep_switch(AcNiuSleepSwitch *sleep_switch) { this->sleep_switch_ = sleep_switch; }
  void set_sleep_mode(bool enabled);
  void send_manual_set_hex(const std::string &command);

 protected:
  struct QueuedCommand {
    uint16_t reg;
    std::vector<uint8_t> values;
  };

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void process_rx_();
  void process_tx_(uint32_t now);
  void handle_frame_(const std::vector<uint8_t> &frame);
  void handle_event_(const std::vector<uint8_t> &frame);
  void log_unknown_event_(const char *reason, const std::vector<uint8_t> &frame, uint16_t reg, uint8_t marker);
  bool is_tx_echo_(const std::vector<uint8_t> &frame) const;
  bool is_event_ack_reply_(const std::vector<uint8_t> &frame) const;
  void clear_tx_reply_wait_(const std::vector<uint8_t> &frame);
  void send_body_(const std::vector<uint8_t> &body, const char *tag);
  void queue_set_(uint16_t reg, std::initializer_list<uint8_t> values);
  void queue_set_(uint16_t reg, const std::vector<uint8_t> &values);
  void send_manual_set_(uint16_t reg, const std::vector<uint8_t> &values);
  void build_identity_frames_();
  void reset_session_(uint32_t now);
  void set_confirmed_mode_(climate::ClimateMode mode);
  void set_confirmed_target_temperature_(float value);
  void sync_visible_target_temperature_();
  bool target_is_available_() const;
  void log_bus_state_(const char *reason) const;

  uint8_t tx_seq_{1};
  uint8_t tx_type_{0x80};
  std::vector<uint8_t> rx_buf_;
  std::vector<std::vector<uint8_t>> init_frames_;
  std::vector<uint8_t> identity_frame_;
  bool engaged_{false};
  bool initialized_{false};
  uint32_t last_rx_ms_{0};
  uint32_t next_tx_ms_{0};
  bool waiting_tx_reply_{false};
  uint8_t tx_reply_seq_{0};
  uint32_t tx_reply_deadline_ms_{0};
  const char *tx_reply_tag_{nullptr};
  uint8_t poll_count_{0};
  size_t init_step_{0};
  bool ac_powered_{false};
  bool ac_uses_fahrenheit_{false};
  climate::ClimateMode operating_mode_{climate::CLIMATE_MODE_COOL};
  float confirmed_target_temperature_{NAN};

  std::vector<QueuedCommand> command_queue_;
  bool command_pending_{false};
  bool command_sent_{false};
  uint16_t command_reg_{0};
  std::vector<uint8_t> command_values_;
  uint32_t command_sent_ms_{0};
  std::vector<uint8_t> pending_ack_;
  uint32_t ack_due_ms_{0};
  bool waiting_event_ack_reply_{false};
  uint8_t event_ack_seq_{0};
  uint32_t event_ack_reply_deadline_ms_{0};
  uint32_t rx_good_frames_{0};
  uint32_t rx_bad_frames_{0};
  uint32_t rx_resyncs_{0};
  uint32_t session_resets_{0};

  binary_sensor::BinarySensor *child_lock_sensor_{nullptr};
  binary_sensor::BinarySensor *pureair_filter_installed_sensor_{nullptr};
  sensor::Sensor *coil_temperature_sensor_{nullptr};
  sensor::Sensor *air_quality_raw_sensor_{nullptr};
  text_sensor::TextSensor *air_quality_sensor_{nullptr};
  text_sensor::TextSensor *filter_status_sensor_{nullptr};
  AcNiuSleepSwitch *sleep_switch_{nullptr};
};

}  // namespace esphome::ac_niu
