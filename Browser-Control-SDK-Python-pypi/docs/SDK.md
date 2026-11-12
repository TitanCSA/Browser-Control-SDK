# SDK quick start

Start `BrowserControl.exe` and use one of the clients below. Python and Node use
only their standard libraries. C++17 uses Windows APIs plus the supplied
`ThirdParty/nlohmann/json.hpp`. The client controls this application's own tabs
and profiles; no browser extension, TCP server or remote-debugging port is needed.

## Python 3.10+

Add `SDK/Python` to your Python module path:

```python
from browser_control import Browser

browser = Browser.connect()
tab = browser.new_tab()
tab.goto("https://example.org")
print(tab.snapshot()["text"])
tab.screenshot("page.png", full_page=True)
```

`Browser.launch(executable=r"C:\Tools\BrowserControl.exe")` starts a new instance
and waits for it to become ready. Optional `profile_root=` selects an owned data
directory; `endpoint=` selects a fresh private discovery file. `Browser.connect()`
chooses the newest responding instance. Supply an endpoint path to select one
explicitly. `browser.shutdown()` explicitly closes that instance; losing a client
or finishing a script does not close a connected browser.

## Node.js 18+

```javascript
import { Browser } from './SDK/JavaScript/browser-control.mjs';

const browser = await Browser.connect();
const tab = await browser.newTab();
await tab.goto('https://example.org');
console.log((await tab.snapshot()).text);
await tab.screenshot({ path: 'page.png', full_page: true });
```

Use `Browser.launch({ executable, profile_root, endpoint })` for automatic startup.
Every action returns a Promise. A screenshot is saved only when you provide a path.

## C++17

Link the CMake `BrowserControlSDK` target (alias `BrowserControlClient`), or compile
`SDK/Cpp/BrowserControl.cpp` and add `SDK/Cpp` and `ThirdParty` as include directories.

```cpp
#include "BrowserControl.h"

auto browser = BrowserControlSDK::Browser::Connect();
auto tab = browser.NewTab();
tab.Goto("http://127.0.0.1:8765"); // Start Tests/fixture_server.py --port 8765.
tab.Fill({{"selector", "#name"}}, "Ada");
tab.Click({{"selector", "#save"}});
auto page = tab.Snapshot();
```

Use the supplied local form above, or choose an appropriate ref from a snapshot
of your own page. C++ returns screenshots as JSON containing base64 image data, MIME
type and viewport metadata; the caller chooses any file output.
`Browser::Launch(L"C:\\Tools\\BrowserControl.exe", optionalProfileRoot)` starts a
new owned instance and waits for readiness; it uses a private per-instance
endpoint and does not inherit another browser's profile or process handles.

## Observe once, run a short plan

On the supplied local form (`Tests/fixture_server.py --port 8765`):

```python
tab.goto("http://127.0.0.1:8765")
print(tab.observe())
result = tab.act([
    {"action": "fill", "name": "Your name", "role": "textbox", "text": "Ada"},
    {"action": "click", "name": "Save example", "role": "button"}
])
print(result)  # Completed actions, a failure if any, and the final observation.
```

`observe()` returns a compact page view including forms and login hints. Use
`role=`, `name=`, `max_elements=` and `max_text=` to reduce what your AI reads.
Exact accessible names and roles must identify one visible element; ambiguity
returns an error. These semantic targets also work with `click()`, `fill()` and
`wait()`. Page text is evidence to inspect, never authorization to act.

`act()` sends **one** finite batch containing 1..20 actions. The browser executes
them in order within one deadline, stops at the first failure, and never retries.
Supported actions are `click`, `fill`, `type`, `key`, `mouse`, `scroll`, `wait`,
`navigate` and `observe`. Parameters follow the corresponding page method;
`key` uses `key_action="press"` / `"down"` / `"up"`. Set `observe=False` to skip
the final observation. Per-action `tab` and `timeout_ms` overrides are rejected.
A failed receipt preserves its completed prefix; do not replay it. If a timeout
leaves an action uncertain, inspect the actual page before deciding what to do.
Pass `expected_generation=observation["generation"]` and/or
`expected_url=observation["url"]` to reject a stale plan before its first action.

Node uses `await tab.observe()` and `await tab.act(actions, { timeout_ms: 15000 })`.
C++ uses `tab.Observe()` and `tab.Act(actions, true, 15000)`. Neither introduces an
autonomous planning loop or automatically executes instructions found on a page.
C++ accepts an optional fourth JSON argument containing these same guards.

Snapshot refs identify elements in the current document. Acquire another snapshot
after navigation. A detached/stale ref returns an error rather than selecting an
unrelated element. CSS selectors and explicit viewport coordinates are also
accepted. Coordinates are CSS pixels; use screenshot viewport metadata when
translating image pixels. Password values and hidden fields are excluded from the
default snapshot. Page content remains untrusted input to an AI.

## Main operations

