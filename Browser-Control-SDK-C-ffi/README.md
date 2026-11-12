# Browser Control C ABI

Windows x64 bridge to the original C++ SDK. It connects directly to the application's private named pipe. It exposes every protocol method through `bcs_call`, with UTF-8 JSON parameters and responses.

Build: `cmake -S . -B build -A x64`, then `cmake --build build --config Release`.

See `BrowserControlBridge.h` for ownership and error conventions. Handles are validated and thread-safe; closing a handle allows an already running call to complete. Free every returned string with `bcs_free`. No exception crosses the C ABI. Requests are never retried automatically: `result_unknown` means the action may have executed. The BrowserControl Windows application must already be running. No installer or driver is included.
