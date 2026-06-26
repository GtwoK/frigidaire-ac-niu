#include "ac_niu_climate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_API_USER_DEFINED_ACTIONS
#include "esphome/components/api/user_services.h"
#include "esphome/core/string_ref.h"

namespace esphome::api {

// ESPHome 2026.6 can omit these API action specializations from the final link.
// Weak definitions preserve compatibility while deferring to ESPHome when it supplies them.
template<> __attribute__((weak)) StringRef get_execute_arg_value<StringRef>(const ExecuteServiceArgument &arg) {
  return arg.string_;
}

template<> __attribute__((weak)) enums::ServiceArgType to_service_arg_type<StringRef>() {
  return enums::SERVICE_ARG_TYPE_STRING;
}

}  // namespace esphome::api
#endif

namespace esphome::ac_niu {

static const char *const TAG = "ac_niu";
static constexpr uint32_t REPLY_WINDOW_MS = 250;

void AcNiuSleepSwitch::write_state(bool state) {
  if (this->parent_ != nullptr)
    this->parent_->set_sleep_mode(state);
}

static const std::vector<std::vector<uint8_t>> INIT_FRAME_TEMPLATES = {
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x00, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x01, 0x06, 0x00, 0x4E, 0x49, 0x55},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x07, 0x00},
    {0x41, 0x43, 0x31, 0x53, 0x43, 0x31, 0x06, 0x80, 0x04, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x07, 0x04},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0xF0, 0x06, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x60, 0x06, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x01, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x32, 0x06, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x04, 0x73, 0x06, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x23, 0x06, 0x52, 0x50, 0x31, 0xFF},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x07, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x0A, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x01, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x0A, 0x04, 0x30, 0x31},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x01, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x0A, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x02, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x01, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x02, 0x04},
    {},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x01, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x02, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x01, 0x00},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x2E, 0x06, 0x01},
    {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x28, 0x04, 0x01},
};

static const std::vector<uint8_t> DISCOVERY = {0x00, 0x00, 0x00, 0x4E, 0x49, 0x55, 0x00, 0x01, 0x00};
static const std::vector<uint8_t> POLL = {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x01, 0x00};

static uint32_t hash_mac(const uint8_t mac[6], uint32_t domain) {
  uint32_t hash = 2166136261U ^ domain;
  for (size_t i = 0; i < 6; i++)
    hash = (hash ^ mac[i]) * 16777619U;
  return hash;
}

static int8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

static std::string decimal_identifier(const uint8_t mac[6], uint32_t domain, uint32_t base, uint32_t range,
                                      int width) {
  char value[10];
  snprintf(value, sizeof(value), "%0*u", width, base + hash_mac(mac, domain) % range);
  return value;
}

void AcNiuClimate::setup() {
  this->build_identity_frames_();
  this->rx_buf_.reserve(64);
  this->next_tx_ms_ = millis() + 1000;
}

void AcNiuClimate::loop() {
  this->process_rx_();
  this->process_tx_(millis());
}

void AcNiuClimate::dump_config() {
  LOG_CLIMATE("", "Electrolux NIU Climate", this);
  ESP_LOGCONFIG(TAG, "  Protocol: 9600 8E1");
  ESP_LOGCONFIG(TAG, "  Identity: generated from ESP hardware ID");
  ESP_LOGCONFIG(TAG, "  Optional telemetry:");
  ESP_LOGCONFIG(TAG, "    Child lock: %s", YESNO(this->child_lock_sensor_ != nullptr));
  ESP_LOGCONFIG(TAG, "    PureAir filter installed: %s", YESNO(this->pureair_filter_installed_sensor_ != nullptr));
  ESP_LOGCONFIG(TAG, "    Coil temperature: %s", YESNO(this->coil_temperature_sensor_ != nullptr));
  ESP_LOGCONFIG(TAG, "    Air quality: %s", YESNO(this->air_quality_sensor_ != nullptr));
}

climate::ClimateTraits AcNiuClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
  traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
  traits.add_supported_mode(climate::CLIMATE_MODE_DRY);
  traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
  traits.add_supported_swing_mode(climate::CLIMATE_SWING_OFF);
  traits.add_supported_swing_mode(climate::CLIMATE_SWING_VERTICAL);
  traits.add_supported_preset(climate::CLIMATE_PRESET_NONE);
  traits.add_supported_preset(climate::CLIMATE_PRESET_ECO);
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(30.0f);
  traits.set_visual_temperature_step(1.0f);
  return traits;
}

