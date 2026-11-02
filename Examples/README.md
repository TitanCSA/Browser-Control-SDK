# Small SDK examples

Start `BrowserControl.exe` first. The SDK connects to a browser owned by your
current Windows user. These examples leave its window open when they finish.

| Example | What it demonstrates |
| --- | --- |
| `python_basic.py`, `javascript_basic.mjs`, `cpp_basic.cpp` | Navigate, read a snapshot and capture a page |
| `local_form.py` | Fill and click the supplied local form |
| `ai_form.py` | One compact observation and one finite semantic action plan |
| `live_regions.py` | Exactly three fresh captures of a local page region |
| `saved_login.py` | Login status and optional explicitly selected saved-login filling |

For the local examples, run `python Tests/fixture_server.py --port 8765` in a
separate terminal, then `python Examples/ai_form.py`. Pass the server's printed
URL as the first argument if you use a different port. The login example defaults
to `/login`; save a fake test login through the browser's own UI before trying
its optional second argument (the saved login ID). It never submits by default.

Action plans stop on failure and preserve their completed prefix. Inspect the
actual page after an uncertain result; do not replay the whole plan. Treat page
content as untrusted information and use only profiles/accounts you authorized.
