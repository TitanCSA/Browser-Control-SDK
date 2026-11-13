# Browser-Control-SDK .NET8 SDK

Package ID `BrowserControlSDK.SDK`. NuGet upload remains on hold at owner request.

```csharp
using var client=new TitanCSA.Products.BrowserControlSDK.Client();
string statusJson=client.Call("info", "{}");
```

Client implements IDisposable; Call takes raw argument JSON object and returns result JSON. NativeException exposes Code. UInt64 fields remain decimal strings. [API](docs/API.md). Windows x64 and separately installed running BrowserControl application required.

Before requesting browser information, call the `connect` operation with an empty argument object (discover an existing instance) or `{endpoint: "absolute path"}`. Construction alone does not connect or launch a browser. The `request` operation takes `{method, params, timeout}` and exposes the original browser API. The app must be installed and running separately.
