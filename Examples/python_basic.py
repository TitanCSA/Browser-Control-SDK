from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "SDK" / "Python"))
from browser_control import Browser

print("Open example.org, read its visible page, and save a screenshot. Start BrowserControl.exe first.")
browser = Browser.connect()
tab = browser.new_tab()
tab.goto("https://example.org")
print(tab.snapshot()["text"])
tab.screenshot("page.png")
