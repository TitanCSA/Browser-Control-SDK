# Ruby API

`BrowserControlSDK::Browser.connect(endpoint: nil) { |browser| ... }` connects and confirms the running instance. The block form always closes the client. `close` is idempotent. Windows x64 is required.

`call(method, params = {}, timeout_ms: 15000)` returns parsed JSON. Parameters must be a Hash, timeout an integer from 1 to 120000 milliseconds. Each client serializes calls. Convenience methods: `info`, `tabs`, `new_tab(options = {})`, `navigate(tab_id, url, timeout_ms: 15000)`.

`BrowserControlSDK::Error` exposes `code` and `details`. A `result_unknown` error means the action may have executed; inspect state before retrying. No request is automatically retried. Transport request/response bounds and timeouts are those of the original C++ SDK (1 MiB request and 64 MiB reply). Ruby Integer preserves JSON integers without converting them to floating point.

The bridge source and C ABI are in the repository's `Browser-Control-SDK-C-ffi` directory. Native handles and returned buffers are released by the wrapper. BrowserControl itself is distributed separately; the gem never downloads or launches it.
