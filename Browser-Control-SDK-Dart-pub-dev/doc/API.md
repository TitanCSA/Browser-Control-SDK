# Native protocol API

`Browser.connect({String? endpoint})` returns `Future<Browser>`. `close()` is idempotent and must be called after use. `callJson(String method, {Map<String, Object?> parameters = const {}, int timeoutMs = 15000})` returns JSON text. Methods `infoJson()` and `tabsJson()` return JSON text too.

Timeout range: 1..120000 milliseconds. Native transport limits: 1 MiB request, 64 MiB response. Native and remote errors throw `BrowserControlError` with `code`, `message`, `details`. `result_unknown` means an action may have executed. No automatic retries occur; inspect state before repeating an action.

Native UTF-8 buffers are explicitly released through their owning allocator. The DLL is bundled under `lib/native` and located using the Dart package resolver. C ABI source: `Browser-Control-SDK-C-ffi` in the repository. Full C++ SDK source: `packaging/native`.

Unsigned 64-bit JSON integers are preserved in returned JSON text. Dart's default JSON decoder can turn very large numbers into floating point, so choose a lossless parser for such responses. Each client belongs to its Dart isolate. All calls block that isolate until a bounded native request completes.
