# Browser Control SDK for Python

Requires Python 3.10+, Windows and a separately installed BrowserControl application.

```sh
python -m pip install browser-control-sdk
```

```python
from browser_control import Browser
browser = Browser.connect()
tab = browser.new_tab()
tab.goto("https://example.org")
print(tab.snapshot()["text"])
```

Start BrowserControl.exe before connecting, or use
`Browser.launch(executable=r"C:\Tools\BrowserControl.exe")`.
This client controls the application's own tabs and profiles.

The wheel and source distribution include `docs/SDK.md` and `examples/browser.py`.

[Documentation](https://github.com/TitanCSA/Browser-Control-SDK/tree/main/Browser-Control-SDK-Python-pypi) | [Downloads](https://github.com/TitanCSA/Browser-Control-SDK/releases)