void AcNiuClimate::control(const climate::ClimateCall &call) {
  if (!this->engaged_) {
    this->log_bus_state_("Control rejected: AC has not answered discovery");
    return;
  }
  if (!this->initialized_)
    this->log_bus_state_("Control queued while NIU initialization is still running");

  climate::ClimateMode effective_mode = this->mode;
  if (call.get_mode().has_value()) {
    effective_mode = *call.get_mode();
    switch (effective_mode) {
      case climate::CLIMATE_MODE_OFF:
        this->queue_set_(0x1000, {0x00});
        break;
      case climate::CLIMATE_MODE_COOL:
        this->queue_set_(0x1000, {0x01});
        break;
      case climate::CLIMATE_MODE_FAN_ONLY:
        this->queue_set_(0x1000, {0x03});
        break;
      case climate::CLIMATE_MODE_DRY:
        this->queue_set_(0x1000, {0x05});
        break;
      default:
        break;
    }
  }

  const bool off = effective_mode == climate::CLIMATE_MODE_OFF;
  const bool fan_only = effective_mode == climate::CLIMATE_MODE_FAN_ONLY;
  if (call.get_fan_mode().has_value() && !off) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW:
        this->queue_set_(0x1002, {0x01});
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        this->queue_set_(0x1002, {0x02});
        break;
      case climate::CLIMATE_FAN_HIGH:
        this->queue_set_(0x1002, {0x04});
        break;
      case climate::CLIMATE_FAN_AUTO:
        if (!fan_only)
          this->queue_set_(0x1002, {0x07});
        break;
      default:
        break;
    }
  }

  if (call.get_swing_mode().has_value())
    this->queue_set_(0x1009, {*call.get_swing_mode() == climate::CLIMATE_SWING_VERTICAL ? (uint8_t) 0x01
                                                                                       : (uint8_t) 0x00});

  if (call.get_target_temperature().has_value() && !off && !fan_only) {
    const float celsius = *call.get_target_temperature();
    if (this->ac_uses_fahrenheit_) {
      const uint8_t fahrenheit = static_cast<uint8_t>(lroundf(celsius * 9.0f / 5.0f + 32.0f));
      this->queue_set_(0x0432, {0x01, 0x00, fahrenheit, 0x00});
    } else {
      this->queue_set_(0x0432, {0x00, 0x00, static_cast<uint8_t>(lroundf(celsius)), 0x00});
    }
  }

  if (call.get_preset().has_value() && !off && !fan_only) {
    if (*call.get_preset() == climate::CLIMATE_PRESET_ECO)
      this->queue_set_(0x0426, {0x01});
    else if (*call.get_preset() == climate::CLIMATE_PRESET_NONE)
      this->queue_set_(0x0426, {0x00});
  } else if (call.get_preset().has_value()) {
    this->publish_state();
  }
}

