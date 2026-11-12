# Browser Control SDK for Go

Windows x64, Go 1.22+. Install:

```
go get github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules@v0.2.1
```

Use `Connect("")` for discovery, or `Connect(endpointPath)` to select a running BrowserControl instance. Always `defer client.Close()`. `Info()`, `Tabs()` and `Call(method, params, timeoutMS)` return `json.RawMessage`; decode large integers with `json.Decoder.UseNumber` or an explicitly typed destination. Structured `*Error` values expose `Code`, `Message`, `Details`.

`Call` exposes all documented browser protocol operations. Parameters must encode to a JSON object, timeout is 1..120000 milliseconds. The native transport bounds each request/reply, preserves Unicode, and never retries commands. `result_unknown` means the action may have executed: inspect state before retrying.

The native bridge is embedded in the module and extracted to the current user's cache with SHA-256 verification. No compiler, Python, external Go module or driver installer is used. The BrowserControl Windows application is required separately. Native C ABI source is in `../Browser-Control-SDK-C-ffi`; SDK source is in `../packaging/native`. See [example](example/main.go) and [API documentation](https://pkg.go.dev/github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules).
