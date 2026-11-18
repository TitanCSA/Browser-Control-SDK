# Changes

## 0.2.1 — 18 November 2026

- Restore a minimized window with `window.set(state="normal")` without validating
  the small iconic rectangle as if it were the normal window size. State-only
  changes preserve the normal placement.
- Validate a requested window state before moving, resizing or changing visibility.
- Return `window_minimized` promptly for screenshot requests instead of waiting
  for a Chromium capture deadline. Restore explicitly before capturing again.
- Add regression checks for restore/capture recovery and invalid-state immutability.
  The real localhost browser suite passes 22 cases; native IPC/vault checks and
  all 26 offline SDK/MCP tests also pass.

The SDK transport and existing application methods remain compatible. The client
package does not install the BrowserControl application. Existing PyPI/npm and
other registry versions retain their own package versions.

Minimized-window pointer input is not qualified: two separate repeated-input
trials stopped after 104 and 108 successful cycles. Keep the window restored,
check the application's postcondition and do not repeat a submission solely
because an immediate read was early. These fixes are not a guarantee that every
website or future WebView2 version works.
