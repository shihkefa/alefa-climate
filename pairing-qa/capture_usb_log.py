"""Capture native USB logs; optional explicit reset to capture startup."""
from pathlib import Path
import argparse
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / '.flash-tools'))
import serial

parser = argparse.ArgumentParser()
parser.add_argument('port')
parser.add_argument('--seconds', type=float, default=20)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--reset', action='store_true', help='Reset using Espressif native USB sequence before reading')
args = parser.parse_args()

port = serial.Serial(port=None, baudrate=115200, timeout=0.25,
                     write_timeout=1, rtscts=False, dsrdtr=False)
port.dtr = False
port.rts = False
port.port = args.port
chunks = []
error = None
try:
    port.open()
    print(f'Opened {args.port}; capturing for {args.seconds:g}s; no serial writes.', flush=True)
    if args.reset:
        from esp_pylib.serial_reset import hard_reset
        print('Resetting via Espressif USB RTS sequence to capture startup.', flush=True)
        hard_reset(port, hold_delay=0.2, post_release_delay=0.2)
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline and sum(map(len, chunks)) < 512 * 1024:
        data = port.read(min(max(port.in_waiting, 1), 8192))
        if data:
            chunks.append(data)
except serial.SerialException as exc:
    error = str(exc)
finally:
    if port.is_open:
        port.close()

raw = b''.join(chunks)
log = raw.decode('utf-8', errors='replace')
log = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', log)
# Do not print commissioning material or network passwords into the chat.
secret_line = re.compile(r'password|passcode|\bpsk\b|manual.?pairing|setup.?code|MT:', re.I)
log = '\n'.join('[credential line omitted]' if secret_line.search(line) else line
                for line in log.splitlines())
args.output.write_text(log, encoding='utf-8')
print(f'Captured {len(raw)} bytes. Log: {args.output}')
print(log[-24000:])
if error:
    print(f'SERIAL ERROR: {error}')
    sys.exit(1)
