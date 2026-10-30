from pathlib import Path
root=Path(__file__).resolve().parent.parent
def edit(path, old, new):
    p=root/path; s=p.read_text(encoding='utf8')
    assert old in s, (path,old[:60])
    p.write_text(s.replace(old,new),encoding='utf8')

# One-time assembly of the independent 0.6.0 source, preserving released versions.
p=root/'Dehumidifier-C6-Pairing.yaml'
s=p.read_text(encoding='utf8')
s=s.replace('upper_devicename: "除濕機"','upper_devicename: "Alpha"').replace('version: "0.5.0"','version: "0.6.0"')
s=s.replace('alefa.dehumidifier-c6','alefa.universal-c6').replace('dehumidifier_c6_components','universal_c6_components')
s=s.replace('components: [taixia, matter, pairing_portal, dehumidifier_bridge]','components: [matter, pairing_portal, appliance_auto]')
s=s.replace('comment: "TaiSEIA dehumidifier: Matter Fan + SHT40 + ESPHome Wi-Fi"','comment: "Auto TaiSEIA AC/dehumidifier + Daikin S21 + Matter + SHT40"')
s=s.replace('# UART TX19/RX20, 9600 baud, through the existing 3.3V/5V level shifter.', '# UART TX19/RX20. Runtime detection: TaiSEIA 9600 8N1 / S21 2400 8E2.\n# Requires the correct level shifter and appliance connector; software cannot identify wiring.')
s=s.replace('  # TaiXia base: tsunglung/taixia@581bd578407a166f1d15a9058ae114d5a5a7458f.\n  # Local fan/receiver fixes are documented in the release source notes.', '  # Protocol references and licenses: appliance_auto/SOURCES.txt.')
s=s.replace('  baud_rate: 9600','  baud_rate: 9600\n  rx_buffer_size: 512')
a=s.index('taixia:\n'); b=s.index('binary_sensor:',a)
s=s[:a]+'''appliance_auto:
  id: appliance
  uart_id: uart_taixia
  matter_id: matter_bridge
  status:
    name: Appliance Connection
  brand:
    name: Appliance Brand
  model:
    name: Appliance Model
  protocol:
    name: Appliance Protocol

'''+s[b:]
a=s.index('# RD-240HH'); b=s.index('matter:\n',a)
s=s[:a]+'''# Both entities are generated; only the detected type is exposed at boot.
climate:
  - platform: appliance_auto
    id: appliance_climate
    name: "空調"
    appliance_id: appliance
fan:
  - platform: appliance_auto
    id: appliance_fan
    name: "除濕機"
    appliance_id: appliance

'''+s[b:]
s=s.replace('product_name: "除濕機"','product_name: "Alpha Universal"')
a=s.index('    1:\n');b=s.index('    2:\n',a)
s=s[:a]+'''    1:
      id: ac_endpoint
      thermostat:
        with_features: [heating, cooling, auto_mode]
      on_attribute:
        thermostat.system_mode:
          - lambda: id(appliance).matter_mode(value);
        thermostat.occupied_heating_setpoint:
          - lambda: id(appliance).matter_target(true, value);
        thermostat.occupied_cooling_setpoint:
          - lambda: id(appliance).matter_target(false, value);
    4:
      id: dehumidifier_endpoint
      fan:
        with_features: [multi_speed]
'''+s[b:]
s=s.replace('  - platform: taixia\n    type: dehumidifier\n    get_info:\n      name: Get Info\n','')
a=s.index('  - platform: taixia\n',s.index('text_sensor:')); b=s.index('\ntime:',a)
s=s[:a]+s[b:]
(root/'Universal-C6-Pairing.yaml').write_text(s,encoding='utf8')

