"""Inspect a local login; optionally fill one saved login explicitly by its ID."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "SDK" / "Python"))
from browser_control import Browser

print("Inspect the local login form. Save a login in the browser UI first; optional second argument fills its ID without submitting.")
browser = Browser.connect()
tab = browser.new_tab()
tab.goto(sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8765/login")
print(tab.login())
print(browser.saved_logins())
if len(sys.argv) > 2:
    print(tab.login("fill", credential_id=sys.argv[2]))
