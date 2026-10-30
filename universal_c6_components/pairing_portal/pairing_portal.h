#pragma once
#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/matter/matter_component.h"
#include <atomic>
#include <cmath>
#include <string>

namespace esphome::pairing_portal {
class PairingPortal : public Component, public AsyncWebHandler {
 public:
  PairingPortal(web_server_base::WebServerBase *base, matter::MatterComponent *matter)
      : base_(base), matter_(matter) {}
  void set_password(const std::string &value) { password_ = value; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION - 4.0f; }
  void setup() override;
  void loop() override;
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void open_setup(); // physical button; never erases network credentials
 protected:
  bool pairing_visible_() const;
  void render_qr_();
  web_server_base::WebServerBase *base_;
  matter::MatterComponent *matter_;
  std::string password_, csrf_, svg_, hostname_;
  std::atomic<bool> qr_ready_{false}, wifi_connected_{false}, setup_override_{false};
  uint32_t setup_until_{0};
};
} // namespace esphome::pairing_portal
