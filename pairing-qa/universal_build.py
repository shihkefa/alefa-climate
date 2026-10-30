from pathlib import Path
import subprocess, sys, time, os
index=sys.argv[1]
if len(sys.argv)>2:
    while not Path(f'/tmp/universal-build{sys.argv[2]}.exit').exists():
        time.sleep(5)
root=Path('/config/esphome')
log=Path(f'/tmp/universal-build{index}.log')
with log.open('w') as out:
    env=os.environ.copy()
    env['ESPHOME_DATA_DIR']='/data'
    env['ESPHOME_ESP_IDF_PREFIX']='/data/cache/idf'
    result=subprocess.run(['esphome','compile',str(root/'Universal-C6-Pairing.yaml')],env=env,stdout=out,stderr=subprocess.STDOUT)
log.with_suffix('.exit').write_text(str(result.returncode))
sys.exit(result.returncode)
