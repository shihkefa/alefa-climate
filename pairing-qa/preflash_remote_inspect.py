from pathlib import Path
import re

build=Path('/config/esphome/.esphome/build/hitachi-c6-pairing')
def excerpts(path,pattern,before=3,after=22):
    lines=path.read_text().splitlines();indexes=set()
    for i,line in enumerate(lines):
        if re.search(pattern,line):indexes.update(range(max(0,i-before),min(len(lines),i+after)))
    print(str(path))
    print('\n'.join(f'{i+1}: {lines[i]}' for i in sorted(indexes)))

excerpts(Path('/esphome/esphome/components/wifi/wifi_component.cpp'),r'bool WiFiComponent::can_proceed|void WiFiComponent::start\(',2,55)
excerpts(Path('/esphome/esphome/core/application.cpp'),r'can_proceed\(',12,22)
sdk=build/'managed_components/davidvtwout__esp_matter'
for p in sdk.rglob('*thermostat*server*.cpp'):
    excerpts(p,r'MinSetpointDeadBand|case.*SystemMode|case.*OccupiedHeatingSetpoint|case.*OccupiedCoolingSetpoint',3,14)
for p in sdk.rglob('basic_information.cpp'):
    excerpts(p,r'node_label|product_name',2,4)
