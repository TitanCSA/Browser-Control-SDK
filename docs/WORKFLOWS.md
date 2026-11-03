# Sessions, human checkpoints and explicit tabs

## Sign in once, reconnect later

1. Open the intended persistent profile and sign in normally on an authorized site.
2. Leave or reopen that same browser/profile, then call `Browser.connect()`.
3. Use `browser.tabs()` to choose the intended tab by ID, or open a new tab with
   `browser.new_tab(url, profile="work")` using an existing profile name.
4. Observe the page before continuing. Sites can expire/revoke a session or ask
   for MFA again. Session cookies are not necessarily persistent cookies.

This browser retains its own profile data; it does not attach to Chrome/Edge.
See [sessions and saved logins](SESSIONS.md) for vault details and boundaries.

## The same human checkpoint in three SDKs

The matching `Examples/human_checkpoint.py`, `.mjs` and `.cpp` examples use one
explicit tab ID, show an observation, wait for the exact word `continue`, and
observe that same tab again. Cancellation or EOF stops the example. They neither
submit a form nor automatically decide that authentication succeeded.

```powershell
python Examples/human_checkpoint.py TAB_ID
node Examples/human_checkpoint.mjs TAB_ID
obj/Build/bin/Release/BrowserCheckpointExample.exe TAB_ID
```

Build the C++ example with `Build.ps1`. Obtain `TAB_ID` from `browser.tabs()`
(Python/JavaScript) or `Browser::Tabs()` (C++). The default connection discovers
the newest responding instance; select an explicit endpoint in your application
when several instances are running.

Use only an authorized account/profile. Finish verification manually. After the
checkpoint, review the fresh observation and expected URL/page state before
selecting another action. Manual navigation can invalidate old element refs.
Closed tabs report an error instead of selecting another tab.

This is a checkpoint pattern, not a detector for every verification screen. Your
application decides where to pause and what permits continuation. Observations
and screenshots of real accounts can be sensitive; do not publish them in logs.

## Switch visible tabs without retargeting the script

Start `python Tests/fixture_server.py --port 8765`, then run:

```powershell
python Examples/tab_identity.py http://127.0.0.1:8765/
```

This example creates two local fixture tabs, selects B in the UI and clicks Save
through A's existing `Tab` object. It checks that A changed, B did not and B stayed
active. It closes A and expects `tab_not_found` from another call on A. It cleans
up only its own tabs. No external account or website is used.

MCP calls with omitted `tab` select the active tab at the start of each call.
Carry the ID from `browser_observe` into subsequent calls and use its URL/generation
guards when appropriate. A batch keeps the ID but does not prevent manual navigation.
[Multiple profiles and instances](../MCP/README.md#profiles-and-instances).

## Uncertain submissions

A failed Submit does not prove nothing was submitted. Inspect the actual outcome
and `act()`'s completed prefix before deciding how to continue. Do not replay a
whole batch that already performed some writes. The supplied examples do not
automatically retry writes or bypass a site's access restrictions.

## Validation of these examples

On 2026-11-03, the C++ target compiled with the repository build configuration.
Python, JavaScript and C++ checkpoints were run against the owned localhost
fixture with confirmation, EOF, cancellation and a closed tab. They observed
without submitting the fixture form. The two-tab identity example passed.
These are local workflow checks, not a real-site MFA test or a compatibility
guarantee for external websites.
