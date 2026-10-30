"""Exercise the actual dump_config() with CHIP unavailable, as on first boot.

CONFIG logging is compiled out, matching the shipped INFO logger setting.
The fake CHIP mutex aborts if used before platform initialization. This tests
the lifecycle boundary missed by protocol-only tests, not the actual radio.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('--expect-unsafe', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
method = 'void MatterComponent::dump_config() {' + source.split(
    'void MatterComponent::dump_config() {', 1)[1].split('\n}\n', 1)[0] + '\n}\n'
fixture = r'''
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstring>
#include <string>
#include <vector>
#define ESP_LOGCONFIG(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
namespace chip {
constexpr size_t kFabricLabelMaxLengthInBytes=32;
struct Fabric {
  std::string label="test";
  std::string GetFabricLabel() const { return label; }
  uint64_t GetCompressedFabricId() const { return 1; }
  uint64_t GetNodeId() const { return 2; }
};
struct Fabrics : std::vector<Fabric> { size_t FabricCount() const { return size(); } };
namespace DeviceLayer {
struct Platform {
  bool initialized=false;
  int locks=0,unlocks=0,held=0;
  void LockChipStack() { assert(initialized); ++locks; ++held; }
  void UnlockChipStack() { assert(held==1); ++unlocks; --held; }
};
Platform &PlatformMgr() { static Platform p; return p; }
}
struct Server {
  Fabrics fabrics;
  static int accesses;
  static Server &GetInstance() {
    assert(DeviceLayer::PlatformMgr().held==1);
    ++accesses;
    static Server s; return s;
  }
  Fabrics &GetFabricTable() { return fabrics; }
};
int Server::accesses=0;
}
class MatterComponent {
 public:
  bool started=false,failed=false;
  std::string qr_code_,manual_pairing_code_;
  bool is_failed() const { return failed; }
  bool ready() const { return started; }
  void dump_config();
};
'''
checks = r'''
int main() {
  MatterComponent pending;
  auto &platform=chip::DeviceLayer::PlatformMgr();
  pending.dump_config(); // Fresh device, no appliance, CHIP has no mutex.
  pending.dump_config(); // Repeated diagnostic requests must also be safe.
  assert(platform.locks==0 && chip::Server::accesses==0);
  pending.failed=true;
  pending.dump_config();
  assert(platform.locks==0 && chip::Server::accesses==0);
  pending.failed=false;
  platform.initialized=true;
  pending.started=true;
  pending.dump_config(); // Initialized, unpaired device.
  assert(platform.locks==1 && platform.unlocks==1 && platform.held==0);
  platform.LockChipStack();
  chip::Server::GetInstance().fabrics.emplace_back();
  platform.UnlockChipStack();
  pending.dump_config(); // Initialized, paired device.
  assert(platform.locks==3 && platform.unlocks==3 && platform.held==0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'startup.cpp'
    path.write_text(fixture+method+checks)
    exe=Path(tmp)/'startup'
    subprocess.run(['g++','-std=c++17','-fsanitize=undefined','-fno-sanitize-recover=all',str(path),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    if args.expect_unsafe:
        assert result.returncode!=0 and 'initialized' in result.stderr, result
        print('PASS: regression reproduces the old uninitialized CHIP mutex crash')
    else:
        assert result.returncode==0, result.stderr
        print('PASS: pending/failed Matter never touches CHIP; ready/paired diagnostics balance locks')
