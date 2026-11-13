# Browser-Control-SDK Rust SDK

`cargo add browser-control-sdk`

Windows x64 MSVC Rust1.82+. Bundled DLL/import library; copy native DLL beside deployed executable.

```rust,no_run
use browser_control_sdk::Client;
let mut client=Client::new()?;
client.call("connect",serde_json::json!({}))?;
let result=client.call("info",serde_json::json!({}))?;
# Ok::<(),Box<dyn std::error::Error>>(())
```

[API](docs/API.md). Drop closes owned session; original driver and licensing requirements unchanged.

Before requesting browser information, call the `connect` operation with an empty argument object (discover an existing instance) or `{endpoint: "absolute path"}`. Construction alone does not connect or launch a browser. The `request` operation takes `{method, params, timeout}` and exposes the original browser API. The app must be installed and running separately.
