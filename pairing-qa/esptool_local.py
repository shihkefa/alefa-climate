"""Run the official project-local esptool install without changing system Python."""
from pathlib import Path
import runpy,sys
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'.flash-tools'))
runpy.run_module('esptool',run_name='__main__')
