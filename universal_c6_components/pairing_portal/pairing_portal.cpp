#include "pairing_portal.h"
#include "esphome/components/appliance_auto/appliance_auto.h"
#include "portal_ui.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <esp_random.h>
#include <qrcodegen.h>
#include <cstdio>
#include <cstdlib>

namespace esphome::pairing_portal {
static const char *const TAG = "pairing_portal";

static std::string json_quote(const std::string &value) {
  std::string result = "\"";
  char escaped[7];
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < 0x20) {
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      result += escaped;
    } else {
      result += static_cast<char>(c);
    }
  }
  return result + '"';
}

void PairingPortal::setup() {
  char token[33];
  snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
           (unsigned long) esp_random(), (unsigned long) esp_random(),
           (unsigned long) esp_random(), (unsigned long) esp_random());
  csrf_ = token;
  hostname_ = App.get_name().c_str();
  base_->add_handler(this);
  base_->init();
  // Keep the temporary setup AP available until the appliance has completed
  // its UART identification. This covers first power-up before the installer
  // has connected the air-conditioner interface.
  if (auto *wifi = wifi::global_wifi_component;
      wifi != nullptr && !appliance_auto::global_appliance->configured()) {
    wifi->set_ap_timeout(0);
    if (wifi->is_disabled()) wifi->enable();
  }
  ESP_LOGI(TAG, "Setup: http://192.168.4.1/ (AP) or device Wi-Fi IP; username admin");
  ESP_LOGI(TAG, "Local URL: http://%s.local/", hostname_.c_str());
}

bool PairingPortal::pairing_visible_() const {
  return qr_ready_.load() && !matter_->commissioned() && matter_->pairing_open();
}

void PairingPortal::render_qr_() {
  uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
  if (!qrcodegen_encodeText(matter_->qr_code().c_str(), scratch, qr, qrcodegen_Ecc_MEDIUM,
                           1, 5, qrcodegen_Mask_AUTO, true)) {
    ESP_LOGE(TAG, "Cannot encode Matter QR payload");
    return;
  }
  const int size = qrcodegen_getSize(qr);
  char buffer[140];
  snprintf(buffer, sizeof(buffer), "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %d %d' shape-rendering='crispEdges'>", size + 8, size + 8);
  svg_ = buffer;
  svg_ += "<rect width='100%' height='100%' fill='white'/><path fill='black' d='";
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (qrcodegen_getModule(qr, x, y)) {
        snprintf(buffer, sizeof(buffer), "M%d %dh1v1h-1z", x + 4, y + 4);
        svg_ += buffer;
      }
    }
  }
  svg_ += "'/></svg>";
  qr_ready_.store(true); // Publish immutable SVG to the HTTP task.
}

void PairingPortal::loop() {
  auto *wifi = wifi::global_wifi_component;
  wifi_connected_.store(wifi->is_connected());
  // Recover the setup AP if another startup path temporarily disabled Wi-Fi.
  // Once Matter pauses Wi-Fi, leave that deliberate pause alone.
  if (!appliance_auto::global_appliance->configured() && !matter_->wifi_paused() &&
      wifi->is_disabled()) {
    wifi->set_ap_timeout(0);
    wifi->enable();
  }
  if (matter_->ready() && !qr_ready_.load() && !matter_->qr_code().empty()) render_qr_();
  if (setup_override_.load() && static_cast<int32_t>(millis() - setup_until_) >= 0) {
    setup_override_.store(false);
    wifi->set_ap_timeout(0);
  }
  // ESPHome closes its AP automatically after Wi-Fi connects. If the user
  // commissions Thread without configuring Wi-Fi, stop the AP after pairing.
  // AP fallback is disabled in YAML, so a later router outage cannot expose it.
  if (matter_->commissioned() && !setup_override_.load() &&
      wifi->is_ap_active()) {
    const bool reconnect = wifi->has_sta();
    wifi->disable();
    if (reconnect) {
      // With no YAML station, start() must load the stable provisioned-network
      // preference, just as it does on boot. Keep the saved credentials in NVS.
      wifi->clear_sta();
      wifi->enable();
    }
    ESP_LOGI(TAG, "Thread commissioned; setup access point stopped");
  }
}

void PairingPortal::open_setup() {
  setup_until_ = millis() + 900000;
  setup_override_.store(true);
  // Temporarily allow the normal ESPHome fallback AP if the saved router is
  // unreachable. This keeps the ESPHome state machine in charge of Wi-Fi.
  wifi::global_wifi_component->set_ap_timeout(1000);
  if (wifi::global_wifi_component->is_disabled()) {
    wifi::global_wifi_component->clear_sta();
    wifi::global_wifi_component->enable();
  }
  matter_->reopen_pairing();
}

bool PairingPortal::canHandle(AsyncWebServerRequest *request) const {
  char url[AsyncWebServerRequest::URL_BUF_SIZE];
  auto path = request->url_to(url);
  return path == "/" || path == "/matter" || path == "/matter/qr.svg" ||
         path == "/status" || path == "/config" || path == "/wifi" ||
         path == "/pairing" || path == "/control";
}

