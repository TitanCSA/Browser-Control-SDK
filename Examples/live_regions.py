"""Capture exactly three fresh regions from the owned local fixture page."""
import base64
from itertools import islice
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "SDK" / "Python"))
from browser_control import Browser

print("Capture three live page regions. Start BrowserControl.exe and the local fixture server first.")
tab = Browser.connect().new_tab()
tab.goto(sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8765/")
for number, frame in enumerate(islice(tab.frames(interval_ms=100, clip={"x": 20, "y": 20, "width": 360, "height": 180}), 3), 1):
    Path(f"region-{number}.png").write_bytes(base64.b64decode(frame["data"]))
    print(f"Saved region-{number}.png")
