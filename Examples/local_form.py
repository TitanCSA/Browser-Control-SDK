"""Start Tests/fixture_server.py, then fill the owned local demo form."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "SDK" / "Python"))
from browser_control import Browser

print("Fill and submit the local demonstration form, then save its page. Start the fixture server first.")
browser = Browser.connect()
tab = browser.new_tab()
tab.goto(sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8765/")
tab.fill("Ada", selector="#name")
tab.click(selector="#save")
print(tab.snapshot()["text"])
tab.screenshot("local-form.png", full_page=True)
