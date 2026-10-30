#pragma once

#include "esphome/core/defines.h"
#ifdef USE_MATTER
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "matter_text_sensors.h"

#include "matter_attributes.h"
#include "matter_endpoints.h"
#include "matter_lights.h"
#include "matter_sensors.h"

#include <functional>
#include <atomic>
#include <string>
#include <vector>

#include <esp_matter.h>

namespace esphome::matter {

class MatterComponent : public Component {
public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    // Must run after ESPHome's network service components. On Wi-Fi/Ethernet
    // this lets ESPHome initialize the shared mDNS responder before Matter
    // publishes DNS-SD records; on Thread it keeps Matter after SRP setup.
    return setup_priority::AFTER_CONNECTION - 5.0f;
    // TODO: AFTER_BLUETOOTH in BLE commissioning mode.
  }

  void factory_reset();
  void factory_reset_all();
  void reopen_pairing();
  void refresh_pairing_state(); // called only from the Matter task
  bool commissioned() const { return this->commissioned_.load(); }
  bool pairing_open() const { return this->pairing_open_.load(); }
  bool ready() const { return this->ready_.load(); }
  bool wifi_paused() const { return this->wifi_paused_.load(); }
  const std::string &qr_code() const { return this->qr_code_; }
  const std::string &manual_code() const { return this->manual_pairing_code_; }

  // When enabled, stop ESPHome Wi-Fi after the BLE PASE session starts so the
  // ESP32-C6 radio can complete Thread commissioning with less 2.4 GHz
  // contention. Wi-Fi is restarted only after Matter commissioning completes
  // or the commissioning attempt times out.
  void set_pause_wifi_during_commissioning(bool value) {
    this->pause_wifi_during_commissioning_ = value;
  }
  // Called by CHIP device-event callbacks; the actual Wi-Fi operation is
  // deferred to ESPHome's main loop.
  void request_wifi_pause_();
  void request_wifi_resume_();

#ifdef USE_TEXT_SENSOR
  void set_manual_pairing_code_sensor(text_sensor::TextSensor *sensor) {
    this->manual_pairing_code_sensor_ = sensor;
  }
  void set_qr_code_sensor(text_sensor::TextSensor *sensor) {
    this->qr_code_sensor_ = sensor;
  }
  void set_fabric_compressed_id_sensor(size_t slot,
                                       text_sensor::TextSensor *sensor);
  void set_fabric_id_sensor(size_t slot, text_sensor::TextSensor *sensor);
  void set_fabric_label_sensor(size_t slot, text_sensor::TextSensor *sensor);
  void set_fabric_node_id_sensor(size_t slot, text_sensor::TextSensor *sensor);
  void set_fabric_vendor_id_sensor(size_t slot,
                                   text_sensor::TextSensor *sensor);
  void schedule_fabric_sensor_update();
#endif

  // Register Matter endpoints
  void register_endpoint(uint16_t endpoint_id, MatterEndpointBuildFn build_fn);

  // Register attribute update actions
  void register_attribute_trigger(MatterAttributeTriggerBase *trigger) {
    this->attribute_triggers_.push_back(trigger);
    this->register_attribute_callback(
        trigger->endpoint_id(), trigger->cluster_id(), trigger->attribute_id(),
        [trigger](const esp_matter_attr_val_t &value) {
          trigger->dispatch(value);
        });
  }
  void register_attribute_callback(uint16_t endpoint_id, uint32_t cluster_id,
                                   uint32_t attribute_id,
                                   MatterAttributeCallback callback);
  void dispatch_attribute_update(uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t attribute_id,
                                 const esp_matter_attr_val_t &value);
  void replay_attribute_callback(uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t attribute_id);
  std::vector<MatterAttributeTriggerBase *> attribute_triggers_;

  // Register ESPHome entities
#ifdef USE_LIGHT
  void register_light(light::LightState *light, uint16_t endpoint_id);
#endif // USE_LIGHT
#ifdef USE_SENSOR
  void register_sensor_attribute(sensor::Sensor *sensor, uint16_t endpoint_id,
                                 uint32_t cluster_id, uint32_t attribute_id,
                                 SensorValueConverter converter);

  template <
      typename ClusterT, typename ValueT,
      CHIP_ERROR (ClusterT::*Setter)(chip::app::DataModel::Nullable<ValueT>)>
  void register_code_driven_sensor_attribute(sensor::Sensor *sensor,
                                             uint16_t endpoint_id,
                                             uint32_t cluster_id,
                                             uint32_t attribute_id,
                                             SensorValueConverter converter) {
    this->mappings_.push_back(new MatterSensorAttributeMapping(
        sensor, endpoint_id, cluster_id, attribute_id, converter,
        update_code_driven_sensor_attribute<ClusterT, ValueT, Setter>));
  }
#endif // USE_SENSOR
#ifdef USE_BINARY_SENSOR
  void
  register_binary_sensor_attribute(binary_sensor::BinarySensor *sensor,
                                   uint16_t endpoint_id, uint32_t cluster_id,
                                   uint32_t attribute_id,
                                   BinarySensorValueConverter converter,
                                   SensorAttributeUpdater updater = nullptr);
#endif // USE_BINARY_SENSOR

  // Public wrapper around the protected Component scheduler; used by the
  // Matter-thread callbacks to hop onto the main loop (defer is thread-safe).
  void defer_to_main_loop(std::function<void()> &&f) {
    this->defer(std::move(f));
  }

private:
  std::atomic<bool> commissioned_{false};
  std::atomic<bool> pairing_open_{false};
  std::atomic<bool> ready_{false};
  std::atomic<bool> wifi_paused_{false};
  std::atomic<bool> wifi_pause_pending_{false};
  std::atomic<bool> wifi_resume_pending_{false};
  bool pause_wifi_during_commissioning_{false};
  uint32_t wifi_pause_started_ms_{0};
  void apply_wifi_pause_();
  void apply_wifi_resume_();
  void generate_commissioning_codes_();
  // Defined in matter_endpoints.cpp
  bool create_endpoints_(esp_matter::node_t *node);
  void initialize_endpoint_mappings_();

  uint16_t discriminator_{0};
  uint32_t passcode_{0};
  std::string manual_pairing_code_;
  std::string qr_code_;
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *manual_pairing_code_sensor_{nullptr};
  text_sensor::TextSensor *qr_code_sensor_{nullptr};
  std::vector<MatterFabricSensorRegistration> fabric_sensors_;
  MatterFabricSensorRegistration &get_fabric_sensors_(size_t slot);
  void publish_commissioning_code_sensors_();
  void publish_fabric_sensors_();
#endif

  std::vector<MatterEndpointRegistration> endpoint_registrations_;
  std::vector<MatterEndpointMappingBase *> mappings_;
  std::vector<MatterAttributeCallbackRegistration> attribute_callbacks_;
};

extern MatterComponent *
    global_matter_component; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

template <typename... Ts>
class MatterFactoryResetAction : public Action<Ts...>,
                                 public Parented<MatterComponent> {
public:
  void play(Ts... x) override { this->parent_->factory_reset(); }
};

} // namespace esphome::matter

#endif // USE_MATTER
