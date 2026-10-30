"""Rasterize the firmware's integer-square SVG, then independently decode it."""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / ".qa-python"))
import zxingcpp

path = pathlib.Path(sys.argv[1])
svg = ET.fromstring(path.read_text())
_, _, width, height = map(int, svg.attrib["viewBox"].split())
assert width == height
scale = 10
image = Image.new("RGB", (width * scale, height * scale), "white")
draw = ImageDraw.Draw(image)
data = svg.find("{http://www.w3.org/2000/svg}path").attrib["d"]
modules = re.findall(r"M(\d+) (\d+)h1v1h-1z", data)
assert modules
assert "".join(f"M{x} {y}h1v1h-1z" for x, y in modules) == data
for x, y in modules:
    x, y = int(x), int(y)
    assert 4 <= x < width - 4 and 4 <= y < height - 4
    draw.rectangle((x * scale, y * scale, (x + 1) * scale - 1, (y + 1) * scale - 1), fill="black")
decoded = zxingcpp.read_barcode(image)
assert decoded is not None
assert decoded.text == "MT:Y.K9042C00KA0648G00", decoded.text
image.save(path.with_suffix(".png"))
print(f"PASS: firmware SVG decoded to the exact public Matter payload; {width-8} modules, 4-module quiet zone")