void AcNiuClimate::process_tx_(uint32_t now) {
  if (this->engaged_ && now - this->last_rx_ms_ > 3500) {
    this->log_bus_state_("AC stopped replying; returning to discovery");
    this->reset_session_(now);
  }
  if (this->waiting_tx_reply_) {
    if (static_cast<int32_t>(now - this->tx_reply_deadline_ms_) < 0)
      return;
    ESP_LOGV(TAG, "No %s reply before pacing timeout; continuing",
             this->tx_reply_tag_ != nullptr ? this->tx_reply_tag_ : "frame");
    this->waiting_tx_reply_ = false;
  }
  if (!this->pending_ack_.empty()) {
    if (static_cast<int32_t>(now - this->ack_due_ms_) < 0)
      return;
    this->event_ack_seq_ = this->tx_seq_;
    this->send_body_(this->pending_ack_, "event acknowledgement");
    this->pending_ack_.clear();
    this->waiting_event_ack_reply_ = true;
    this->event_ack_reply_deadline_ms_ = now + 180;
    this->next_tx_ms_ = now + 40;
    return;
  }
  if (this->waiting_event_ack_reply_) {
    if (static_cast<int32_t>(now - this->event_ack_reply_deadline_ms_) < 0)
      return;
    ESP_LOGV(TAG, "No event acknowledgement reply before timeout; resuming poll");
    this->waiting_event_ack_reply_ = false;
  }
  if (static_cast<int32_t>(now - this->next_tx_ms_) < 0)
    return;

  if (!this->engaged_) {
    this->send_body_(DISCOVERY, "discovery");
    this->next_tx_ms_ = now + 1000;
    return;
  }

  if (!this->initialized_) {
    if (this->init_step_ < this->init_frames_.size()) {
      this->send_body_(this->init_frames_[this->init_step_++], "initialization");
      this->next_tx_ms_ = now + 120;
      return;
    }
    this->initialized_ = true;
    this->poll_count_ = 0;
    ESP_LOGI(TAG, "NIU initialization complete; requesting state dump");
  }

  if (!this->command_pending_ && !this->command_queue_.empty()) {
    QueuedCommand command = std::move(this->command_queue_.front());
    this->command_queue_.erase(this->command_queue_.begin());
    this->command_reg_ = command.reg;
    this->command_values_ = std::move(command.values);
    this->command_pending_ = true;
    this->command_sent_ = false;
  }

  if (this->command_pending_) {
    if (!this->command_sent_) {
      std::vector<uint8_t> body = {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55,
                                   static_cast<uint8_t>(this->command_reg_ >> 8),
                                   static_cast<uint8_t>(this->command_reg_), 0x04};
      body.insert(body.end(), this->command_values_.begin(), this->command_values_.end());
      this->send_body_(body, "command");
      this->command_sent_ = true;
      this->command_sent_ms_ = now;
      this->next_tx_ms_ = now + 500;
      return;
    }
    if (now - this->command_sent_ms_ >= 1200) {
      ESP_LOGW(TAG, "SET timeout for register %04X", this->command_reg_);
      this->command_pending_ = false;
      this->command_sent_ = false;
      this->next_tx_ms_ = now + 40;
    }
    return;
  }

  if (this->poll_count_ >= 10) {
    this->poll_count_ = 0;
    this->send_body_(this->identity_frame_, "identity");
    this->next_tx_ms_ = now + 100;
    return;
  }

  this->send_body_(POLL, "poll");
  this->poll_count_++;
  this->next_tx_ms_ = now + 1000;
}

void AcNiuClimate::process_rx_() {
  uint8_t byte;
  while (this->available()) {
    if (!this->read_byte(&byte))
      return;
    if (this->rx_buf_.empty() && byte != 0xC6)
      continue;
    this->rx_buf_.push_back(byte);
    if (this->rx_buf_.size() < 2)
      continue;

    const size_t needed = static_cast<size_t>(this->rx_buf_[1]) + 3;
    if (needed < 6 || needed > 128) {
      this->rx_buf_.clear();
      continue;
    }
    if (this->rx_buf_.size() < needed)
      continue;

    uint8_t checksum = 0;
    for (size_t i = 1; i + 1 < needed; i++)
      checksum ^= this->rx_buf_[i];
    if (checksum == this->rx_buf_[needed - 1]) {
      this->rx_good_frames_++;
      this->handle_frame_(this->rx_buf_);
      this->rx_buf_.clear();
    } else {
      if (this->waiting_event_ack_reply_ && this->is_event_ack_reply_(this->rx_buf_)) {
        this->waiting_event_ack_reply_ = false;
        this->next_tx_ms_ = millis() + 40;
        ESP_LOGD(TAG, "Ignoring bad checksum on event acknowledgement reply seq=%02X", this->rx_buf_[3]);
        this->rx_buf_.clear();
        continue;
      }
      this->rx_bad_frames_++;
      char raw[128 * 3];
      format_hex_pretty_to(raw, sizeof(raw), this->rx_buf_.data(), this->rx_buf_.size(), ' ');
      ESP_LOGW(TAG, "RX checksum failure calculated=%02X received=%02X raw=[%s]", checksum,
               this->rx_buf_[needed - 1], raw);
      // A dropped byte can make the start of the next frame complete the damaged frame's
      // advertised length. Preserve that next sentinel so subsequent bytes can recover it.
      auto next = std::find(this->rx_buf_.begin() + 1, this->rx_buf_.end(), 0xC6);
      if (next == this->rx_buf_.end())
        this->rx_buf_.clear();
      else
        this->rx_buf_.erase(this->rx_buf_.begin(), next);
    }
  }
}

