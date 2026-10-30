"""Check current custom C++ using the real build flags without modifying active build outputs."""
from pathlib import Path
import json,shlex,subprocess,sys
b=Path('/config/esphome/.esphome/build/hitachi-c6-pairing')
commands=json.loads((b/'build/compile_commands.json').read_text())
entry=next(c for c in commands if c['file'].endswith('/appliance_auto/appliance_auto.cpp'))
argv=shlex.split(entry['command']); filtered=[]; skip=False
for arg in argv:
    if skip:skip=False;continue
    if arg in ('-o','-MF','-MT','-MQ'):skip=True;continue
    if arg in ('-c','-MD','-MMD','-MP'):continue
    if arg==entry['file']:continue
    filtered.append(arg)
source=Path('/config/esphome/universal_c6_components/appliance_auto/appliance_auto.cpp')
result=subprocess.run(filtered+['-fsyntax-only',str(source)],cwd=entry['directory'],capture_output=True,text=True)
Path('/tmp/universal-syntax.log').write_text(result.stdout+result.stderr)
if result.returncode:
    lines=(result.stdout+result.stderr).splitlines()
    keep=set()
    for i,line in enumerate(lines):
        if 'error:' in line or 'static assertion' in line:
            keep.update(range(max(0,i-2),min(len(lines),i+5)))
    print('\n'.join(lines[i] for i in sorted(keep)) if keep else '\n'.join(lines[-25:]))
    sys.exit(result.returncode)
print('PASS: current appliance controller compiles against actual ESPHome/ESP-IDF/Matter headers')
