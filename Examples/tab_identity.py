"""Switching the visible tab must not retarget a Python Tab object."""
from pathlib import Path
import sys
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "SDK" / "Python"))
from browser_control import Browser, BrowserError

url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8765/"
parsed = urlsplit(url)
if parsed.scheme != "http" or parsed.hostname not in ("127.0.0.1", "localhost", "::1"):
    raise SystemExit("Use the owned localhost fixture, not an external website.")

browser = Browser.connect()
first = second = None
try:
    first = browser.new_tab(url)
    second = browser.new_tab(url)
    first.wait(selector="#name")
    second.wait(selector="#name")
    first.fill("Tab A", selector="#name")
    second.fill("Tab B", selector="#name")
    second.activate()
    first.click(selector="#save")
    assert first.evaluate("document.querySelector('#result').textContent").startswith("Saved: Tab A")
    assert not second.evaluate("document.querySelector('#result').textContent").startswith("Saved:")
    assert [t["id"] for t in browser.tabs() if t.get("active")] == [second.id]
    print("A received its own Save click; B was unchanged and stayed active.")
    first.close()
    try:
        first.click(selector="#save")
    except BrowserError as error:
        if error.code != "tab_not_found":
            raise
        print("Closed A returned tab_not_found; no fallback to B.")
    else:
        raise AssertionError("A closed tab unexpectedly accepted the action")
finally:
    for tab in (first, second):
        if tab is not None:
            try:
                tab.close()
            except BrowserError as error:
                if error.code != "tab_not_found":
                    raise
