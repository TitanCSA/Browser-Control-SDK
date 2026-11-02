# Local control, profiles and data

The native app runs as the current Windows user. Its automation endpoint is a
local named pipe with a protected current-user/SYSTEM ACL, a medium-integrity
label, remote clients refused and a fresh 256-bit instance token. It opens no
TCP control port and exposes no host command bridge to website JavaScript.

Endpoint JSON files live by default under
`%LOCALAPPDATA%\TitanCSA\BrowserControl\instances`. They are private capability
files, not configuration to publish or commit. The token is sent only inside the
private pipe; the app does not print it or put it in command-line arguments.
Explicit endpoint/profile folders must be local and privately writable. Existing
shared directories are rejected rather than having their permissions silently
rewritten. The browser never imports another application's default profile.
An OS file lock prevents two browser instances from concurrently writing one
profile data root. Separate instances can use separate private data folders.

An authorized same-user client can navigate, evaluate scripts and explicitly
read this browser profile's cookies and storage. Protect that user's session and
scripts accordingly. This channel is not a boundary against an administrator or
malicious code already running as the same Windows user. The renderer's ordinary
Chromium sandbox and web security settings remain enabled.

Requests are length-prefixed UTF-8 JSON, bounded to 1 MiB and limited nesting;
responses are bounded to 64 MiB. Workers and outstanding operations are bounded.
Shutdown cancels pipe IO and releases waiting workers. A queued operation that
times out before dispatch is skipped. An already-started browser operation may
have effects despite a timeout: callers must inspect the `may_have_executed`
detail or `result_unknown` before choosing whether to retry. Mutating actions are
not silently retried by the clients.

Snapshot helpers execute in an isolated JavaScript world. The snapshot identifies
visible interactive elements, redacts password input values and omits hidden
fields. It does not certify a page's honesty: labels, URLs and visible content
come from an untrusted website. Use explicit user instructions to decide actions.
Public page evaluation runs in the page's main world by default; choose the
isolated world explicitly when appropriate. A screenshot or explicit cookie/
storage operation can contain private data even if a text snapshot is redacted.

Downloads are disabled until configured. An enabled download is saved, never
automatically executed. Keep output directories private and review downloaded
files normally. SDK screenshot helpers write only where the calling script
explicitly asks them to. Profiles retain their own ordinary browser data until
you choose to remove it; closing a tab is not secure deletion. In-private contexts
use the runtime's private profile mode, with no claim of memory-forensic secrecy.

Saved passwords use an additional current-user DPAPI vault in the private data
root. Saving requires explicit native UI confirmation. Fills match an exact
scheme/host/port and profile; private profiles cannot use the vault. List/status
operations return metadata and login helper errors are sanitized. The SDK/MCP
has no password-save or password-export endpoint. Ordinary observations redact
passwords, username-autocomplete fields and text fields in password forms.

This protection is for data at rest: an authorized client can deliberately
evaluate a page, and a website receives the password when its form is filled.
Same-user malicious code and administrators remain outside the boundary.
Temporary in-process copies are cleared where practical, without a forensic
memory-secrecy claim. [Session handling and limits](SESSIONS.md).

Action plans are bounded and stop at the first error. Confirmed partial receipts
are published to the transport without adding a timeout grace period. The host
uses the original absolute deadline, including UI queue time. An in-flight step
can still have effects; inspect its uncertainty before deciding what to retry.

The app provides general browser controls, not hidden-account login, challenge
solving, block evasion or unrestricted operating-system automation. Third-party
authentication restrictions can prevent an embedded browser from completing a
site's login flow. For websites that permit automation, use authorized accounts,
their supported login flow and applicable platform approvals.

Dependency versions and SHA-256 pins are recorded in `ThirdParty/Dependencies.json`
and CMake. The released app is not a kernel driver and its executable is not
Microsoft-signed. Security updates for the engine are supplied through Microsoft's
WebView2 Evergreen Runtime.

Engine guidance: [Microsoft's WebView2 security practices](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security)
and [Evergreen runtime deployment](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/evergreen-vs-fixed-version).
