"""Compile the actual protocol + controller with deterministic UART/NVS/SDK fakes.
This validates logic, not the ESP32 radio or any physical appliance.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('components',type=Path);args=p.parse_args()
root=args.components/'appliance_auto'
def body(path):
    return '\n'.join(l for l in path.read_text(encoding='utf8').splitlines() if not l.startswith(('#include','#pragma once')))
sdk=re.search(r"sdk = r'''(.*?)'''",(Path(__file__).parent/'dehumidifier_host_check.py').read_text(encoding='utf8'),re.S)[1]
prefix=r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
using std::size_t;
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGCONFIG(...) ((void)0)
uint32_t clock_ms=100;
uint32_t millis(){return clock_ms;}
constexpr int ESP_OK=0, UART_SIGNAL_TXD_INV=1, UART_SIGNAL_RXD_INV=2;
using uart_port_t=int; using nvs_handle_t=int;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
std::vector<uint8_t> stored_blob;
bool legacy_pin=false;uint32_t reset_ok=0;
int nvs_open(const char*,int,int*h){*h=1;return 0;}
void nvs_close(int){}
int nvs_get_blob(int,const char*,void*p,size_t*n){if(stored_blob.size()!=*n)return 1;std::memcpy(p,stored_blob.data(),*n);return 0;}
int nvs_set_blob(int,const char*,const void*p,size_t n){stored_blob.assign((uint8_t*)p,(uint8_t*)p+n);return 0;}
int nvs_commit(int){return 0;}
int nvs_get_u32(int,const char*k,uint32_t*v){if(std::string(k)=="pin-code"){if(!legacy_pin)return 1;*v=1;}else *v=reset_ok;return 0;}
int nvs_set_u32(int,const char*,uint32_t v){reset_ok=v;return 0;}
int nvs_erase_all(int){stored_blob.clear();return 0;}
int inversion=0;
int uart_set_line_inverse(int,int mask){inversion=mask;return 0;}
template<class T>struct nullable{};
struct Attr {int x;};
Attr esp_matter_nullable_int16(nullable<int16_t>){return {0};}
namespace esphome {
struct Mutex {};
struct LockGuard {LockGuard(Mutex&){};};
namespace setup_priority {constexpr float DATA=600;}
struct Application {int reboots=0;void safe_reboot(){reboots++;}} App;
class Component {
 public:
  std::map<std::string,std::pair<uint32_t,std::function<void()>>> timers;
  virtual void setup(){};virtual void loop(){};virtual void dump_config(){};virtual float get_setup_priority()const{return 0;}
  bool failed=false;void mark_failed(){failed=true;}
  template<class F>void set_timeout(const char*n,uint32_t ms,F fn){timers[n]={clock_ms+ms,fn};}
  void run_timers(){std::vector<std::function<void()>> pending;for(auto i=timers.begin();i!=timers.end();){if(int32_t(clock_ms-i->second.first)>=0){pending.push_back(i->second.second);i=timers.erase(i);}else ++i;}for(auto&f:pending)f();}
};
namespace uart {
enum UARTParityOptions {UART_CONFIG_PARITY_NONE,UART_CONFIG_PARITY_EVEN};
class UARTComponent {
 public:
  std::deque<uint8_t> input;std::vector<std::vector<uint8_t>> output;
  uint32_t baud=0; int bits=0,stop=0;UARTParityOptions parity{};
  void set_flush_timeout(int){};void flush(){};void flush_input(){input.clear();}
  void set_baud_rate(uint32_t v){baud=v;}void set_data_bits(int v){bits=v;}
  void set_stop_bits(int v){stop=v;}void set_parity(UARTParityOptions v){parity=v;}
  int apply_settings_live(){return 0;}int get_hw_serial_number(){return 1;}
  void write_array(const std::vector<uint8_t>&v){output.push_back(v);}
  bool available(){return !input.empty();}bool read_byte(uint8_t*b){if(input.empty())return false;*b=input.front();input.pop_front();return true;}
};
using IDFUARTComponent=UARTComponent;
class UARTDevice {
 public:UARTComponent *parent_{};
  void write_array(const std::vector<uint8_t>&v){parent_->write_array(v);}
  void write_byte(uint8_t b){parent_->write_array({b});}
  bool available(){return parent_->available();}bool read_byte(uint8_t*b){return parent_->read_byte(b);}
};
}
namespace climate {
enum ClimateMode {CLIMATE_MODE_OFF,CLIMATE_MODE_HEAT_COOL,CLIMATE_MODE_COOL,CLIMATE_MODE_HEAT,CLIMATE_MODE_FAN_ONLY,CLIMATE_MODE_DRY,CLIMATE_MODE_AUTO};
enum ClimateFanMode {CLIMATE_FAN_AUTO,CLIMATE_FAN_LOW,CLIMATE_FAN_MEDIUM,CLIMATE_FAN_HIGH};
constexpr int CLIMATE_SUPPORTS_CURRENT_TEMPERATURE=1;
class ClimateTraits {public:void set_supported_modes(std::initializer_list<ClimateMode>){};void set_supported_fan_modes(std::initializer_list<ClimateFanMode>){};
void add_feature_flags(int){};void set_visual_min_temperature(int){};void set_visual_max_temperature(int){};void set_visual_temperature_step(int){};};
struct ClimateCall {std::optional<ClimateMode> mode;std::optional<float> target;std::optional<ClimateFanMode> fan;
auto get_mode()const{return mode;}auto get_target_temperature()const{return target;}auto get_fan_mode()const{return fan;}};
class Climate {public:ClimateMode mode{};float target_temperature=NAN,current_temperature=NAN;std::optional<ClimateFanMode>fan_mode;
bool internal=false;int published=0;void set_internal(bool v){internal=v;}void publish_state(){published++;}
virtual ClimateTraits traits()=0;virtual void control(const ClimateCall&)=0;};
}
namespace fan {
struct FanCall {std::optional<bool> state;std::optional<int> speed;auto get_state()const{return state;}auto get_speed()const{return speed;}};
class FanTraits {public:FanTraits(bool,bool,bool,int){};};
class Fan {public:bool state=false,internal=false;int speed=1,published=0;void set_internal(bool v){internal=v;}void publish_state(){published++;}
virtual FanTraits get_traits()=0;virtual void control(const FanCall&)=0;};
}
namespace text_sensor {class TextSensor {public:std::string state;void publish_state(std::string v){state=v;}};}
namespace matter {
class MatterComponent {public:bool started=false;bool ready()const{return started;}};
struct Update {int ep,cl,attr;int64_t value;};std::vector<Update> updates;
template<class T>void set_attribute_value_now(int ep,int cl,int a,T v){
  // Mirror the real bridge's signed INT8 dispatch for MinSetpointDeadBand.
  if(a==0x19 && !std::is_same_v<T,int64_t>)return;
  updates.push_back({ep,cl,a,int64_t(v)});
}
}
}
namespace esp_matter::attribute {int null_temperatures=0;int update(int ep,int cl,int attr,Attr*){if(ep==1&&cl==0x0201&&attr==0)++null_temperatures;return 0;}}
'''
tests=r'''
using namespace esphome;
using namespace esphome::appliance_auto;
struct Rig {
  uart::IDFUARTComponent bus;AutoClimate ac;AutoFan fan;matter::MatterComponent matter;ApplianceAuto hub;
  Rig(){chip::DeviceLayer::scheduler.pending.clear();hub.parent_=&bus;hub.set_climate(&ac);hub.set_fan(&fan);hub.set_matter(&matter);ac.set_parent(&hub);fan.set_parent(&hub);hub.setup();}
  void advance(uint32_t ms=101){clock_ms+=ms;hub.run_timers();hub.loop();}
  void reply(std::vector<uint8_t> f,bool ack=false){if(ack)bus.input.push_back(6);for(auto b:f)bus.input.push_back(b);hub.loop();}
};
std::vector<uint8_t> tai(std::initializer_list<uint8_t> data){std::vector<uint8_t> f(data);f[0]=f.size()+1;f.push_back(xor_sum(f.data(),f.size()));return f;}
auto dehum(int power=1,int speed=3){return tai({0,0,8,0,0,uint8_t(power),14,0,uint8_t(speed)});}
auto air(){return tai({0,0,8,0,0,1,1,0,0,2,0,3,3,0,24,4,0,26});}
void valid_tai_probe(Rig&r,int kind=4,const char*brand="HITACHI"){
  r.advance(301); // registration
  r.reply(tai({0,0,0,0,0}));r.advance();
  assert(r.bus.output.back()==tai_request(0,4));
  r.reply(tai({0,0,4,0,uint8_t(kind)}));r.advance();
  r.reply(tai({0,0,4,0,uint8_t(kind)}));r.advance();
  std::vector<uint8_t> b{0,0,5};for(const char*p=brand;*p;p++)b.push_back(*p);b[0]=b.size()+1;b.push_back(xor_sum(b.data(),b.size()));
  r.reply(b);r.advance();r.reply(tai({0,0,6,'T','E','S','T'}));r.advance();
  r.reply(kind==4?dehum():air());r.advance();r.reply(kind==4?dehum():air());
}
void drain_commands(Rig&r){for(int i=0;i<8;i++){r.advance(101);if(r.hub.waiting_ && r.hub.purpose_==ApplianceAuto::Purpose::WRITE){if(r.hub.wire_s21_)r.reply({6});else r.reply(r.hub.request_);}else break;}}
int main(){
  State s;
  // Wire fixtures are explicit and independent of the builders under test.
  assert(tai_request(0,4)==std::vector<uint8_t>({6,0,4,255,255,2}));
  assert(tai_request(4,0x8e,3)==std::vector<uint8_t>({6,4,142,0,3,143}));
  assert(s21_request('F','1')==std::vector<uint8_t>({2,'F','1',0x77,3}));
  assert(tai_identity({6,0,4,0,1,3})==Profile::TAI_AC);
  assert(tai_identity({6,0,4,0,4,6})==Profile::TAI_DEHUM);
  assert(tai_identity(tai_request(0,4))==Profile::UNKNOWN);
  assert(tai_state(dehum(),Profile::TAI_DEHUM,s)&&s.speed==3&&s.power);
  assert(tai_state(air(),Profile::TAI_AC,s)&&s.target==24&&s.mode==Mode::COOL);
  assert(!tai_state(air(),Profile::TAI_DEHUM,s));assert(!tai_state(dehum(),Profile::TAI_AC,s));
  for(auto f:{dehum(2,1),dehum(1,0),dehum(1,5),tai({0,0,8,0,0,1,14,0,2,14,0,3})})assert(!tai_state(f,Profile::TAI_DEHUM,s));
  auto f=air();for(size_t n=0;n<f.size();n++){auto t=f;t.resize(n);assert(!tai_state(t,Profile::TAI_AC,s));}
  for(size_t i=0;i<f.size();i++){auto t=f;t[i]^=1;assert(!tai_state(t,Profile::TAI_AC,s));}
  auto basic=s21_request('G','1',{'1','3',76,'5'});
  assert(s21_state(basic,s)&&s.target==24&&s.mode==Mode::COOL&&s.speed==3);
  assert(!s21_state(s21_request('F','1'),s));assert(!s21_state(s21_request('G','1',{'2','3',76,'5'}),s));
  for(size_t n=0;n<basic.size();n++){auto t=basic;t.resize(n);assert(!s21_state(t,s));}
  for(int mode:{'0','1','2','3','4','6','7'})assert(s21_state(s21_request('G','1',{'1',uint8_t(mode),0x80,'A'}),s));
  // Deterministic noise corpus / parser bounds under undefined-behavior sanitizer.
  uint32_t rng=42;for(int n=0;n<100000;n++){std::vector<uint8_t> v(n%260);for(auto&b:v){rng=rng*1664525+1013904223;b=rng>>24;}tai_identity(v);tai_text(v,5);tai_state(v,Profile::TAI_AC,s);tai_state(v,Profile::TAI_DEHUM,s);s21_state(v,s);}
  puts("PASS: framing, CRC, query-echo rejection, bounds, modes, temperatures; 100000 noise packets");

  stored_blob.clear();Rig first;first.hub.request_fan(3);first.hub.request_climate(3,24);assert(first.bus.output.empty());
  assert(first.ac.internal&&first.fan.internal&&!first.hub.configured());
  valid_tai_probe(first);assert(!stored_blob.empty());assert(first.hub.rebooting_);first.advance(1300);assert(App.reboots==1);
  for(auto &tx:first.bus.output)assert(tx.size()==1 || tx[2]<0x80); // No control writes during identification.
  Rig deh;assert(deh.ac.internal&&!deh.fan.internal&&deh.hub.endpoint_enabled(4)&&!deh.hub.endpoint_enabled(1));
  assert(!deh.hub.endpoint_enabled(2) && !deh.hub.endpoint_enabled(3));
  deh.hub.request_fan(3);assert(deh.bus.output.empty()); // Persisted identity alone never enables writes.
  valid_tai_probe(deh);assert(deh.hub.online_&&deh.fan.state&&deh.fan.speed==3);
  deh.matter.started=true;
  deh.hub.request_fan(4);assert(deh.hub.commands_.size()==2);assert(deh.hub.commands_[0]==tai_request(4,0x8e,4));
  deh.hub.request_fan(0);assert(deh.hub.commands_.size()==1&&deh.hub.commands_[0]==tai_request(4,0x80,0));
  assert(deh.fan.state); // No optimistic state.
  drain_commands(deh);
  deh.hub.commands_.clear();deh.hub.request_fan(5);assert(deh.hub.commands_.empty());
  // Matter callback queues only. Main-loop validation rejects offline requests.
  auto&server=esp_matter::data_model::provider::server;server.delegate=&deh.hub;
  deh.hub.online_=false;size_t before=deh.bus.output.size();
  deh.hub.OnFanDriveStateChanged({FC::FanModeEnum::kHigh,chip::app::DataModel::MakeNullable<uint8_t>(4)});
  assert(deh.bus.output.size()==before);deh.advance();assert(deh.hub.commands_.empty());
  Rig mismatch;valid_tai_probe(mismatch,1,"Panasonic");assert(mismatch.hub.mismatch_&&!mismatch.hub.online_);
  mismatch.hub.request_climate(3,24);assert(mismatch.hub.commands_.empty());
  assert(mismatch.hub.profile()==Profile::TAI_DEHUM); // Descriptor type never changes silently.
  puts("PASS: identification confirmation, persistence/reboot, hidden entities, locked type, offline command blocking");

  stored_blob.clear();Rig ac_first;valid_tai_probe(ac_first,1);Rig ac;valid_tai_probe(ac,1);ac.matter.started=true;
  assert(ac.hub.endpoint_enabled(1) && !ac.hub.endpoint_enabled(2) && !ac.hub.endpoint_enabled(3) && !ac.hub.endpoint_enabled(4));
  ac.hub.publish_state_(ac.hub.state_);chip::DeviceLayer::scheduler.run();
  assert(std::any_of(matter::updates.begin(),matter::updates.end(),[](const auto &u){return u.attr==0x19&&u.value==0;}));
  assert(std::any_of(matter::updates.begin(),matter::updates.end(),[](const auto &u){return u.attr==0&&u.value==2600;}));
  assert(ac.hub.status_json().find("\"return_air_temperature\":26.0")!=std::string::npos);
  ac.hub.request_climate(4,23);assert(ac.hub.commands_.size()==3);
  assert(ac.hub.commands_[0]==tai_request(1,0x81,4));assert(ac.hub.commands_[1]==tai_request(1,0x83,23));
  ac.hub.request_climate(-1,25);assert(ac.hub.commands_.size()==3);
  assert(ac.hub.commands_[0]==tai_request(1,0x81,4)&&ac.hub.commands_[1]==tai_request(1,0x83,25));
  ac.hub.request_climate(0,28,3);assert(ac.hub.commands_.size()==1&&ac.hub.commands_[0]==tai_request(1,0x80,0));
  ac.hub.commands_.clear();ac.hub.request_climate(3,31);assert(ac.hub.commands_.empty());
  ac.hub.matter_mode(1);ac.hub.matter_target(true,2000);ac.hub.matter_target(false,2600);ac.advance(90);
  assert(ac.hub.commands_[1]==tai_request(1,0x83,23)); // Single physical target = rounded AUTO midpoint.
  ac.hub.commands_.clear();ac.hub.waiting_=false;ac.hub.publish_state_(ac.hub.state_);
  const auto count=matter::updates.size();ac.hub.matter_mode(0);chip::DeviceLayer::scheduler.run();assert(matter::updates.size()==count);
  ac.hub.begin_probe_(0);chip::DeviceLayer::scheduler.run();
  assert(std::isnan(ac.ac.current_temperature) && esp_matter::attribute::null_temperatures>0);
  assert(ac.hub.status_json().find("\"return_air_temperature\":null")!=std::string::npos);

  // S21 auto probe, optional FC model failure, and ACK-only write semantics.
  stored_blob.clear();Rig sfirst;sfirst.hub.begin_probe_(2);sfirst.advance(301);
  sfirst.reply(basic,true);sfirst.advance();sfirst.reply(basic,true);sfirst.advance();sfirst.reply({0x15});
  assert(sfirst.hub.rebooting_);Rig srig;
  assert(srig.bus.baud==2400&&srig.bus.parity==uart::UART_CONFIG_PARITY_EVEN&&srig.bus.stop==2);
  srig.advance(301);srig.reply(basic,true);srig.advance();srig.reply(basic,true);srig.advance();srig.reply({0x15});
  assert(srig.hub.online_);srig.hub.request_climate(8,NAN);
  assert(srig.hub.commands_.front()==s21_request('D','1',{'1','2',0x80,'5'}));
  srig.hub.request_climate(0,NAN);assert(srig.hub.commands_.front()==s21_request('D','1',{'0','2',0x80,'5'}));
  srig.hub.commands_.clear();srig.hub.request_climate(4,30,3);assert(srig.hub.commands_.front()==s21_request('D','1',{'1','4',88,'7'}));
  srig.hub.request_climate(-1,25);assert(srig.hub.commands_.front()==s21_request('D','1',{'1','4',78,'7'}));
  drain_commands(srig);assert(srig.hub.state_.mode==Mode::COOL); // ACK is not a physical state report.
  // S21 return-air temperature is little-endian ASCII tenths, with a final sign.
  srig.matter.started=true;srig.hub.commands_.clear();
  srig.hub.send_(s21_request('R','H'),ApplianceAuto::Purpose::TEMPERATURE);
  srig.reply(s21_request('S','H',{'6','5','2','+'}),true);
  assert(std::fabs(srig.hub.state_.temperature-25.6f)<0.001f);
  srig.hub.poll_();srig.reply(basic,true);chip::DeviceLayer::scheduler.run();
  assert(std::fabs(srig.ac.current_temperature-25.6f)<0.001f);
  assert(srig.hub.status_json().find("\"return_air_temperature\":25.6")!=std::string::npos);
  assert(std::any_of(matter::updates.begin(),matter::updates.end(),[](const auto &u){return u.attr==0&&u.value==2560;}));
  srig.hub.send_(s21_request('R','H'),ApplianceAuto::Purpose::TEMPERATURE);
  srig.reply(s21_request('S','H',{'5','2','0','-'}),true);
  assert(srig.hub.state_.temperature==-2.5f);
  const auto valid_temperature_time=srig.hub.temperature_ms_;
  srig.hub.send_(s21_request('R','H'),ApplianceAuto::Purpose::TEMPERATURE);
  srig.reply(s21_request('S','H',{'X','2','0','+'}),true);
  assert(srig.hub.temperature_ms_==valid_temperature_time && srig.hub.state_.temperature==-2.5f);
  clock_ms+=20001;const auto null_count=esp_matter::attribute::null_temperatures;
  srig.hub.poll_();srig.reply(basic,true);chip::DeviceLayer::scheduler.run();
  assert(std::isnan(srig.ac.current_temperature) && esp_matter::attribute::null_temperatures>null_count);
  assert(srig.hub.status_json().find("\"return_air_temperature\":null")!=std::string::npos);
  puts("PASS: S21 return-air tenths/sign, malformed reply rejection, native/Matter/JSON output and 20-second expiry");
  srig.hub.clear_profile();assert(stored_blob.empty());
  StoredProfile old_profile{};old_profile.magic=0x0600a1ef;old_profile.type=Profile::TAI_AC;
  stored_blob.assign(reinterpret_cast<uint8_t*>(&old_profile),reinterpret_cast<uint8_t*>(&old_profile)+sizeof(old_profile));
  legacy_pin=true;reset_ok=0;Rig legacy;assert(legacy.hub.migration_required_);
  assert(!legacy.hub.configured());
  legacy.advance(1000);assert(legacy.bus.output.empty());
  legacy.hub.clear_profile();Rig after_reset;assert(!after_reset.hub.migration_required_);
  after_reset.hub.begin_probe_(3);assert(inversion==(UART_SIGNAL_TXD_INV|UART_SIGNAL_RXD_INV));
  after_reset.advance(301);after_reset.reply(basic,false);assert(!after_reset.hub.online_ && after_reset.hub.waiting_);
  after_reset.advance(701);assert(!after_reset.hub.online_); // No ACK means no identification.
  after_reset.hub.begin_probe_(0);assert(inversion==0 && after_reset.bus.baud==9600 && after_reset.bus.stop==1);
  puts("PASS: AC/dehumidifier commands, off priority, AUTO midpoint, stale reports, S21 framing and no optimistic ACK reports");
}
'''
header=body(root/'appliance_auto.h').replace(' protected:',' public:')
source=prefix+sdk+body(root/'protocol.h')+header+body(root/'appliance_auto.cpp')+tests
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(source,encoding='utf8')
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