void AcNiuClimate::handle_frame_(const std::vector<uint8_t> &frame) {
  if (this->is_tx_echo_(frame)) {
    ESP_LOGV(TAG, "Ignoring local TX echo type=%02X seq=%02X", frame[2], frame[3]);
    return;
  }

  const uint32_t now = millis();
  this->last_rx_ms_ = now;
  this->clear_tx_reply_wait_(frame);
  if (!this->engaged_) {
    this->engaged_ = true;
    this->initialized_ = false;
    this->init_step_ = 0;
    this->poll_count_ = 0;
    this->next_tx_ms_ = now + 40;
    ESP_LOGI(TAG, "AC discovered; NIU initialization started");
  } else if (!this->initialized_) {
    this->next_tx_ms_ = now + 50;
  }

  if (this->initialized_ && frame.size() >= 15 && frame[11] == 0x02 && frame[12] == 0x02 &&
      frame[13] == 0x07) {
    this->waiting_event_ack_reply_ = false;
    this->next_tx_ms_ = now + 40;
  }

  if (this->command_sent_ && frame.size() >= 15 && frame[11] == (this->command_reg_ >> 8) &&
      frame[12] == (this->command_reg_ & 0xFF)) {
    if (frame[13] != 0x05) {
      char raw[128 * 3];
      format_hex_pretty_to(raw, sizeof(raw), frame.data(), frame.size(), ' ');
      ESP_LOGW(TAG, "Unexpected SET response %02X for register %04X raw=[%s]", frame[13], this->command_reg_, raw);
    }
    this->command_pending_ = false;
    this->command_sent_ = false;
    this->next_tx_ms_ = now + 40;
  }

  if (frame.size() >= 25 && frame[11] == 0x02 && frame[12] == 0x01 && frame[13] == 0x02 && frame[14] == 0xAD)
    this->handle_event_(frame);
}

