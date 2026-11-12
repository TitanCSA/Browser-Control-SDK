# Browser Control Python SDK

Install locally with `python -m pip install ./SDK/Python`. Python 3.10+ and Windows
are required for live browser calls. The client has no third-party runtime
dependencies. Start `BrowserControl.exe`, then:

```python
from browser_control import Browser
browser = Browser.connect()
tab = browser.new_tab()
tab.goto("https://example.org")
print(tab.snapshot()["text"])
tab.screenshot("page.png")
```

`Browser.launch(executable="...BrowserControl.exe")` starts an owned instance.
Endpoint capability files are private. See the repository's SDK reference for
profiles, cookies, settings, raw renderer CDP and timeout/ownership semantics.

For an AI, `tab.observe()` gives a compact page view and `tab.act([...])` runs one
finite plan of at most 20 actions, stops on the first failure and never retries.
Login status and saved-login metadata do not export passwords. Saving a login
requires human consent in the browser window; filling it requires the matching
origin/profile and the user's authorization. Existing website login challenges
still require human interaction.