base='universal_c6_components/'
edit(base+'matter/endpoints.py','esphome/components/dehumidifier_bridge/dehumidifier_bridge.h','esphome/components/appliance_auto/appliance_auto.h')
edit(base+'matter/endpoints.py','esphome::dehumidifier_bridge::global_bridge','esphome::appliance_auto::global_appliance')
edit(base+'matter/matter_component.cpp','#include "matter_component.h"','#include "matter_component.h"\n#include "esphome/components/appliance_auto/appliance_auto.h"')
edit(base+'matter/matter_component.cpp','  global_matter_component = this;','  global_matter_component = this;\n  if (!appliance_auto::global_appliance->configured()) {\n    ESP_LOGI(TAG, "Waiting for appliance identification before Matter commissioning");\n    return;\n  }')
edit(base+'matter/matter_component.cpp','nvs_set_str(h, "device-name", "除濕機");','nvs_set_str(h, "device-name", appliance_auto::global_appliance->device_name());')
edit(base+'matter/matter_endpoints.cpp','#include "matter_component.h"','#include "matter_component.h"\n#include "esphome/components/appliance_auto/appliance_auto.h"')
edit(base+'matter/matter_endpoints.cpp','    uint16_t endpoint_id = registration.endpoint_id;','    uint16_t endpoint_id = registration.endpoint_id;\n    if (!appliance_auto::global_appliance->endpoint_enabled(endpoint_id)) continue;')
edit(base+'matter/matter_component.cpp','void MatterComponent::factory_reset() {','void MatterComponent::factory_reset() {\n  appliance_auto::global_appliance->clear_profile();')
edit(base+'matter/matter_component.cpp','  // SDK reset clears fabrics AND the runtime-provisioned Thread dataset.','  if (this->ready()) {\n  // SDK reset clears fabrics AND the runtime-provisioned Thread dataset.')
edit(base+'matter/matter_component.cpp','  return;\n#endif\n  ESP_LOGW(TAG, "Matter factory reset.', '  return;\n  }\n#endif\n  ESP_LOGW(TAG, "Matter factory reset.')
# When detection has not started Matter, clear its NVS directly (including Thread).
edit(base+'matter/matter_component.cpp','{"chip-config", "chip-counters", "CHIP_KVS"}', '{"chip-config", "chip-counters", "CHIP_KVS", "openthread"}')
edit(base+'pairing_portal/pairing_portal.cpp','#include "pairing_portal.h"','#include "pairing_portal.h"\n#include "esphome/components/appliance_auto/appliance_auto.h"')
edit(base+'pairing_portal/pairing_portal.cpp','json_number(humidity_.load()) + "}}";', 'json_number(humidity_.load()) + "}";\n    status += ",\\\"appliance\\\":" + appliance_auto::global_appliance->status_json() + "}";')

# The first profile is chosen while Matter is stopped; make this visible in the portal.
portal=root/base/'pairing_portal/portal.html'
s=portal.read_text(encoding='utf8').replace('除濕機 · 裝置設定','Alpha · 裝置設定').replace('ALPHA DEHUMIDIFIER','ALPHA UNIVERSAL')
s=s.replace('這台除濕機的 Matter','這台裝置的 Matter').replace('除濕機 · ESP32-C6','Alpha · ESP32-C6')
s=s.replace('<div class="grid">','''<section class="card environment" aria-label="設備辨識"><div class="environment-head"><div><h2 id="appliance-title">正在辨識設備</h2><p class="caption" id="appliance-info">請將通訊線接到設備，並接通設備電源。</p></div><span class="pill" id="appliance-pill"><span class="dot"></span><span id="appliance-status">辨識中</span></span></div><p class="environment-note" id="appliance-note">辨識完成後會自動重新啟動一次，再顯示 Matter 配對碼。</p></section>
<div class="grid">''',1)
s=s.replace("function render(s){",'''function renderAppliance(a){if(!a)return;const labels={starting:'準備中',detecting:'辨識中',connected:'已連線',no_response:'未收到回應',profile_mismatch:'設備已更換',identified_restarting:'辨識完成，重新啟動中',uart_error:'通訊埠異常',storage_error:'儲存失敗'};$('appliance-title').textContent=a.type==='dehumidifier'?'除濕機':a.type==='air_conditioner'?'空調':'正在辨識設備';$('appliance-status').textContent=labels[a.status]||a.status;$('appliance-pill').classList.toggle('good',a.online);$('appliance-info').textContent=[a.protocol,a.brand||'品牌未回報',a.model||'型號未回報'].join(' · ');$('appliance-note').textContent=a.status==='profile_mismatch'?'偵測到不同設備。請先在原平台移除裝置，再按住 BOOT 10 秒重設及重新配對。':a.online?'已完成通訊握手。可繼續 Matter 配對或連接 Wi-Fi。':'請確認通訊線與設備電源。未通過辨識時不會送出控制指令。';}
function render(s){renderAppliance(s.appliance);''')
s=s.replace("'正在啟動配對服務'","'等待設備辨識與配對服務'").replace("'請稍候，裝置正在準備藍牙與 Thread。'","'接通設備電源後會先辨識通訊協定；辨識成功後自動啟動配對。'")
portal.write_text(s,encoding='utf8')

# Avoid a member/function name collision.
edit(base+'appliance_auto/appliance_auto.h','void detected_();','void accept_profile_();')
edit(base+'appliance_auto/appliance_auto.cpp','detected_();','accept_profile_();')
edit(base+'appliance_auto/appliance_auto.cpp','void ApplianceAuto::detected_()', 'void ApplianceAuto::accept_profile_()')
print('Universal source assembled')
