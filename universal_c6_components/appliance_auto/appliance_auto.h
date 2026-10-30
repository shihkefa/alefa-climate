#pragma once
#include "protocol.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/matter/matter_component.h"
#include <app/clusters/fan-control-server/fan-control-delegate.h>
#include <atomic>
#include <deque>

namespace esphome::appliance_auto {
class ApplianceAuto;
enum class EntityProfile : uint8_t { ANY = 0, TAI_AC = 1, TAI_DEHUM = 2 };

class AutoServiceSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ApplianceAuto *p) { parent_ = p; }
  void set_service(uint8_t v) { service_ = v; }
  uint8_t service_id() const { return service_; }
  void set_profile(uint8_t v) { profile_ = static_cast<EntityProfile>(v); }
  void apply_profile(Profile p) { set_internal(profile_ == EntityProfile::TAI_AC ? p != Profile::TAI_AC : profile_ == EntityProfile::TAI_DEHUM ? p != Profile::TAI_DEHUM : false); }
  void publish_value(uint16_t v) { publish_state(v != 0); }
 protected:
  void write_state(bool state) override;
  ApplianceAuto *parent_{}; uint8_t service_{}; EntityProfile profile_{EntityProfile::ANY};
};

class AutoServiceNumber : public number::Number, public Component {
 public:
  void set_parent(ApplianceAuto *p) { parent_ = p; }
  void set_service(uint8_t v) { service_ = v; }
  uint8_t service_id() const { return service_; }
  void set_profile(uint8_t v) { profile_ = static_cast<EntityProfile>(v); }
  void apply_profile(Profile p) { set_internal(profile_ == EntityProfile::TAI_AC ? p != Profile::TAI_AC : profile_ == EntityProfile::TAI_DEHUM ? p != Profile::TAI_DEHUM : false); }
  void publish_value(uint16_t v) { publish_state(static_cast<float>(v)); }
 protected:
  void control(float value) override;
  ApplianceAuto *parent_{}; uint8_t service_{}; EntityProfile profile_{EntityProfile::ANY};
};

class AutoServiceBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void set_parent(ApplianceAuto *p) { parent_ = p; }
  void set_service(uint8_t v) { service_ = v; }
  uint8_t service_id() const { return service_; }
  void set_profile(uint8_t v) { profile_ = static_cast<EntityProfile>(v); }
  void apply_profile(Profile p) { set_internal(profile_ == EntityProfile::TAI_AC ? p != Profile::TAI_AC : profile_ == EntityProfile::TAI_DEHUM ? p != Profile::TAI_DEHUM : false); }
  void publish_value(uint16_t v) { publish_state(v != 0); }
 private:
  ApplianceAuto *parent_{}; uint8_t service_{}; EntityProfile profile_{EntityProfile::ANY};
};

class AutoServiceSelect : public select::Select, public Component {
 public:
  void set_parent(ApplianceAuto *p) { parent_ = p; }
  void set_service(uint8_t v) { service_ = v; }
  uint8_t service_id() const { return service_; }
  void set_profile(uint8_t v) { profile_ = static_cast<EntityProfile>(v); }
  void apply_profile(Profile p) { set_internal(profile_ == EntityProfile::TAI_AC ? p != Profile::TAI_AC : profile_ == EntityProfile::TAI_DEHUM ? p != Profile::TAI_DEHUM : false); }
  void publish_value(uint16_t v) {
    if (v < this->traits.get_options().size()) this->publish_state(this->traits.get_options()[v]);
  }
 protected:
  void control(const std::string &value) override;
  ApplianceAuto *parent_{}; uint8_t service_{}; EntityProfile profile_{EntityProfile::ANY};
};

