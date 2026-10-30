#include "appliance_auto.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart_component_esp_idf.h"
#include <app/clusters/fan-control-server/FanControlCluster.h>
#include <platform/CHIPDeviceLayer.h>
#include <nvs.h>
#include <algorithm>
#include <cstring>

namespace esphome::appliance_auto {
namespace FC = chip::app::Clusters::FanControl;
using chip::Protocols::InteractionModel::Status;
using namespace esphome::climate;
static const char *const TAG="appliance.auto";
ApplianceAuto *global_appliance=nullptr;
static ClimateMode native_mode(Mode m) {
  switch(m) {case Mode::AUTO:return CLIMATE_MODE_AUTO;case Mode::COOL:return CLIMATE_MODE_COOL;
  case Mode::HEAT:return CLIMATE_MODE_HEAT;case Mode::FAN:return CLIMATE_MODE_FAN_ONLY;
  case Mode::DRY:return CLIMATE_MODE_DRY;default:return CLIMATE_MODE_OFF;}
}
static int from_native(ClimateMode m) {
  switch(m) {case CLIMATE_MODE_AUTO:case CLIMATE_MODE_HEAT_COOL:return 1;
  case CLIMATE_MODE_COOL:return 3;case CLIMATE_MODE_HEAT:return 4;case CLIMATE_MODE_FAN_ONLY:return 7;
  case CLIMATE_MODE_DRY:return 8;default:return 0;}
}
void AutoServiceSwitch::write_state(bool state) {
  if (this->parent_ != nullptr)
    this->parent_->request_service(this->service_, state ? 1 : 0);
}
void AutoServiceNumber::control(float value) {
  if (this->parent_ != nullptr && std::isfinite(value) && value >= 0)
    this->parent_->request_service(this->service_, static_cast<uint16_t>(std::lround(value)));
}
void AutoServiceSelect::control(const std::string &value) {
  if (this->parent_ == nullptr) return;
  const auto &options = this->traits.get_options();
  for (size_t i = 0; i < options.size(); i++) {
    if (options[i] == value) {
      this->parent_->request_service(this->service_, static_cast<uint16_t>(i));
      return;
    }
  }
}
ClimateTraits AutoClimate::traits() {
  ClimateTraits t;
  t.set_supported_modes({CLIMATE_MODE_OFF,CLIMATE_MODE_AUTO,CLIMATE_MODE_COOL,CLIMATE_MODE_HEAT,CLIMATE_MODE_FAN_ONLY,CLIMATE_MODE_DRY});
  t.set_supported_fan_modes({CLIMATE_FAN_AUTO,CLIMATE_FAN_LOW,CLIMATE_FAN_MEDIUM,CLIMATE_FAN_HIGH});
  t.add_feature_flags(CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  t.set_visual_min_temperature(16); t.set_visual_max_temperature(30); t.set_visual_temperature_step(1);
  return t;
}
void AutoClimate::control(const ClimateCall &call) {
  int fan=-1;
  if(call.get_fan_mode()) {
    switch(*call.get_fan_mode()) {case CLIMATE_FAN_LOW:fan=1;break;case CLIMATE_FAN_MEDIUM:fan=2;break;
    case CLIMATE_FAN_HIGH:fan=3;break;case CLIMATE_FAN_AUTO:fan=0;break;default:return;}
  }
  parent_->request_climate(call.get_mode() ? from_native(*call.get_mode()) : -1,
                           call.get_target_temperature().value_or(NAN),fan);
}
void AutoFan::control(const fan::FanCall &call) {
  if(call.get_state() && !*call.get_state()) { parent_->request_fan(0); return; }
  if(call.get_speed()) parent_->request_fan(*call.get_speed());
  else if(call.get_state() && *call.get_state()) parent_->request_fan(1);
}
ApplianceAuto::ApplianceAuto() : FC::Delegate(4) { global_appliance=this; }
void ApplianceAuto::setup() {
  nvs_handle_t h;
  if(nvs_open("alefa-auto",NVS_READONLY,&h)==ESP_OK) {
    size_t len=sizeof(stored_); StoredProfile p{};
    if(nvs_get_blob(h,"profile",&p,&len)==ESP_OK && len==sizeof(p) && p.magic==stored_.magic &&
       p.type>=Profile::TAI_AC && p.type<=Profile::S21_AC && p.inverted<=1 && p.brand[39]==0 && p.model[39]==0)
      stored_=p;
    nvs_close(h);
  }
  // Hide the inactive native entity before API/mdns setup. No runtime type swaps.
  climate_->set_internal(!is_ac(profile()));
  fan_->apply_profile(profile());
  if (ac_fan_ != nullptr) ac_fan_->apply_profile(profile());
  for (auto *entity : service_switches_) entity->apply_profile(profile());
  for (auto *entity : service_numbers_) entity->apply_profile(profile());
  for (auto *entity : service_binary_sensors_) entity->apply_profile(profile());
  for (auto *entity : service_selects_) entity->apply_profile(profile());
  for (auto *entity : service_sensors_) entity->apply_profile(profile());
  climate_->target_temperature=NAN; climate_->current_temperature=NAN;
  if(!configured()) {
    // A pre-0.6.0 firmware may have a different descriptor on the same fabric.
    // Keep existing pairing untouched until the user performs factory reset.
    uint32_t pin=0, reset_ok=0;
    bool legacy_identity=false;
    if(nvs_open("chip-factory",NVS_READONLY,&h)==ESP_OK) {
      legacy_identity=nvs_get_u32(h,"pin-code",&pin)==ESP_OK; nvs_close(h);
    }
    if(nvs_open("alefa-auto",NVS_READONLY,&h)==ESP_OK) {
      nvs_get_u32(h,"reset-ok",&reset_ok); nvs_close(h);
    }
    if(legacy_identity && reset_ok!=0x0601a1ef) {
      migration_required_=true; snapshot_("reset_required"); return;
    }
  }
  if(configured()) {
    if(brand_sensor_) brand_sensor_->publish_state(stored_.brand);
    if(model_sensor_) model_sensor_->publish_state(stored_.model);
  }
  rx_.reserve(256);
  begin_probe_(configured() ? (profile()==Profile::S21_AC ? 2 : 0)+stored_.inverted : 0);
}
bool ApplianceAuto::save_profile_() {
  nvs_handle_t h; if(nvs_open("alefa-auto",NVS_READWRITE,&h)!=ESP_OK) return false;
  const bool ok=nvs_set_blob(h,"profile",&detected_,sizeof(detected_))==ESP_OK &&
      nvs_set_u32(h,"reset-ok",0)==ESP_OK && nvs_commit(h)==ESP_OK;
  nvs_close(h); return ok;
}
void ApplianceAuto::clear_profile() {
  online_=false; commands_.clear(); generation_.fetch_add(1);
  nvs_handle_t h;
  if(nvs_open("alefa-auto",NVS_READWRITE,&h)==ESP_OK) {
    nvs_erase_all(h); nvs_set_u32(h,"reset-ok",0x0601a1ef); nvs_commit(h); nvs_close(h);
  }
}
bool ApplianceAuto::set_wire_(bool s21,bool inverted) {
  auto *bus=static_cast<uart::IDFUARTComponent *>(parent_);
  bus->set_flush_timeout(100);
  bus->flush(); bus->flush_input();
  bus->set_baud_rate(s21 ? 2400 : 9600); bus->set_data_bits(8);
  bus->set_parity(s21 ? uart::UART_CONFIG_PARITY_EVEN : uart::UART_CONFIG_PARITY_NONE);
  bus->set_stop_bits(s21 ? 2 : 1);
  if(bus->apply_settings_live()!=ESP_OK) return false;
  if(uart_set_line_inverse(static_cast<uart_port_t>(bus->get_hw_serial_number()),
      inverted ? UART_SIGNAL_TXD_INV|UART_SIGNAL_RXD_INV : 0)!=ESP_OK) return false;
  bus->flush_input(); rx_.clear(); wire_s21_=s21; return true;
}
void ApplianceAuto::begin_probe_(uint8_t candidate) {
  generation_.fetch_add(1);
  online_=false; commands_.clear(); waiting_=false; candidate_=candidate%4;
  if(is_ac(profile()) && std::isfinite(state_.temperature)) {
    state_.temperature=NAN; climate_->current_temperature=NAN; climate_->publish_state();
    if(matter_->ready()) {
      const uint32_t generation=generation_.load();
      chip::DeviceLayer::SystemLayer().ScheduleLambda([this,generation](){
        if(generation!=generation_.load()) return;
        auto unknown=esp_matter_nullable_int16(nullable<int16_t>());
        esp_matter::attribute::update(1,0x0201,0x0000,&unknown);
      });
    }
  }
  detected_=StoredProfile{}; detected_.inverted=candidate_&1;
  phase_=candidate_<2 ? Phase::REGISTER : Phase::STATUS1;
  if(candidate_>=2) { detected_.type=Profile::S21_AC; std::strcpy(detected_.brand,"Daikin (S21)"); }
  if(!set_wire_(candidate_>=2,candidate_&1)) { snapshot_("uart_error"); mark_failed(); return; }
  next_ms_=millis()+300; snapshot_(mismatch_ ? "profile_mismatch" : "detecting");
}
void ApplianceAuto::probe_next_() {
  switch(phase_) {
  case Phase::REGISTER: send_(tai_request(0,0),Purpose::PROBE); break;
  case Phase::ID1: case Phase::ID2: send_(tai_request(0,4),Purpose::PROBE); break;
  case Phase::BRAND: send_(tai_request(0,5),Purpose::PROBE); break;
  case Phase::MODEL: send_(wire_s21_ ? s21_request('F','C') : tai_request(0,6),Purpose::PROBE); break;
  case Phase::STATUS1: case Phase::STATUS2:
    send_(wire_s21_ ? s21_request('F','1') : tai_request(0,8),Purpose::PROBE); break;
  default: break;
  }
}
void ApplianceAuto::send_(std::vector<uint8_t> frame,Purpose purpose) {
  static_cast<uart::IDFUARTComponent *>(parent_)->flush_input();
  request_=std::move(frame); rx_.clear(); s21_ack_=false; purpose_=purpose;
  write_array(request_); waiting_=true; sent_ms_=millis();
}
void ApplianceAuto::receive_() {
  // Bounded work per loop and bounded packet storage, including on a noisy wire.
  uint8_t b; size_t budget=256;
  while(available() && budget-- && read_byte(&b)) {
    if(!waiting_) continue;
    if(wire_s21_) {
      if(rx_.empty()) {
        if(b==0x15) { complete_(false); return; }
        if(b==6) {
          s21_ack_=true;
          if(purpose_==Purpose::WRITE) { complete_(true); return; }
          continue;
        }
        if(b!=2 || !s21_ack_) continue;
      }
      rx_.push_back(b);
      if(rx_.size()>64) { complete_(false); return; }
      if(b==3 && rx_.size()>1) {
        const char response_command=request_[1]+1;
        const bool ok=s21_valid(rx_,response_command,request_[2],4);
        if(ok) {
          auto frame=rx_;
          // Match the upstream documented 45 ms response ACK delay.
          set_timeout("s21_ack",45,[this](){ write_byte(6); });
          complete_(true,frame);
        } else complete_(false);
        return;
      }
    } else {
      if(rx_.empty() && (b<6 || b>200)) continue;
      rx_.push_back(b);
      if(rx_.size()==rx_[0]) {
        // Ignore exact read-query echoes; never accept echoed 0xffff as status.
        const bool match=tai_valid(rx_) && rx_[1]==request_[1] && rx_[2]==request_[2];
        const bool echo=purpose_!=Purpose::WRITE && rx_==request_;
        if(match && !echo) { auto frame=rx_; complete_(true,frame); return; }
        rx_.clear();
      }
    }
  }
  if(waiting_ && uint32_t(millis()-sent_ms_)>700) complete_(false);
}
void ApplianceAuto::complete_(bool ok,const std::vector<uint8_t>&frame) {
  waiting_=false; rx_.clear(); next_ms_=millis()+100;
  if(purpose_==Purpose::WRITE) {
    if(!ok) {
      ESP_LOGW(TAG,"Control acknowledgement missing; discarding remaining commands");
      commands_.clear(); online_=false; snapshot_("no_response");
    }
    last_poll_ms_=0; return;
  }
  if(purpose_==Purpose::TEMPERATURE) {
    if(ok && s21_valid(frame,'S','H',4) && frame[3]>='0' && frame[3]<='9' &&
       frame[4]>='0' && frame[4]<='9' && frame[5]>='0' && frame[5]<='9' && (frame[6]=='+' || frame[6]=='-')) {
      float t=((frame[3]-'0')+(frame[4]-'0')*10+(frame[5]-'0')*100)/10.0f;
      if(frame[6]=='-') t=-t;
      if(t>=-40 && t<=85) { state_.temperature=t; temperature_ms_=millis(); }
    }
    return;
  }
  State s;
  if(purpose_==Purpose::POLL) {
    if(ok && (wire_s21_ ? s21_state(frame,s) : tai_state(frame,profile(),s))) {
      if(wire_s21_ && uint32_t(millis()-temperature_ms_)<20000) s.temperature=state_.temperature;
      online_=true; last_status_ms_=millis(); publish_state_(s); snapshot_("connected");
      if(wire_s21_) set_timeout("temperature",150,[this](){
        if(!waiting_ && commands_.empty() && phase_==Phase::READY) send_(s21_request('R','H'),Purpose::TEMPERATURE);
      });
    } else {
      // Re-handshake before accepting any more controls, including after hot swapping.
      begin_probe_((wire_s21_?2:0)+stored_.inverted);
    }
    return;
  }
  switch(phase_) {
  case Phase::REGISTER: phase_=Phase::ID1; break; // registration metadata is optional
  case Phase::ID1:
    detected_.type=ok ? tai_identity(frame) : Profile::UNKNOWN;
    if(detected_.type==Profile::UNKNOWN) { begin_probe_(candidate_+1); return; }
    phase_=Phase::ID2; break;
  case Phase::ID2:
    if(!ok || tai_identity(frame)!=detected_.type) { begin_probe_(candidate_+1); return; }
    phase_=Phase::BRAND; break;
  case Phase::BRAND: {
    auto brand=ok ? tai_text(frame,5) : std::string();
    std::strncpy(detected_.brand,brand.c_str(),39); phase_=Phase::MODEL; break;
  }
  case Phase::MODEL:
    if(wire_s21_) {
      if(ok) std::snprintf(detected_.model,sizeof(detected_.model),"S21 code %02X%02X%02X%02X",frame[3],frame[4],frame[5],frame[6]);
      accept_profile_(); return;
    } else {
      const auto model=ok ? tai_text(frame,6) : std::string();
      std::strncpy(detected_.model,model.c_str(),39); phase_=Phase::STATUS1;
    }
    break;
  case Phase::STATUS1: case Phase::STATUS2:
    if(!ok || !(wire_s21_ ? s21_state(frame,s) : tai_state(frame,detected_.type,s))) {
      begin_probe_(candidate_+1); return;
    }
    state_=s;
    if(phase_==Phase::STATUS1) phase_=Phase::STATUS2;
    else if(wire_s21_) phase_=Phase::MODEL;
    else { accept_profile_(); return; }
    break;
  default: break;
  }
}
void ApplianceAuto::accept_profile_() {
  if(!configured()) {
    if(!save_profile_()) { snapshot_("storage_error"); next_ms_=millis()+5000; return; }
    snapshot_("identified_restarting"); rebooting_=true;
    ESP_LOGI(TAG,"Verified profile %u; rebooting once to build HA/Matter entities",unsigned(detected_.type));
    set_timeout("profile_restart",1200,[](){ App.safe_reboot(); }); return;
  }
  const bool brand_changed=stored_.brand[0] && detected_.brand[0] && std::strcmp(stored_.brand,detected_.brand)!=0;
  const bool model_changed=stored_.model[0] && detected_.model[0] && std::strcmp(stored_.model,detected_.model)!=0;
  if(profile()!=detected_.type || stored_.inverted!=detected_.inverted || brand_changed || model_changed) {
    mismatch_=true; online_=false; snapshot_("profile_mismatch");
    // Preserve the commissioned descriptor. Only the physical factory reset clears it.
    next_ms_=millis()+5000; phase_=Phase::ID1;
    begin_probe_(candidate_); next_ms_=millis()+5000; return;
  }
  mismatch_=false; phase_=Phase::READY; online_=true;
  last_status_ms_=millis(); last_poll_ms_=millis();
  publish_state_(state_); snapshot_("connected");
  if(brand_sensor_) brand_sensor_->publish_state(detected_.brand[0] ? detected_.brand : "Unknown (TaiSEIA)");
  if(model_sensor_) model_sensor_->publish_state(detected_.model[0] ? detected_.model : "Not reported");
}
void ApplianceAuto::poll_() {
  last_poll_ms_=millis();
  send_(wire_s21_ ? s21_request('F','1') : tai_request(0,8),Purpose::POLL);
}
bool ApplianceAuto::control_allowed_() const {
  return configured() && online_ && !mismatch_ && phase_==Phase::READY && uint32_t(millis()-last_status_ms_)<15000;
}
void ApplianceAuto::enqueue_(std::vector<uint8_t> frame) {
  // Coalesce the same setting without dropping other fields in a pending call.
  for(auto &queued:commands_) {
    if(queued[1]==frame[1] && queued[2]==frame[2]) { queued=std::move(frame); return; }
  }
  if(commands_.size()<8) commands_.push_back(std::move(frame));
}
void ApplianceAuto::request_fan(int speed) {
  if(!is_ac(profile()) && profile()!=Profile::TAI_DEHUM) return;
  if(!control_allowed_() || speed<0 || speed>4) return;
  generation_.fetch_add(1); commands_.clear();
  if (profile()==Profile::TAI_DEHUM) {
    if(speed==0) enqueue_(tai_request(4,0x80,0));
    else { enqueue_(tai_request(4,0x8e,speed)); enqueue_(tai_request(4,0x80,1)); }
  } else if (profile()==Profile::TAI_AC) {
    // Matter AC fan endpoint uses the same TaiSEIA fan-speed service as the
    // climate fan mode, while the thermostat entity continues to expose the
    // coarse Auto/Low/Medium/High modes.
    const uint8_t map[]{0,2,3,4};
    enqueue_(tai_request(1,0x82,map[std::min(speed,3)]));
    enqueue_(tai_request(1,0x80, speed == 0 ? 0 : 1));
  } else {
    auto p=state_.s21_raw;
    const std::vector<uint8_t> *pending=nullptr;
    if(waiting_ && purpose_==Purpose::WRITE) pending=&request_;
    if(!commands_.empty()) pending=&commands_.back();
    if(pending && pending->size()==9 && (*pending)[1]=='D' && (*pending)[2]=='1')
      std::copy_n(pending->begin()+3,4,p.begin());
    p[3] = speed==0 ? 'A' : speed==1 ? '3' : speed==2 ? '5' : '7';
    p[0] = speed==0 ? '0' : '1';
    enqueue_(s21_request('D','1',std::vector<uint8_t>(p.begin(),p.end())));
  }
}
void ApplianceAuto::matter_fan_mode(uint64_t value) {
  // Matter FanModeEnum: Off=0, Low=1, Medium=2, High=3, On=4,
  // Auto=5, Smart=6.  BC7215 normalizes On to High and Auto/Smart to
  // the appliance's automatic fan setting.
  if (!is_ac(profile()) || !control_allowed_() || value > 6) return;
  if (value == 0) {
    request_fan(0);
  } else if (value <= 3) {
    request_fan(static_cast<int>(value));
  } else if (value == 4) {
    request_fan(3);  // Matter On means the highest available fixed speed.
  } else {
    generation_.fetch_add(1);
    commands_.clear();
    enqueue_(tai_request(1, 0x82, 0));  // TaiSEIA automatic fan value.
    enqueue_(tai_request(1, 0x80, 1));
  }
}
void ApplianceAuto::matter_fan_percent(uint64_t value) {
  if (!is_ac(profile()) || !control_allowed_() || value > 100) return;
  if (value == 0) request_fan(0);
  else if (value <= 33) request_fan(1);
  else if (value <= 66) request_fan(2);
  else request_fan(3);
}
void ApplianceAuto::matter_fan_speed(uint64_t value) {
  // MultiSpeed SpeedSetting is 0..3 for this endpoint. Zero is off when a
  // controller writes this attribute directly; Auto is represented by a
  // nullable SpeedSetting and is handled by matter_fan_mode/delegate below.
  if (!is_ac(profile()) || !control_allowed_() || value > 3) return;
  request_fan(static_cast<int>(value));
}
void ApplianceAuto::matter_vertical_swing(uint64_t value) {
  // Matter RockBitmap bit 1 (0x02) is the vertical/up-down vane.
  if (!is_ac(profile()) || (value != 0 && value != 2)) return;
  request_vertical_swing(value == 2);
}
void ApplianceAuto::request_service(uint8_t service, uint16_t value) {
  if (profile()!=Profile::TAI_AC && profile()!=Profile::TAI_DEHUM) return;
  if (!control_allowed_() || service >= 0x80 || value > 0xFFFE) return;
  generation_.fetch_add(1);
  enqueue_(tai_request(profile()==Profile::TAI_DEHUM ? 4 : 1,
                       static_cast<uint8_t>(0x80 | service), value));
}
bool ApplianceAuto::portal_control(const std::string &command, int service, int value) {
  if (!control_allowed_()) return false;
  if (command == "power") {
    if (is_ac(profile())) {
      if (value == 0) request_climate(0, NAN);
      else {
        const int mode = state_.mode == Mode::OFF ? int(Mode::COOL) : int(state_.mode);
        request_climate(mode, state_.target);
      }
    } else if (profile() == Profile::TAI_DEHUM) {
      request_fan(value ? (state_.speed ? state_.speed : 1) : 0);
    } else return false;
    return true;
  }
  if (command == "mode" && is_ac(profile()) && valid_mode(value)) {
    request_climate(value, NAN); return true;
  }
  if (command == "target" && is_ac(profile()) && value >= 16 && value <= 30) {
    request_climate(-1, float(value)); return true;
  }
  if (command == "fan") {
    const int max = is_ac(profile()) ? 3 : profile() == Profile::TAI_DEHUM ? 4 : -1;
    if (value < 0 || value > max) return false;
    request_fan(value); return true;
  }
  if (command == "vertical" && is_ac(profile()) && (value == 0 || value == 1)) {
    request_service(0x0e, uint16_t(value)); return true;
  }
  if (command == "horizontal" && is_ac(profile()) && (value == 0 || value == 1)) {
    request_service(0x10, uint16_t(value)); return true;
  }
  if (command == "service" && service >= 0 && service < 0x80 && value >= 0 && value <= 0xfffe) {
    request_service(uint8_t(service), uint16_t(value)); return true;
  }
  return false;
}
void ApplianceAuto::request_climate(int mode,float target,int fan) {
  if(!is_ac(profile()) || !control_allowed_() || (mode!=-1 && !valid_mode(mode)) || fan < -1 || fan>3) return;
  if(std::isfinite(target) && !temperature_valid(target)) return;
  generation_.fetch_add(1);
  const Mode m=mode<0 ? state_.mode : static_cast<Mode>(mode);
  if(profile()==Profile::TAI_AC) {
    if(mode==0) { commands_.clear(); enqueue_(tai_request(1,0x80,0)); return; }
    if(mode>0) enqueue_(tai_request(1,0x81,tai_mode(m)));
    if(std::isfinite(target)) enqueue_(tai_request(1,0x83,std::lround(target)));
    if(fan>=0) { const uint8_t map[]{0,2,3,4}; enqueue_(tai_request(1,0x82,map[fan])); }
    if(mode>0) enqueue_(tai_request(1,0x80,1));
  } else {
    auto p=state_.s21_raw;
    // A subsequent target/fan-only request must preserve a pending mode/power change.
    const std::vector<uint8_t> *pending=nullptr;
    if(waiting_ && purpose_==Purpose::WRITE) pending=&request_;
    if(!commands_.empty()) pending=&commands_.back();
    if(pending && pending->size()==9 && (*pending)[1]=='D' && (*pending)[2]=='1')
      std::copy_n(pending->begin()+3,4,p.begin());
    if(mode==0) p[0]='0';
    else {
      if(mode>0) { p[0]='1'; p[1]=s21_mode(m); }
      if(p[1]=='2') p[2]=0x80; // native dry sentinel, not a temperature
      else if(std::isfinite(target)) p[2]=uint8_t(std::lround(target)*2+28);
      else if(mode>0 && (p[2]==0 || p[2]>=0x80)) p[2]=uint8_t(std::lround(cool_target_)*2+28);
      if(fan>=0) { const uint8_t map[]{'A','3','5','7'}; p[3]=map[fan]; }
    }
    enqueue_(s21_request('D','1',std::vector<uint8_t>(p.begin(),p.end())));
  }
}
void ApplianceAuto::matter_mode(uint64_t mode) {
  if(!matter_->ready() || !is_ac(profile()) || !control_allowed_() || mode>8 || !valid_mode(mode)) return;
  generation_.fetch_add(1);
  pending_mode_=mode; climate_due_=millis()+80;
}
void ApplianceAuto::matter_target(bool heating,int64_t value) {
  if(!matter_->ready() || !is_ac(profile()) || !control_allowed_() || value<1600 || value>3000) return;
  generation_.fetch_add(1);
  (heating ? heat_target_ : cool_target_)=value/100.0f;
  target_dirty_=true; climate_due_=millis()+80;
}
Status ApplianceAuto::HandleStep(FC::StepDirectionEnum,bool,bool) { return Status::UnsupportedCommand; }
void ApplianceAuto::OnFanDriveStateChanged(const FC::FanDriveState &s) {
  if(reporting_ || !matter_ || !matter_->ready()) return;
  if (s.mode == FC::FanModeEnum::kOff) {
    generation_.fetch_add(1); pending_fan_.store(0); return;
  }
  if (s.mode == FC::FanModeEnum::kAuto || s.mode == FC::FanModeEnum::kSmart) {
    generation_.fetch_add(1); pending_fan_.store(5); return;
  }
  if (s.speedSetting.IsNull()) return;
  const int speed=s.mode==FC::FanModeEnum::kOn ? 3 : s.speedSetting.Value();
  if(speed<=4) { generation_.fetch_add(1); pending_fan_.store(speed); }
}
void ApplianceAuto::OnRockSettingChanged(chip::BitMask<FC::RockBitmap> value) {
  if (reporting_ || !is_ac(profile()) || !control_allowed_()) return;
  generation_.fetch_add(1);
  request_vertical_swing(value.Has(FC::RockBitmap::kRockUpDown));
}
void ApplianceAuto::publish_state_(const State &s) {
  state_=s;
  publish_service_entities_(s);
  if (ac_fan_ != nullptr && is_ac(profile())) {
    ac_fan_->state=s.power;
    ac_fan_->speed=std::min<uint8_t>(s.speed, 4);
    ac_fan_->publish_state();
  }
  if(profile()==Profile::TAI_DEHUM) {
    fan_->state=s.power; if(s.speed) fan_->speed=s.speed; fan_->publish_state();
  } else {
    climate_->mode=native_mode(s.mode); climate_->target_temperature=s.target; climate_->current_temperature=s.temperature;
    climate_->fan_mode=s.speed==0 ? CLIMATE_FAN_AUTO :
        s.speed<=(wire_s21_?2:2) ? CLIMATE_FAN_LOW : s.speed<=(wire_s21_?3:3) ? CLIMATE_FAN_MEDIUM : CLIMATE_FAN_HIGH;
    climate_->publish_state();
    if(!target_dirty_ && pending_mode_<0 && temperature_valid(s.target)) {
      // Preserve dual Matter setpoints in AUTO until the physical target changes.
      if(s.mode!=Mode::AUTO || !std::isfinite(last_target_) || std::fabs(last_target_-s.target)>=0.5f)
        heat_target_=cool_target_=s.target;
      last_target_=s.target;
    }
  }
  if(matter_->ready() && commands_.empty() && pending_mode_<0 && !target_dirty_ && !report_queued_.exchange(true)) {
    report_state_=s; report_generation_=generation_.load();
    report_heat_=heat_target_; report_cool_=cool_target_;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([this](){
      report_matter_(report_state_,report_generation_,report_heat_,report_cool_);
      report_queued_.store(false);
    });
  }
}
void ApplianceAuto::publish_service_entities_(const State &s) {
  if (profile()!=Profile::TAI_AC && profile()!=Profile::TAI_DEHUM) return;
  for (auto *entity : service_switches_) {
    const uint8_t service = entity->service_id();
    if (s.value_valid[service]) entity->publish_value(s.values[service]);
  }
  for (auto *entity : service_numbers_) {
    const uint8_t service = entity->service_id();
    if (s.value_valid[service]) entity->publish_value(s.values[service]);
  }
  for (auto *entity : service_binary_sensors_) {
    const uint8_t service = entity->service_id();
    if (s.value_valid[service]) entity->publish_value(s.values[service]);
  }
  for (auto *entity : service_selects_) {
    const uint8_t service = entity->service_id();
    if (s.value_valid[service]) entity->publish_value(s.values[service]);
  }
  for (auto *entity : service_sensors_) {
    const uint8_t service = entity->service_id();
    if (s.value_valid[service]) entity->publish_value(s.values[service]);
  }
}
void ApplianceAuto::report_matter_(State s,uint32_t generation,float heat,float cool) {
  if(generation!=generation_.load() || pending_fan_.load()>=0) return;
  if(profile()==Profile::TAI_DEHUM) {
    auto *base=esp_matter::data_model::provider::get_instance().registry().Get({4,FC::Id});
    if(!base) return;
    auto *server=static_cast<chip::app::Clusters::FanControlCluster *>(base);
    const uint8_t speed=s.power ? s.speed : 0;
    reporting_=true; server->SetSpeedSetting(chip::app::DataModel::MakeNullable(speed));
    server->SetSpeedCurrent(speed); server->SetPercentCurrent(speed*25); reporting_=false;
  } else {
    // AC FanControl is part of thermostat endpoint 1, matching the working
    // BC7215 bridge. Keeping it on the same endpoint is required for HA to
    // expose the swing control with the air-conditioner entity.
    auto *fan_base=esp_matter::data_model::provider::get_instance().registry().Get({1,FC::Id});
    if (fan_base != nullptr) {
      auto *fan_server=static_cast<chip::app::Clusters::FanControlCluster *>(fan_base);
      const uint8_t mode = !s.power ? 0 : s.speed == 0 ? 5 :
          s.speed <= 2 ? 1 : s.speed == 3 ? 2 : 3;
      const uint8_t speed = mode >= 1 && mode <= 3 ? mode : 0;
      matter::MatterLocalReportGuard local_report;
      reporting_=true;
      fan_server->SetFanMode(static_cast<FC::FanModeEnum>(mode));
      fan_server->SetSpeedSetting(chip::app::DataModel::MakeNullable(speed));
      fan_server->SetSpeedCurrent(speed);
      fan_server->SetPercentCurrent(speed == 0 ? 0 : speed == 1 ? 33 : speed == 2 ? 67 : 100);
      chip::BitMask<FC::RockBitmap> rock;
      if (s.value_valid[0x0e] && s.values[0x0e] != 0)
        rock.Set(FC::RockBitmap::kRockUpDown);
      fan_server->SetRockSetting(rock);
      reporting_=false;
    }
    // Run this report as one Matter-task work item; no nested stale work queue.
    matter::set_attribute_value_now(1,0x0201,0x001c,uint64_t(s.mode));
    matter::set_attribute_value_now(1,0x0201,0x0019,int64_t(0));
    if(std::isfinite(s.temperature)) matter::set_attribute_value_now(1,0x0201,0x0000,int64_t(std::lround(s.temperature*100)));
    else {
      auto unknown=esp_matter_nullable_int16(nullable<int16_t>());
      esp_matter::attribute::update(1,0x0201,0x0000,&unknown);
    }
    if(temperature_valid(s.target)) {
      matter::set_attribute_value_now(1,0x0201,0x0011,int64_t(std::lround(cool*100)));
      matter::set_attribute_value_now(1,0x0201,0x0012,int64_t(std::lround(heat*100)));
    }
  }
}
void ApplianceAuto::loop() {
  if(rebooting_ || migration_required_) return;
  receive_();
  const uint32_t now=millis();
  const int fan=pending_fan_.exchange(-1);
  if(fan==5) matter_fan_mode(5);
  else if(fan>=0) request_fan(fan);
  if((pending_mode_>=0 || target_dirty_) && int32_t(now-climate_due_)>=0) {
    const int m=pending_mode_>=0 ? pending_mode_ : int(state_.mode);
    float t=NAN;
    if(target_dirty_) t=std::round(m==1 ? (heat_target_+cool_target_)/2 : m==4 ? heat_target_ : cool_target_);
    request_climate(pending_mode_,t); pending_mode_=-1; target_dirty_=false;
  }
  if(waiting_ || int32_t(now-next_ms_)<0) return;
  if(phase_!=Phase::READY) { probe_next_(); return; }
  if(!online_ || uint32_t(now-last_status_ms_)>=15000) { begin_probe_(candidate_); return; }
  if(!commands_.empty()) {
    auto cmd=std::move(commands_.front()); commands_.pop_front(); send_(std::move(cmd),Purpose::WRITE);
  } else if(last_poll_ms_==0 || uint32_t(now-last_poll_ms_)>=5000) poll_();
}
static std::string quote(const std::string &s) {
  std::string r="\""; for(unsigned char c:s) { if(c=='"' || c=='\\') r+='\\'; if(c>=32) r+=char(c); } return r+'"';
}
void ApplianceAuto::snapshot_(const char *status) {
  const auto &p=configured() ? stored_ : detected_;
  const char *protocol=p.type==Profile::S21_AC ? "Daikin S21" : p.type==Profile::UNKNOWN ? "Unknown" : "TaiSEIA";
  const char *kind=p.type==Profile::TAI_DEHUM ? "dehumidifier" : is_ac(p.type) ? "air_conditioner" : "unknown";
  std::string temperature="null";
  if(online() && is_ac(profile()) && std::isfinite(state_.temperature) &&
     (!wire_s21_ || uint32_t(millis()-temperature_ms_)<20000)) {
    char value[24]; std::snprintf(value,sizeof(value),"%.1f",double(state_.temperature));
    temperature=value;
  }
  auto service_json = [this](uint8_t id) {
    return state_.value_valid[id] ? std::to_string(state_.values[id]) : "null";
  };
  std::string target = std::isfinite(state_.target) ? std::to_string(state_.target) : "null";
  std::string s="{\"status\":"+quote(status)+",\"type\":"+quote(kind)+",\"protocol\":"+quote(protocol)+
      ",\"brand\":"+quote(p.brand)+",\"model\":"+quote(p.model)+",\"online\":"+(online()?"true":"false")+
      ",\"locked\":"+(configured()?"true":"false")+",\"inverted\":"+(p.inverted?"true":"false")+
      ",\"return_air_temperature\":"+temperature+
      ",\"power\":"+(state_.power?"true":"false")+
      ",\"mode\":"+std::to_string(int(state_.mode))+
      ",\"speed\":"+std::to_string(unsigned(state_.speed))+
      ",\"target\":"+target+
      ",\"vertical_swing\":"+service_json(0x0e)+
      ",\"horizontal_swing\":"+service_json(0x10)+
      ",\"services\":{\"1\":"+service_json(1)+",\"3\":"+service_json(3)+",\"4\":"+service_json(4)+
      ",\"5\":"+service_json(5)+",\"6\":"+service_json(6)+",\"8\":"+service_json(8)+
      ",\"11\":"+service_json(11)+",\"12\":"+service_json(12)+",\"14\":"+service_json(14)+
      ",\"16\":"+service_json(16)+",\"19\":"+service_json(19)+",\"20\":"+service_json(20)+
      ",\"22\":"+service_json(22)+",\"23\":"+service_json(23)+",\"24\":"+service_json(24)+
      ",\"26\":"+service_json(26)+",\"27\":"+service_json(27)+",\"30\":"+service_json(30)+
      ",\"32\":"+service_json(32)+",\"41\":"+service_json(41)+",\"57\":"+service_json(57)+"}}";
  { LockGuard lock(snapshot_mutex_); snapshot_json_=std::move(s); }
  if(status_sensor_ && status_sensor_->state!=status) status_sensor_->publish_state(status);
  if(protocol_sensor_ && protocol_sensor_->state!=protocol) protocol_sensor_->publish_state(protocol);
}
std::string ApplianceAuto::status_json() { LockGuard lock(snapshot_mutex_); return snapshot_json_; }
void ApplianceAuto::dump_config() {
  ESP_LOGCONFIG(TAG,"Universal 0.6.2: TaiSEIA SA 1/4 + Daikin S21; profile=%u",unsigned(profile()));
  ESP_LOGCONFIG(TAG,"Auto-detection uses read queries only. Four-speed dehumidifier profile.");
}
} // namespace
