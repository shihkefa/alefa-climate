"""Embed the local-only web UI as deterministic gzip to reduce response RAM."""
from pathlib import Path
import gzip
import hashlib
import argparse

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--portal', type=Path, default=root / 'climate_c6_pairing_components/pairing_portal')
portal = parser.parse_args().portal
data = (portal / "portal.html").read_bytes()
compressed = gzip.compress(data, compresslevel=9, mtime=0)
lines = [", ".join(f"0x{value:02x}" for value in compressed[i:i+16]) for i in range(0, len(compressed), 16)]
header = ("// Generated from portal.html by pairing-qa/build_portal_ui.py.\n"
          f"// Source SHA256: {hashlib.sha256(data).hexdigest()}\n"
          "#pragma once\n#include <cstdint>\nnamespace esphome::pairing_portal {\n"
          "inline constexpr uint8_t PORTAL_HTML_GZIP[] = {\n  " + ",\n  ".join(lines) +
          "\n};\n} // namespace esphome::pairing_portal\n")
(portal / "portal_ui.h").write_text(header, encoding="utf-8")
assert gzip.decompress(compressed) == data
print(f"Portal HTML: {len(data)} bytes; gzip response: {len(compressed)} bytes")