void PairingPortal::handleRequest(AsyncWebServerRequest *request) {
  if (!request->authenticate("admin", password_.c_str())) {
    request->requestAuthentication();
    return;
  }
  char url[AsyncWebServerRequest::URL_BUF_SIZE];
  auto path = request->url_to(url);
  if (path == "/wifi" || path == "/pairing" || path == "/control") {
    if (request->method() != HTTP_POST || request->arg("token") != csrf_) {
      request->send(403, "text/plain", "Invalid setup request");
      return;
    }
    if (path == "/pairing") {
      this->defer([this]() { matter_->reopen_pairing(); });
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    if (path == "/control") {
      const auto command = request->arg("command");
      const auto service_text = request->arg("service");
      const auto value_text = request->arg("value");
      char *end = nullptr;
      const long service = service_text.empty() ? -1 : std::strtol(service_text.c_str(), &end, 10);
      if (!service_text.empty() && (end == service_text.c_str() || *end != '\0')) {
        request->send(400, "text/plain", "Invalid service"); return;
      }
      end = nullptr;
      const long value = value_text.empty() ? -1 : std::strtol(value_text.c_str(), &end, 10);
      if (value_text.empty() || end == value_text.c_str() || *end != '\0') {
        request->send(400, "text/plain", "Invalid value"); return;
      }
      if (appliance_auto::global_appliance == nullptr ||
          !appliance_auto::global_appliance->portal_control(command, int(service), int(value))) {
        request->send(409, "text/plain", "Appliance is not ready or command is not supported");
        return;
      }
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    // Network changes on a paired device require physical presence (10-second
    // factory reset); ordinary LAN viewers cannot silently move the device elsewhere.
    if (matter_->commissioned() && !setup_override_.load()) {
      request->send(403, "text/plain", "Hold BOOT for 10 seconds to reset Wi-Fi and Matter setup");
      return;
    }
    const auto ssid = request->arg("ssid");
    const auto psk = request->arg("psk");
    if (ssid.empty() || ssid.size() > 32 || psk.size() > 63 ||
        (!psk.empty() && psk.size() < 8) || ssid.find('\0') != std::string::npos ||
        psk.find('\0') != std::string::npos) {
      request->send(400, "text/plain", "SSID: 1-32 bytes. Password: 8-63 bytes or blank for an open network.");
      return;
    }
    this->defer([this, ssid, psk]() {
      this->set_timeout("apply_wifi", 1500, [ssid, psk]() {
        auto *wifi = wifi::global_wifi_component;
        wifi->save_wifi_sta(ssid, psk);
        wifi->disable();
        // Clear only the runtime list. Otherwise start() selects a different
        // preference key and subsequent changes are lost at the next boot.
        wifi->clear_sta();
        wifi->enable();
      });
    });
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (request->method() != HTTP_GET) {
    request->send(405, "text/plain", "Method not allowed");
    return;
  }
  if (path == "/config") {
    std::string config = "{\"name\":" + json_quote(hostname_);
    config += ",\"url\":" + json_quote("http://" + hostname_ + ".local/");
    config += ",\"token\":" + json_quote(csrf_);
    config += ",\"manual_code\":" + json_quote(pairing_visible_() ? matter_->manual_code() : "");
    config += "}";
    auto *response = request->beginResponse(200, "application/json", config);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    request->send(response);
    return;
  }
  if (path == "/matter/qr.svg") {
    if (!pairing_visible_()) {
      request->send(409, "text/plain", "Pairing closed, already paired, or starting");
      return;
    }
    auto *response = request->beginResponse(200, "image/svg+xml", svg_);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  if (path == "/status") {
    std::string status = "{\"schema_version\":3,\"name\":" + json_quote(hostname_);
    status += ",\"paired\":";
    status += matter_->commissioned() ? "true" : "false";
    status += ",\"open\":";
    status += pairing_visible_() ? "true" : "false";
    status += ",\"ready\":";
    status += matter_->ready() ? "true" : "false";
    status += ",\"setup\":";
    status += setup_override_.load() ? "true" : "false";
    status += ",\"wifi\":";
    status += wifi_connected_.load() ? "true" : "false";
    status += ",\"wifi_paused\":";
    status += matter_->wifi_paused() ? "true" : "false";
    status += ",\"appliance\":" + appliance_auto::global_appliance->status_json() + "}";
    auto *response = request->beginResponse(200, "application/json", status);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  std::string body(reinterpret_cast<const char *>(PORTAL_HTML_GZIP), sizeof(PORTAL_HTML_GZIP));
  auto *response = request->beginResponse(200, "text/html; charset=utf-8", body);
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Frame-Options", "DENY");
  response->addHeader("X-Content-Type-Options", "nosniff");
  request->send(response);
}
} // namespace esphome::pairing_portal
