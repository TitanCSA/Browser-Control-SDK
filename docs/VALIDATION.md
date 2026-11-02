# Validation

Version **0.1.0**, checked on Windows 10 x64 build 19045 with WebView2 Runtime
**156.0.4314.58**, Visual Studio 2022, Python 3.10.11 and Node 24.21.0. All website interaction in
these checks used the supplied, owned localhost fixture. No external account,
website login or real credential was used.

## Checked behavior

| Suite | Result | Scope |
| --- | --- | --- |
| Native IPC | 8 passed | Framing, authentication, version/timeout bounds, JSON limits, restricted-token refusal, queued cancellation and shutdown/endpoint cleanup |
| Offline SDK / MCP | 17 passed | Requests, response identities, errors, timeouts, discovery, lazy startup and MCP tool responses |
| Actual browser integration | 20 passed | Forms, semantic refs, keyboard/mouse events, async DOM waits, screenshots/live regions, layout, cookies/storage, profile isolation, downloads, navigation, CDP, external-protocol blocking and concurrent clients |
| Other live clients | Node, C++ and MCP passed | Real local form fill/click, redacted snapshot and PNG response; Node live frames and MCP image content |
| Concurrent workload | 1,200 requests, 0 errors | Four clients: 400 browser-info requests, 400 renderer evaluations and 400 semantic snapshots |

The integration run checks actual outcomes: submitted form contents, keyboard and
pointer events, image dimensions and changing frame bytes, cookie separation,
downloaded bytes, stale-ref refusal and cleanup. Case-sensitive profile names are
tested on Windows, including `Work` and `work` as separate sessions.

A small 30-sample local renderer roundtrip measured **0.339 ms median / 0.516 ms
p95** on this machine. This includes the SDK pipe, UI dispatch and a trivial
JavaScript evaluation. It excludes network loading, website work and screenshot
encoding, and is not a guaranteed latency or FPS result. The concurrent mixed
workload took 0.461 seconds, with 1.497 ms median / 2.878 ms p95 per request.

## Reproduce

Run the build and offline/native checks:

```powershell
.\Build.ps1 -Test
```

Then explicitly open the actual browser against its own fixture:

```powershell
python Tests/integration.py --exe obj/Build/bin/Release/BrowserControl.exe
```

The integration script starts a temporary localhost server and isolated browser
data folder, then shuts both processes down. Its external-protocol test briefly
registers a unique HKCU test scheme with a nonexistent handler and removes it in
`finally`; no external application is launched. Reports and screenshots are
written under ignored `obj/Validation`, never committed with runtime profiles.

The GitHub workflow builds Windows x64 and runs the native/offline suites. It does
not automatically open websites or run the interactive integration suite.

## Boundaries

These checks establish the tested behavior, not freedom from every bug or
compatibility with every site, Windows configuration or future engine version.
The initial executable was tested on Windows 10; Windows 11 deployment uses the
same x64 WebView2 APIs but has not received a separate complete device-matrix run.
Keep the Evergreen Runtime updated and inspect `browser.capabilities()` for the
actual runtime's supported features. Snapshots currently cover the main frame.

Use the normal login flow of websites that permit embedded-browser automation.
The app does not solve CAPTCHA, bypass access controls or promise arbitrary
browser extensions. See [security and data handling](SECURITY.md).