void AcNiuClimate::handle_event_(const std::vector<uint8_t> &frame) {
  const uint8_t reg_hi = frame[21];
  const uint8_t reg_lo = frame[22];
  const uint8_t marker = frame[23];
  const uint16_t reg = (static_cast<uint16_t>(reg_hi) << 8) | reg_lo;
  const size_t value_len = frame.size() - 25;
  const uint8_t *value = &frame[24];

  bool recognized = true;
  bool value_valid = true;

  if (marker == 0x06 && value_len > 0) {
    const uint8_t v = value[value_len - 1];
    switch (reg) {
      case 0x0401:
        value_valid = v == 0x00 || v == 0x02;
        this->ac_powered_ = v == 0x02;
        this->set_confirmed_mode_(this->ac_powered_ ? this->operating_mode_ : climate::CLIMATE_MODE_OFF);
        this->publish_state();
        break;
      case 0x1000:
        value_valid = v == 0x00 || v == 0x01 || v == 0x03 || v == 0x05;
        if (v == 0x01)
          this->operating_mode_ = climate::CLIMATE_MODE_COOL;
        else if (v == 0x03)
          this->operating_mode_ = climate::CLIMATE_MODE_FAN_ONLY;
        else if (v == 0x05)
          this->operating_mode_ = climate::CLIMATE_MODE_DRY;
        this->set_confirmed_mode_(this->ac_powered_ ? this->operating_mode_ : climate::CLIMATE_MODE_OFF);
        this->publish_state();
        break;
      case 0x1002:
        value_valid = v == 0x01 || v == 0x02 || v == 0x04 || v == 0x07;
        if (v == 0x01)
          this->fan_mode = climate::CLIMATE_FAN_LOW;
        else if (v == 0x02)
          this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        else if (v == 0x04)
          this->fan_mode = climate::CLIMATE_FAN_HIGH;
        else if (v == 0x07)
          this->fan_mode = climate::CLIMATE_FAN_AUTO;
        this->publish_state();
        break;
      case 0x1003:
        value_valid = v == 0x01 || v == 0x02 || v == 0x04;
        break;
      case 0x0420:
        value_valid = v == 0x00 || v == 0x01;
        this->ac_uses_fahrenheit_ = v == 0x01;
        break;
      case 0x0426:
        value_valid = v == 0x00 || v == 0x01;
        this->preset = v != 0 ? climate::CLIMATE_PRESET_ECO : climate::CLIMATE_PRESET_NONE;
        this->publish_state();
        break;
      case 0x0428:
        value_valid = v == 0x00 || v == 0x01;
        if (value_valid) {
          if (this->sleep_switch_ != nullptr)
            this->sleep_switch_->publish_state(v == 0x01);
        }
        break;
      case 0x1009:
        value_valid = v == 0x00 || v == 0x01;
        this->swing_mode = v != 0 ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;
        this->publish_state();
        break;
      case 0x0463:
        value_valid = v == 0x00 || v == 0x01;
        if (this->child_lock_sensor_ != nullptr)
          this->child_lock_sensor_->publish_state(v == 0);
        break;
      case 0x0651:
        value_valid = v == 0x00 || v == 0x01;
        if (this->pureair_filter_installed_sensor_ != nullptr)
          this->pureair_filter_installed_sensor_->publish_state(v == 0x01);
        break;
      case 0x0622:
        value_valid = value_len >= 2;
        if (this->air_quality_raw_sensor_ != nullptr)
          this->air_quality_raw_sensor_->publish_state(value_valid ? (static_cast<uint16_t>(value[0]) << 8) | value[1] : v);
        break;
      case 0x0621:
        value_valid = v <= 0x02;
        if (this->air_quality_sensor_ != nullptr)
          this->air_quality_sensor_->publish_state(v == 0 ? "Green" : v == 1 ? "Yellow" : v == 2 ? "Red" : "Unknown");
        break;
      case 0x047C:
        value_valid = v == 0x00 || v == 0x03;
        if (this->filter_status_sensor_ != nullptr)
          this->filter_status_sensor_->publish_state(v == 0 ? "Good" : v == 3 ? "Clean" : "Unknown");
        break;
      case 0x0432:
      case 0x0430:
      case 0x0435:
        value_valid = value_len >= 4 && (value[0] == 0x00 || value[0] == 0x01);
        if (value_valid) {
          float temperature = value[2];
          if (value[0] == 0x01)
            temperature = (temperature - 32.0f) * 5.0f / 9.0f;
          if (reg == 0x0432)
            this->set_confirmed_target_temperature_(temperature);
          else if (reg == 0x0430)
            this->current_temperature = temperature;
          else if (this->coil_temperature_sensor_ != nullptr)
            this->coil_temperature_sensor_->publish_state(temperature);
          if (reg == 0x0432 || reg == 0x0430)
            this->publish_state();
        }
        break;
      default:
        recognized = false;
        break;
    }
    if (!recognized)
      this->log_unknown_event_("unmapped register", frame, reg, marker);
    else if (!value_valid)
      this->log_unknown_event_("unexpected value", frame, reg, marker);
    char raw_value[128 * 3];
    format_hex_pretty_to(raw_value, sizeof(raw_value), value, value_len, ' ');
    ESP_LOGD(TAG, "Event register %04X value=[%s] length %u", reg, raw_value, static_cast<unsigned>(value_len));
  } else if (marker == 0x06) {
    this->log_unknown_event_("missing event value", frame, reg, marker);
  } else if (marker == 0x04 && reg == 0x0024) {
    ESP_LOGI(TAG, "WiFi setup requested; ignored");
  } else if (marker != 0x04 || reg != 0x0401) {
    this->log_unknown_event_("unrecognized event marker", frame, reg, marker);
  }

  std::vector<uint8_t> ack = {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x02, 0x02, 0x06, 0xAD,
                              frame[18], frame[19], frame[20], frame[15], frame[16], frame[17], reg_hi, reg_lo};
  if (marker == 0x04)
    ack.insert(ack.end(), {0x0C, 0x01});
  else
    ack.push_back(0x07);
  this->pending_ack_ = std::move(ack);
  this->ack_due_ms_ = millis() + 40;
}

