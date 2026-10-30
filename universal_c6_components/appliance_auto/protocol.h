// GPL-3.0-or-later. Protocol mappings reference tsunglung/taixia and
// asund/esphome-daikin-s21; see SOURCES.txt. No ESPHome or SDK dependency.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::appliance_auto {
enum class Profile : uint8_t { UNKNOWN, TAI_AC, TAI_DEHUM, S21_AC };
enum class Mode : uint8_t { OFF=0, AUTO=1, COOL=3, HEAT=4, FAN=7, DRY=8 };
inline bool valid_mode(int m) { return m==0 || m==1 || m==3 || m==4 || m==7 || m==8; }
inline bool is_ac(Profile p) { return p==Profile::TAI_AC || p==Profile::S21_AC; }
struct State {
  bool power{false}; Mode mode{Mode::OFF}; uint8_t speed{0};
  float target{NAN}, temperature{NAN};
  std::array<uint8_t,4> s21_raw{};
  // TaiSEIA status frames carry many optional services in the same triplet
  // format. Keep the raw values so the ESPHome entities can expose the
  // switches/numbers from the upstream taixia examples without inventing
  // brand-specific semantics.
  std::array<uint16_t,128> values{};
  std::array<uint8_t,128> value_valid{};
};
inline uint8_t xor_sum(const uint8_t *p, size_t n) {
  uint8_t v=0; while(n--) v^=*p++; return v;
}
inline bool tai_valid(const std::vector<uint8_t> &f) {
  return f.size()>=6 && f.size()<=200 && f[0]==f.size() && xor_sum(f.data(),f.size())==0;
}
inline std::vector<uint8_t> tai_request(uint8_t sa, uint8_t svc, uint16_t value=0xffff) {
  std::vector<uint8_t> f{6,sa,svc,uint8_t(value>>8),uint8_t(value),0};
  f[5]=xor_sum(f.data(),5); return f;
}
inline Profile tai_identity(const std::vector<uint8_t> &f) {
  if (!tai_valid(f) || f.size()!=6 || f[1]!=0 || f[2]!=4 || f[3]!=0) return Profile::UNKNOWN;
  return f[4]==1 ? Profile::TAI_AC : f[4]==4 ? Profile::TAI_DEHUM : Profile::UNKNOWN;
}
inline std::string tai_text(const std::vector<uint8_t> &f, uint8_t svc) {
  if (!tai_valid(f) || f[1]!=0 || f[2]!=svc) return {};
  std::string s;
  for(size_t i=3;i+1<f.size() && s.size()<39;i++) {
    if(f[i]==0) break;
    if(f[i]<32 || f[i]>126) return {}; // unknown encoding: don't invent a brand
    s+=char(f[i]);
  }
  return s;
}
inline bool tai_state(const std::vector<uint8_t>& f, Profile profile, State &out) {
  if (!tai_valid(f) || f[1]!=0 || f[2]!=8 || (f.size()-4)%3) return false;
  if(profile!=Profile::TAI_AC && profile!=Profile::TAI_DEHUM) return false;
  State s; bool power=false,speed=false,mode=false,target=false;
  std::array<bool,128> seen{};
  for(size_t i=3;i+2<f.size()-1;i+=3) {
    const uint8_t svc=f[i]; const uint16_t v=uint16_t(f[i+1])*256+f[i+2];
    if(svc>=128 || seen[svc]) return false;
    seen[svc]=true;
    if(v==0xffff) continue;
    s.values[svc]=v; s.value_valid[svc]=1;
    if(svc==0) { if(v>1) return false; s.power=v; power=true; }
    if(profile==Profile::TAI_DEHUM) {
      if(svc==14) { if(v>4) return false; s.speed=v; speed=true; }
    } else {
      if(svc==1) {
        if(v>4) return false;
        constexpr Mode modes[]{Mode::COOL,Mode::DRY,Mode::FAN,Mode::AUTO,Mode::HEAT};
        s.mode=modes[v]; mode=true;
      }
      if(svc==2) { if(v>6) return false; s.speed=v; speed=true; }
      if(svc==3) { if(v<10 || v>40) return false; s.target=v; target=true; }
      if(svc==4) { const int16_t t=v; if(t>=-40 && t<=85) s.temperature=t; }
    }
  }
  if(!power || !speed) return false;
  if(profile==Profile::TAI_DEHUM && s.power && !s.speed) return false;
  if(profile==Profile::TAI_AC && (!mode || !target)) return false;
  if(!s.power) s.mode=Mode::OFF;
  out=s; return true;
}
inline uint8_t s21_sum(const uint8_t *p,size_t n) {
  uint8_t c=0; while(n--) c+=*p++;
  if(c==2 || c==3 || c==6 || c==0x15) c+=2;
  return c;
}
inline std::vector<uint8_t> s21_request(char a,char b,const std::vector<uint8_t>& payload={}) {
  std::vector<uint8_t> f{2,uint8_t(a),uint8_t(b)};
  f.insert(f.end(),payload.begin(),payload.end());
  f.push_back(s21_sum(f.data()+1,f.size()-1)); f.push_back(3); return f;
}
inline bool s21_valid(const std::vector<uint8_t>&f,char a,char b,size_t payload) {
  return f.size()==payload+5 && f.front()==2 && f.back()==3 && f[1]==uint8_t(a) &&
         f[2]==uint8_t(b) && s21_sum(f.data()+1,f.size()-3)==f[f.size()-2];
}
inline bool s21_state(const std::vector<uint8_t>&f,State &out) {
  if(!s21_valid(f,'G','1',4) || (f[3]!='0' && f[3]!='1')) return false;
  State s; s.power=f[3]=='1';
  switch(f[4]) {
    case '0': case '1': case '7': s.mode=Mode::AUTO; break;
    case '2': s.mode=Mode::DRY; break;
    case '3': s.mode=Mode::COOL; break;
    case '4': s.mode=Mode::HEAT; break;
    case '6': s.mode=Mode::FAN; break;
    default: return false;
  }
  if(f[6]=='A') s.speed=0;
  else if(f[6]=='B') s.speed=1;
  else if(f[6]>='3' && f[6]<='7') s.speed=f[6]-'2';
  else return false;
  if(f[5]>=48 && f[5]<=108) s.target=(f[5]-28)/2.0f;
  else if(f[5]!=0 && f[5]!=0x80 && f[5]!=0xc0) return false;
  for(size_t i=0;i<4;i++) s.s21_raw[i]=f[i+3];
  if(!s.power) s.mode=Mode::OFF;
  out=s; return true;
}
// Command builders reject unknown/out-of-range requests before touching UART.
inline uint8_t tai_mode(Mode m) {
  switch(m) {case Mode::COOL:return 0;case Mode::DRY:return 1;case Mode::FAN:return 2;
    case Mode::HEAT:return 4;default:return 3;}
}
inline uint8_t s21_mode(Mode m) {
  switch(m) {case Mode::COOL:return '3';case Mode::DRY:return '2';case Mode::FAN:return '6';
    case Mode::HEAT:return '4';default:return '1';}
}
inline bool temperature_valid(float t) { return std::isfinite(t) && t>=16 && t<=30; }
} // namespace
