"""One compact observation, then one finite plan on the owned local fixture."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "SDK" / "Python"))
from browser_control import Browser

print("Observe the local form, fill Your name, then click Save example in one finite plan.")
tab = Browser.connect().new_tab()
tab.goto(sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8765")
print(tab.observe(max_text=1200))
print(tab.act([
    {"action": "fill", "name": "Your name", "role": "textbox", "text": "Ada"},
    {"action": "click", "name": "Save example", "role": "button"}
]))