void AcNiuClimate::log_unknown_event_(const char *reason, const std::vector<uint8_t> &frame, uint16_t reg,
                                      uint8_t marker) {
  char raw[128 * 3];
  format_hex_pretty_to(raw, sizeof(raw), frame.data(), frame.size(), ' ');
  ESP_LOGI(TAG, "UNKNOWN RX: %s type=%02X seq=%02X reg=%04X marker=%02X raw=[%s]", reason, frame[2], frame[3],
           reg, marker, raw);
}

bool AcNiuClimate::is_tx_echo_(const std::vector<uint8_t> &frame) const {
  return frame.size() >= 11 && frame[4] == 0xAD && frame[5] == 0x41 && frame[6] == 0x43 && frame[7] == 0x31 &&
         frame[8] == 0x4E && frame[9] == 0x49 && frame[10] == 0x55;
}

bool AcNiuClimate::is_event_ack_reply_(const std::vector<uint8_t> &frame) const {
  return frame.size() >= 15 && frame[1] == 0x0C && (frame[2] & 0xF0) == 0x00 && frame[3] == this->event_ack_seq_ &&
         frame[4] == 0xAD && frame[5] == 0x4E && frame[6] == 0x49 && frame[7] == 0x55 && frame[8] == 0x41 &&
         frame[9] == 0x43 && frame[10] == 0x31 && frame[11] == 0x02 && frame[12] == 0x02 && frame[13] == 0x07;
}

void AcNiuClimate::clear_tx_reply_wait_(const std::vector<uint8_t> &frame) {
  if (this->waiting_tx_reply_ && frame.size() >= 4 && frame[3] == this->tx_reply_seq_)
    this->waiting_tx_reply_ = false;
}

void AcNiuClimate::send_body_(const std::vector<uint8_t> &body, const char *tag) {
  std::vector<uint8_t> frame = {0xC6, static_cast<uint8_t>(3 + body.size()), this->tx_type_, this->tx_seq_, 0xAD};
  frame.insert(frame.end(), body.begin(), body.end());
  uint8_t checksum = 0;
  for (size_t i = 1; i < frame.size(); i++)
    checksum ^= frame[i];
  frame.push_back(checksum);
  this->write_array(frame.data(), frame.size());
  ESP_LOGD(TAG, "TX %s type=%02X seq=%02X", tag, this->tx_type_, this->tx_seq_);
  this->waiting_tx_reply_ = true;
  this->tx_reply_seq_ = this->tx_seq_;
  this->tx_reply_deadline_ms_ = millis() + REPLY_WINDOW_MS;
  this->tx_reply_tag_ = tag;
  if (++this->tx_seq_ == 0)
    this->tx_type_ = 0x80 | ((this->tx_type_ + 1) & 0x0F);
}

void AcNiuClimate::queue_set_(uint16_t reg, std::initializer_list<uint8_t> values) {
  this->queue_set_(reg, std::vector<uint8_t>(values));
}

void AcNiuClimate::queue_set_(uint16_t reg, const std::vector<uint8_t> &values) {
  this->command_queue_.push_back({reg, values});
}

void AcNiuClimate::set_sleep_mode(bool enabled) {
  this->queue_set_(0x0428, {enabled ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00)});
}

void AcNiuClimate::send_manual_set_hex(const std::string &command) {
  std::vector<uint8_t> bytes;
  bytes.reserve(command.size() / 2);
  int8_t high = -1;
  for (char c : command) {
    const int8_t nibble = hex_nibble(c);
    if (nibble >= 0) {
      if (high < 0)
        high = nibble;
      else {
        bytes.push_back(static_cast<uint8_t>((high << 4) | nibble));
        high = -1;
      }
    } else if (c != ' ' && c != ':' && c != '-' && c != ',' && c != '_') {
      ESP_LOGW(TAG, "Manual SET rejected: invalid character '%c'", c);
      return;
    }
  }
  if (high >= 0 || bytes.size() < 3 || bytes.size() > 66) {
    ESP_LOGW(TAG, "Manual SET rejected: expected a 2-byte register and 1-64 value bytes");
    return;
  }

  const uint16_t reg = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
  this->send_manual_set_(reg, std::vector<uint8_t>(bytes.begin() + 2, bytes.end()));
}

