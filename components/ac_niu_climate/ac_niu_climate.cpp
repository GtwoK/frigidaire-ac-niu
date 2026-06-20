#include "ac_niu_climate.h"

#include <cmath>
#include <cstdio>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::ac_niu {

static const char *const TAG = "ac_niu";

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
  ESP_LOGCONFIG(TAG, "    Filter door: %s", YESNO(this->filter_door_sensor_ != nullptr));
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
  if (!this->engaged_ || !this->initialized_) {
    ESP_LOGW(TAG, "Control rejected: bus is not ready");
    return;
  }

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

  if (call.get_preset().has_value()) {
    if (*call.get_preset() == climate::CLIMATE_PRESET_ECO)
      this->queue_set_(0x0426, {0x01});
    else if (*call.get_preset() == climate::CLIMATE_PRESET_NONE)
      this->queue_set_(0x0426, {0x00});
  }

  this->next_tx_ms_ = millis();
}

void AcNiuClimate::process_tx_(uint32_t now) {
  if (this->engaged_ && now - this->last_rx_ms_ > 3500) {
    ESP_LOGW(TAG, "AC stopped replying; returning to discovery");
    this->reset_session_(now);
  }
  if (!this->pending_ack_.empty()) {
    if (static_cast<int32_t>(now - this->ack_due_ms_) < 0)
      return;
    this->send_body_(this->pending_ack_, "event acknowledgement");
    this->pending_ack_.clear();
    this->next_tx_ms_ = now + 40;
    return;
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
    if (now - this->command_sent_ms_ >= 500) {
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
    if (byte == 0xC6 && !this->rx_buf_.empty())
      this->rx_buf_.clear();
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
    if (checksum == this->rx_buf_[needed - 1])
      this->handle_frame_(this->rx_buf_);
    else
      ESP_LOGW(TAG, "RX checksum failure");
    this->rx_buf_.clear();
  }
}

void AcNiuClimate::handle_frame_(const std::vector<uint8_t> &frame) {
  const uint32_t now = millis();
  this->last_rx_ms_ = now;
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
      frame[13] == 0x07)
    this->next_tx_ms_ = now + 40;

  if (this->command_sent_ && frame.size() >= 15 && frame[11] == (this->command_reg_ >> 8) &&
      frame[12] == (this->command_reg_ & 0xFF)) {
    if (frame[13] != 0x05)
      ESP_LOGW(TAG, "Unexpected SET response %02X for register %04X", frame[13], this->command_reg_);
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
        if (this->filter_door_sensor_ != nullptr)
          this->filter_door_sensor_->publish_state(v == 0);
        break;
      case 0x0622:
        if (this->air_quality_raw_sensor_ != nullptr)
          this->air_quality_raw_sensor_->publish_state(v * 100.0f / 255.0f);
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
    ESP_LOGD(TAG, "Event register %04X value %02X length %u", reg, v, static_cast<unsigned>(value_len));
  } else if (marker == 0x06) {
    this->log_unknown_event_("missing event value", frame, reg, marker);
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

void AcNiuClimate::send_body_(const std::vector<uint8_t> &body, const char *tag) {
  std::vector<uint8_t> frame = {0xC6, static_cast<uint8_t>(3 + body.size()), this->tx_type_, this->tx_seq_, 0xAD};
  frame.insert(frame.end(), body.begin(), body.end());
  uint8_t checksum = 0;
  for (size_t i = 1; i < frame.size(); i++)
    checksum ^= frame[i];
  frame.push_back(checksum);
  this->write_array(frame.data(), frame.size());
  ESP_LOGD(TAG, "TX %s type=%02X seq=%02X", tag, this->tx_type_, this->tx_seq_);
  if (++this->tx_seq_ == 0)
    this->tx_type_ = 0x80 | ((this->tx_type_ + 1) & 0x0F);
}

void AcNiuClimate::queue_set_(uint16_t reg, std::initializer_list<uint8_t> values) {
  this->queue_set_(reg, std::vector<uint8_t>(values));
}

void AcNiuClimate::queue_set_(uint16_t reg, const std::vector<uint8_t> &values) {
  this->command_queue_.push_back({reg, values});
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
  this->engaged_ = false;
  this->initialized_ = false;
  this->init_step_ = 0;
  this->poll_count_ = 0;
  this->command_pending_ = false;
  this->command_sent_ = false;
  this->command_queue_.clear();
  this->pending_ack_.clear();
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

}  // namespace esphome::ac_niu