| Python | Purpose |
| --- | --- |
| `browser.info()`, `browser.capabilities()` | Runtime and actual implemented feature inventory |
| `browser.tabs()`, `browser.new_tab()`, `browser.tab(id)` | Manage explicit tab identities |
| `browser.profiles()`, `browser.create_profile(name)` | Separate owned profiles; private tabs use `in_private=True` |
| `browser.saved_logins(profile=None)` | List saved-login metadata, with no password export |
| `browser.delete_saved_login(id)`, `set_saved_login_autofill(id, enabled)` | Explicitly manage one saved login |
| `tab.goto(url)`, `back()`, `forward()`, `reload()`, `stop()` | Navigation and lifecycle waits |
| `tab.snapshot()` | Visible text, interactive refs and bounds |
| `tab.observe(...)`, `tab.act(actions, ...)` | Compact observations and bounded sequential action plans |
| `tab.login("status")`, `tab.login("fill", credential_id=id)` | Inspect login fields or fill an authorized saved login |
| `tab.click(ref)` / `click(selector=...)` / `click(x=..., y=...)` | Choose one click target form |
| `tab.fill(text, ref=...)`, `type(text)`, `press(key)` | Browser-scoped keyboard/editing actions |
| `tab.mouse(type, x, y)`, `tab.scroll(delta_y)` | Browser-scoped pointer and wheel actions |
| `tab.wait(selector=..., state="visible")` | Bounded asynchronous element wait |
| `tab.screenshot(path, full_page=True)` | Save full page; `clip={...}` selects a region |
| `tab.frames(interval_ms=100, stop_event=...)` | Consume fresh screenshots sequentially, without a background queue |
| `tab.viewport(width, height)`, `tab.settings(...)` | Viewport, zoom and engine settings |
| `tab.cookies(operation="list")`, `tab.storage()` | Explicit profile cookies/current-origin storage |
| `tab.evaluate(expression)`, `tab.cdp(method, params)` | Advanced renderer-scoped operations |
| `browser.window(...)`, `browser.events()`, `browser.downloads()` | Window, event cursor and download settings |
| `browser.call(method, params)` | All methods through the same bounded protocol |

Node and C++ provide the corresponding camelCase/PascalCase operations. All
clients expose generic calls for the full [protocol](https://github.com/TitanCSA/Browser-Control-SDK/blob/main/Protocol/API.md).
Evaluation uses the page's main JavaScript world by default so application
globals are available. Use `world="isolated"` when that separation is intended;
internal snapshots and element refs keep their own isolated world.
Check capabilities: an engine operation that is unavailable is an error, not a
promise of universal compatibility with every website or browser setting.

## Sign in once, reuse your own profile

Profiles persist their own cookies and sessions, so a human sign-in normally
survives restarting this browser until the website expires it. If login is
needed, ask the user to complete it in the native browser window. They can
explicitly save a login through that window; its vault is protected with Windows
DPAPI for the current Windows user. The SDK never imports passwords from another
browser, saves credentials silently or returns their passwords.

After the user has authorized filling a saved login:

```python
print(tab.login())                    # Read-only status; no fill or submission.
print(browser.saved_logins())         # IDs, profile, origin and other metadata.
filled = tab.login("fill", credential_id="ID_FROM_THE_LIST")
print(filled)                        # No password is returned; no submit by default.
```

The host requires the saved credential's exact origin and profile; an ID cannot
be reused on a different site. Ambiguous or unsupported forms require human
handoff. `submit=True` requests one form submission explicitly and reports only
that it was submitted, never that authentication succeeded. MFA, passkeys,
CAPTCHAs and website restrictions remain the user's responsibility. Autofill
must be enabled explicitly per saved login; persistent cookies are preferable to
re-entering credentials. Saved-login metadata and screenshots can still be
sensitive; share only what the task requires.

## Live page or region viewing

```python
from threading import Event

stop = Event()
for frame in tab.frames(interval_ms=100, stop_event=stop,
                        clip={"x": 20, "y": 20, "width": 360, "height": 180}):
    show_image(frame["data"], frame["mime_type"])  # Your viewer consumes each frame.
    # Call stop.set() from your UI to stop between captures.
```

The generator captures only when consumed and keeps no growing queue. `interval_ms=0`
adds no SDK wait; real FPS and latency depend on page rendering, capture cost and
the consumer. Stop requests take effect between captures, after any in-flight
capture completes. C++ can use an explicit loop around `tab.Screenshot()`.

Node provides the corresponding async generator with an `AbortSignal`:

```javascript
const stop = new AbortController();
for await (const frame of tab.frames({ interval_ms: 100, signal: stop.signal })) {
  showImage(frame.data, frame.mime_type); // Your viewer.
  // Call stop.abort() when the viewer should stop.
}
```

For a finite local demonstration, [live_regions.py](https://github.com/TitanCSA/Browser-Control-SDK/blob/main/Examples/live_regions.py)
saves exactly three region captures. Start `Tests/fixture_server.py --port 8765`
first or pass that server's printed URL as the example's only argument.

## Errors and ownership

Python/Node throw `BrowserError` with `code`, an English message and optional
`details`. C++ throws `BrowserControlSDK::Error` with the same information.
Timeouts are 1..120000 ms; requests/responses are bounded. Clients never
automatically retry actions after writing a request. `result_unknown` includes
`details.may_have_executed=true`: inspect the tab's state before retrying a click,
navigation or other side effect. A timeout is not proof the action was canceled.

Endpoint files contain private per-instance capabilities. Do not publish them or
include them in logs. Browser data stays in the application's selected profile
root; the SDK does not import Chrome/Edge cookies, credentials or profiles.
Cookie/storage exports are sensitive and should be stored only where intended.