void AcNiuClimate::send_manual_set_(uint16_t reg, const std::vector<uint8_t> &values) {
  if (!this->engaged_) {
    this->log_bus_state_("Manual SET rejected: AC has not answered discovery");
    return;
  }
  if (!this->initialized_)
    this->log_bus_state_("Manual SET queued while NIU initialization is still running");
  char raw[64 * 3];
  format_hex_pretty_to(raw, sizeof(raw), values.data(), values.size(), ' ');
  ESP_LOGI(TAG, "Manual SET queued reg=%04X values=[%s]", reg, raw);
  this->queue_set_(reg, values);
}

void AcNiuClimate::build_identity_frames_() {
  uint8_t mac[6];
  get_mac_address_raw(mac);

  // The real NIU writes cached decimal identifiers into otherwise-empty AC registers.
  // Stable pseudonyms preserve the observed field shapes without embedding appliance data.
  const std::string pnc = decimal_identifier(mac, 0x504E43U, 100000000U, 900000000U, 9);
  const std::string serial = decimal_identifier(mac, 0x534552U, 10000000U, 90000000U, 8);

  this->init_frames_ = INIT_FRAME_TEMPLATES;
  this->init_frames_[4].insert(this->init_frames_[4].end(), pnc.begin(), pnc.end());
  this->init_frames_[19].insert(this->init_frames_[19].end(), serial.begin(), serial.end());

  this->identity_frame_ = {0x41, 0x43, 0x31, 0x4E, 0x49, 0x55, 0x00, 0x20, 0x06,
                           0x00, 0x02, 0x01, 0xFF, 0x00, 0x00};
  this->identity_frame_.insert(this->identity_frame_.end(), mac, mac + 6);
  // 0x04 precedes an IPv4 field. The NIU also reports 0.0.0.0 while offline, which is sufficient locally.
  this->identity_frame_.insert(this->identity_frame_.end(), {0x04, 0x00, 0x00, 0x00, 0x00});
  this->init_frames_[20] = this->identity_frame_;
}

void AcNiuClimate::reset_session_(uint32_t now) {
  this->session_resets_++;
  this->engaged_ = false;
  this->initialized_ = false;
  this->init_step_ = 0;
  this->poll_count_ = 0;
  this->command_pending_ = false;
  this->command_sent_ = false;
  this->command_queue_.clear();
  this->pending_ack_.clear();
  this->waiting_tx_reply_ = false;
  this->waiting_event_ack_reply_ = false;
  this->next_tx_ms_ = now;
}

void AcNiuClimate::set_confirmed_mode_(climate::ClimateMode mode) {
  this->mode = mode;
  this->sync_visible_target_temperature_();
}

void AcNiuClimate::set_confirmed_target_temperature_(float value) {
  this->confirmed_target_temperature_ = value;
  this->sync_visible_target_temperature_();
}

bool AcNiuClimate::target_is_available_() const {
  return this->mode != climate::CLIMATE_MODE_OFF && this->mode != climate::CLIMATE_MODE_FAN_ONLY;
}

void AcNiuClimate::sync_visible_target_temperature_() {
  this->target_temperature = this->target_is_available_() ? this->confirmed_target_temperature_ : NAN;
}

void AcNiuClimate::log_bus_state_(const char *reason) const {
  const uint32_t now = millis();
  const uint32_t rx_age = this->last_rx_ms_ == 0 ? 0 : now - this->last_rx_ms_;
  ESP_LOGW(TAG,
           "%s (engaged=%s initialized=%s init_step=%u/%u last_rx_age=%ums queue=%u pending=%s ack=%s "
           "tx_reply=%s ack_reply=%s "
           "rx_ok=%u rx_bad=%u resync=%u resets=%u)",
           reason, YESNO(this->engaged_), YESNO(this->initialized_), static_cast<unsigned>(this->init_step_),
           static_cast<unsigned>(this->init_frames_.size()), rx_age, static_cast<unsigned>(this->command_queue_.size()),
           YESNO(this->command_pending_), YESNO(!this->pending_ack_.empty()), YESNO(this->waiting_tx_reply_),
           YESNO(this->waiting_event_ack_reply_),
           static_cast<unsigned>(this->rx_good_frames_), static_cast<unsigned>(this->rx_bad_frames_),
           static_cast<unsigned>(this->rx_resyncs_), static_cast<unsigned>(this->session_resets_));
}

}  // namespace esphome::ac_niu
