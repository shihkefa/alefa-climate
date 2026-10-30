"""Audit the generated SDK configuration, without printing credentials."""
import argparse
import csv
import pathlib

p = argparse.ArgumentParser()
p.add_argument("build_root", type=pathlib.Path)
args = p.parse_args()
sdk = (args.build_root / "sdkconfig.alefa-climate").read_text().splitlines()
values = dict(line.split("=", 1) for line in sdk if line.startswith("CONFIG_") and "=" in line)
enabled = (
    "CONFIG_ESPTOOLPY_FLASHSIZE_4MB", "CONFIG_ENABLE_MATTER_OVER_THREAD",
    "CONFIG_ESP_MATTER_ENABLE_OPENTHREAD", "CONFIG_OPENTHREAD_ENABLED",
    "CONFIG_OPENTHREAD_RADIO_NATIVE", "CONFIG_OPENTHREAD_SRP_CLIENT",
    "CONFIG_ENABLE_CHIPOBLE", "CONFIG_BT_ENABLED", "CONFIG_BT_NIMBLE_ENABLED",
    "CONFIG_ESP_COEX_SW_COEXIST_ENABLE", "CONFIG_MBEDTLS_ECDSA_DETERMINISTIC",
)
disabled = (
    "CONFIG_ENABLE_WIFI_STATION", "CONFIG_ENABLE_WIFI_AP", "CONFIG_BT_BLUEDROID_ENABLED",
    "CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING", "CONFIG_USE_MINIMAL_MDNS",
    "CONFIG_ENABLE_ETHERNET_TELEMETRY",
)
for name in enabled:
    assert values.get(name) == "y", (name, values.get(name))
for name in disabled:
    assert values.get(name, "n") == "n", (name, values.get(name))
defines = (args.build_root / "src/esphome/core/defines.h").read_text()
for flag in ("USE_API", "USE_API_PLAINTEXT", "USE_WIFI", "USE_WIFI_AP", "USE_MDNS", "USE_MATTER_BLE_THREAD", "USE_WEBSERVER_AUTH"):
    assert f"#define {flag}" in defines, flag
assert "#define USE_OPENTHREAD\n" not in defines, "Two owners of the Thread stack"
assert "USE_OPENTHREAD_TLVS" not in defines, "Unexpected fixed Thread dataset"
assert "#define USE_OTA" not in defines, "OTA enabled in a single-app layout"
assert "#define USE_API_NOISE" not in defines, "API encryption must be disabled for keyless HA setup"
rows = list(csv.reader(line for line in (args.build_root / "partitions.csv").read_text().splitlines()
                       if line.strip() and not line.lstrip().startswith("#")))
rows = [[value.strip() for value in row] for row in rows]
apps = [row for row in rows if row[1] == "app"]
assert len(apps) == 1 and apps[0][2] == "factory", apps
assert int(apps[0][3], 0) == 0x10000 and int(apps[0][4], 0) == 0x380000, apps
for row in rows:
    assert int(row[3], 0) + int(row[4], 0) <= 0x400000, row
print("PASS: BLE + native Thread + radio coexistence enabled; ESPHome Native API uses plaintext without a key")
print("PASS: no hardcoded Thread dataset; QR portal requires authentication")
print("PASS: 4 MB flash, single 3.5 MB factory application, USB updates only")
