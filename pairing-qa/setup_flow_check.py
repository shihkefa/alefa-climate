"""Host-test actual portal methods with simulated Wi-Fi/Matter dependencies.

Checks setup state decisions and repeated credential persistence; this is not
a radio, FreeRTOS, BLE commissioning, or hardware test.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("portal", type=Path)
p.add_argument("wifi_source", type=Path)
args = p.parse_args()
source = args.portal.read_text()
wifi_source = args.wifi_source.read_text()
# Pin the dependency behavior represented by the fixture. ESPHome changes to
# preference selection must be reviewed instead of silently passing this test.
assert "this->has_sta() ? App.get_config_version_hash() : 88491487UL" in wifi_source
methods = source.split("void PairingPortal::loop()", 1)[1].split("bool PairingPortal::canHandle", 1)[0]
apply_body = source.split('this->set_timeout("apply_wifi", 1500, [ssid, psk]() {', 1)[1].split("      });", 1)[0]
prefix = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <map>
#define ESP_LOGI(...) ((void)0)
uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
namespace wifi {
struct WiFi {
  bool disabled = false, ap = true, connected = false;
  uint32_t timeout = 0;
  int preference = 0;
  std::string sta;
  std::map<int, std::string> saved;
  bool is_connected() { return connected; }
  bool is_ap_active() { return ap; }
  bool has_sta() { return !sta.empty(); }
  bool is_disabled() { return disabled; }
  void set_ap_timeout(uint32_t value) { timeout = value; }
  void clear_sta() { sta.clear(); }
  void disable() { disabled = true; ap = false; connected = false; }
  void enable() {
    if (!disabled) return;
    disabled = false;
    // ESPHome start() chooses its preference key BEFORE loading credentials.
    preference = has_sta() ? 1 : 0;
    if (saved.count(preference)) sta = saved.at(preference);
    ap = !has_sta();
  }
  void save_wifi_sta(const std::string &ssid, const std::string &) {
    saved[preference] = ssid;
    sta = ssid;
  }
};
WiFi *global_wifi_component;
}
struct Matter {
  bool paired = false;
  int reopen_requests = 0;
  bool commissioned() { return paired; }
  bool ready() { return false; }
  const std::string &qr_code() { static std::string code; return code; }
  void reopen_pairing() { ++reopen_requests; }
};
class PairingPortal {
 public:
  Matter *matter_;
  std::atomic<bool> wifi_connected_{false}, qr_ready_{false}, setup_override_{false};
  uint32_t setup_until_ = 0;
  void render_qr_() {}
  void loop();
  void open_setup();
};
void PairingPortal::loop()
'''
suffix = r'''
int main() {
  wifi::WiFi radio;
  wifi::global_wifi_component = &radio;
  Matter matter;
  PairingPortal portal;
  portal.matter_ = &matter;
  portal.loop();
  assert(radio.ap); // New, unpaired device remains accessible.
  matter.paired = true;
  portal.loop();
  assert(!radio.ap && radio.disabled); // Thread-only commissioning closes AP.
  portal.open_setup();
  portal.loop();
  assert(radio.ap && !radio.disabled && matter.paired);
  assert(radio.timeout == 1000 && portal.setup_override_);
  now_ms += 900000;
  portal.loop();
  assert(!radio.ap && radio.disabled && matter.paired);
  assert(radio.timeout == 0 && !portal.setup_override_);

  portal.open_setup();
  apply_wifi("Home-A", "test-password");
  assert(radio.sta == "Home-A" && !radio.ap && radio.preference == 0);
  apply_wifi("Home-B", "test-password");
  assert(radio.sta == "Home-B" && radio.preference == 0);
  radio.disable();
  radio.clear_sta();
  radio.enable(); // Simulate boot with no compiled YAML station.
  assert(radio.sta == "Home-B" && matter.paired);

  portal.setup_override_ = false;
  radio.ap = true; // An active fallback AP must stop without losing saved STA.
  portal.loop();
  assert(!radio.ap && !radio.disabled && radio.sta == "Home-B");
  assert(radio.preference == 0 && matter.paired);
  now_ms = UINT32_MAX - 100;
  portal.open_setup();
  portal.loop();
  assert(portal.setup_override_);
  now_ms += 900000;
  portal.loop();
  assert(!portal.setup_override_ && matter.paired); // millis rollover.
  std::cout << "PASS: actual setup methods preserve pairing, close/reopen AP, "
               "expire after 15 minutes, and persist repeated Wi-Fi changes\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp = tmp / "setup.cpp"
    cpp.write_text(prefix + methods +
                   "void apply_wifi(const std::string &ssid, const std::string &psk) {\n" +
                   apply_body + "}\n" + suffix)
    exe = tmp / "setup"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
