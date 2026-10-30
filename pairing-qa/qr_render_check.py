"""Host-check the EXACT SVG rendering method used in the firmware.

Run in the Linux build container with ESPHome project and qrcodegen paths.
This substitutes only the ESPHome/Matter class shell, not the render method.
Outputs an SVG containing a public development payload, never device secrets.
"""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("source", type=pathlib.Path)
parser.add_argument("qrcodegen", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
source = args.source.read_text()
method = source.split("void PairingPortal::render_qr_()", 1)[1].split("void PairingPortal::loop()", 1)[0]
prefix = r'''
#include <atomic>
#include <cstdio>
#include <string>
#include <iostream>
#include "qrcodegen.h"
#define ESP_LOGE(...) ((void)0)
struct TestMatter {
  std::string payload = "MT:Y.K9042C00KA0648G00";
  const std::string &qr_code() { return payload; }
};
class PairingPortal {
 public:
  TestMatter test_matter;
  TestMatter *matter_ = &test_matter;
  std::string svg_;
  std::atomic<bool> qr_ready_{false};
  void render_qr_();
};
void PairingPortal::render_qr_()
'''
suffix = r'''
int main() {
  PairingPortal p;
  p.render_qr_();
  if (!p.qr_ready_.load()) return 2;
  std::cout << p.svg_;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cpp = tmp / "render.cpp"
    cpp.write_text(prefix + method + suffix)
    exe = tmp / "render"
    subprocess.run(["g++", "-std=c++17", "-O1", "-I", str(args.qrcodegen),
                    str(cpp), str(args.qrcodegen / "qrcodegen.c"), "-o", str(exe)], check=True)
    svg = subprocess.check_output([str(exe)])
    assert svg.startswith(b"<svg")
    args.output.write_bytes(svg)
print("PASS: compiled actual firmware SVG renderer; public test payload rendered")