class AutoServiceSensor : public sensor::Sensor, public Component {
 public:
  void set_parent(ApplianceAuto *p) { parent_ = p; }
  void set_service(uint8_t v) { service_ = v; }
  uint8_t service_id() const { return service_; }
  void set_divisor(float v) { divisor_ = v > 0.0f ? v : 1.0f; }
  void set_profile(uint8_t v) { profile_ = static_cast<EntityProfile>(v); }
  void apply_profile(Profile p) {
    set_internal(profile_ == EntityProfile::TAI_AC ? p != Profile::TAI_AC :
                 profile_ == EntityProfile::TAI_DEHUM ? p != Profile::TAI_DEHUM : false);
  }
  void publish_value(uint16_t v) { publish_state(static_cast<float>(v) / divisor_); }
 protected:
  ApplianceAuto *parent_{}; uint8_t service_{}; float divisor_{1.0f};
  EntityProfile profile_{EntityProfile::ANY};
};

class AutoClimate : public climate::Climate {
 public:
  void set_parent(ApplianceAuto *p) { parent_=p; }
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
 protected: ApplianceAuto *parent_{};
};
class AutoFan : public fan::Fan {
 public:
  void set_parent(ApplianceAuto *p) { parent_=p; }
  void set_profile(uint8_t v) { profile_=static_cast<EntityProfile>(v); }
  void apply_profile(Profile p) {
    const bool hide = (profile_ == EntityProfile::TAI_AC) ? !is_ac(p) :
                      (profile_ == EntityProfile::TAI_DEHUM) ? p != Profile::TAI_DEHUM : false;
    set_internal(hide);
  }
  fan::FanTraits get_traits() override { return fan::FanTraits(false,true,false,4); }
  void control(const fan::FanCall &call) override;
 protected: ApplianceAuto *parent_{}; EntityProfile profile_{EntityProfile::ANY};
};
struct StoredProfile {
  uint32_t magic{0x0601a1ef};
  Profile type{Profile::UNKNOWN}; uint8_t inverted{0};
  char brand[40]{}, model[40]{};
};
class ApplianceAuto : public Component, public uart::UARTDevice,
                      public chip::app::Clusters::FanControl::Delegate {
 public:
  ApplianceAuto();
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_climate(AutoClimate *p) { climate_=p; }
  void set_fan(AutoFan *p) { fan_=p; }
  void set_ac_fan(AutoFan *p) { ac_fan_=p; }
  void set_matter(matter::MatterComponent *p) { matter_=p; }
  void set_status_sensor(text_sensor::TextSensor *p) { status_sensor_=p; }
  void set_brand_sensor(text_sensor::TextSensor *p) { brand_sensor_=p; }
  void set_model_sensor(text_sensor::TextSensor *p) { model_sensor_=p; }
  void set_protocol_sensor(text_sensor::TextSensor *p) { protocol_sensor_=p; }
  void register_service_switch(AutoServiceSwitch *p) { service_switches_.push_back(p); }
  void register_service_number(AutoServiceNumber *p) { service_numbers_.push_back(p); }
  void register_service_binary_sensor(AutoServiceBinarySensor *p) { service_binary_sensors_.push_back(p); }
  void register_service_select(AutoServiceSelect *p) { service_selects_.push_back(p); }
  void register_service_sensor(AutoServiceSensor *p) { service_sensors_.push_back(p); }
  Profile profile() const { return stored_.type; }
  bool configured() const { return profile()!=Profile::UNKNOWN; }
  // Require a fresh status frame before reporting the appliance as online.
  bool online() const {
    return configured() && online_ && !mismatch_ && phase_==Phase::READY &&
           uint32_t(millis() - last_status_ms_) < 15000;
  }
  bool endpoint_enabled(uint16_t id) const {
    if (id == 1 || id == 2) return is_ac(profile());
    return id == 4 && profile() == Profile::TAI_DEHUM;
  }
  const char *device_name() const { return profile()==Profile::TAI_DEHUM ? "除濕機" : "Alpha 空調"; }
  uint8_t speed_max() const { return 4; }
  std::string status_json(); // mutex-protected immutable snapshot for HTTP task
  void clear_profile();
  void request_climate(int mode, float target, int fan=-1);
  void request_fan(int speed);
  void request_service(uint8_t service, uint16_t value);
  // Control endpoint used by the local setup page. It is deliberately kept
  // behind the same online/configured gate as the native ESPHome entities.
  bool portal_control(const std::string &command, int service, int value);
  void request_vertical_swing(bool enabled) { request_service(0x0E, enabled ? 1 : 0); }
  // Matter FanControl is attached to the AC thermostat endpoint. Matter's
  // FanMode enum is not identical to TaiSEIA's four fan-speed values, so keep
  // the conversion at this boundary rather than sending enum values raw.
  void matter_fan_mode(uint64_t value);
  void matter_fan_percent(uint64_t value);
  void matter_fan_speed(uint64_t value);
  void matter_vertical_swing(uint64_t value);
  void matter_mode(uint64_t mode);
  void matter_target(bool heating, int64_t value);
  chip::Protocols::InteractionModel::Status HandleStep(chip::app::Clusters::FanControl::StepDirectionEnum,bool,bool) override;
  void OnFanDriveStateChanged(const chip::app::Clusters::FanControl::FanDriveState &state) override;
  void OnRockSettingChanged(chip::BitMask<chip::app::Clusters::FanControl::RockBitmap> value) override;
 protected:
  enum class Phase { REGISTER, ID1, ID2, BRAND, MODEL, STATUS1, STATUS2, READY };
  enum class Purpose { PROBE, POLL, TEMPERATURE, WRITE };
  bool set_wire_(bool s21, bool inverted);
  void begin_probe_(uint8_t candidate);
  void probe_next_();
  void send_(std::vector<uint8_t> frame, Purpose purpose);
  void receive_();
  void complete_(bool ok, const std::vector<uint8_t>& frame={});
  void accept_profile_();
  void publish_state_(const State &state);
  void publish_service_entities_(const State &state);
  void report_matter_(State state, uint32_t generation, float heat, float cool);
  void snapshot_(const char *status);
  bool control_allowed_() const;
  void enqueue_(std::vector<uint8_t> frame);
  void poll_();
  bool save_profile_();
  StoredProfile stored_{}, detected_{};
  State state_{};
  AutoClimate *climate_{}; AutoFan *fan_{}; AutoFan *ac_fan_{}; matter::MatterComponent *matter_{};
  text_sensor::TextSensor *status_sensor_{}, *brand_sensor_{}, *model_sensor_{}, *protocol_sensor_{};
  Phase phase_{Phase::REGISTER}; Purpose purpose_{Purpose::PROBE};
  uint8_t candidate_{0}, confirmations_{0};
  bool wire_s21_{false}, waiting_{false}, online_{false}, mismatch_{false}, rebooting_{false}, s21_ack_{false};
  bool migration_required_{false};
  bool reporting_{false}; // Matter thread only
  uint32_t sent_ms_{0}, next_ms_{0}, last_status_ms_{0}, last_poll_ms_{0}, temperature_ms_{0};
  std::vector<uint8_t> rx_, request_;
  std::deque<std::vector<uint8_t>> commands_;
  std::vector<AutoServiceSwitch *> service_switches_;
  std::vector<AutoServiceNumber *> service_numbers_;
  std::vector<AutoServiceBinarySensor *> service_binary_sensors_;
  std::vector<AutoServiceSelect *> service_selects_;
  std::vector<AutoServiceSensor *> service_sensors_;
  std::atomic<int> pending_fan_{-1};
  std::atomic<uint32_t> generation_{0};
  // Main loop writes only while no report is queued; Matter releases the slot last.
  State report_state_{};
  uint32_t report_generation_{0};
  float report_heat_{24}, report_cool_{24};
  std::atomic<bool> report_queued_{false};
  int pending_mode_{-1}; bool target_dirty_{false}; uint32_t climate_due_{0};
  float heat_target_{24}, cool_target_{24}, last_target_{NAN};
  Mutex snapshot_mutex_; std::string snapshot_json_{"{\"status\":\"starting\"}"};
};
extern ApplianceAuto *global_appliance;
} // namespace
