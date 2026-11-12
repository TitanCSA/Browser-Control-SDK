# Browser Control SDK for Dart and Flutter

`dart pub add browser_control_sdk` or `flutter pub add browser_control_sdk`

Requires Windows x64, Dart 3.4+, and a running BrowserControl application.

```dart
import 'dart:convert';
import 'package:browser_control_sdk/browser_control_sdk.dart';
Future<void> main() async {
  final browser = await Browser.connect();
  try { print(jsonDecode(browser.infoJson())); }
  finally { browser.close(); }
}
```

`Browser.connect(endpoint: 'C:/path/private-endpoint.json')` selects an instance. Without a path, the native SDK discovers an instance for the current user. Endpoint files contain a private token; keep them private.

`callJson(method, parameters: {...}, timeoutMs: 15000)` exposes every protocol operation and returns JSON text. Keeping replies as text preserves unsigned 64-bit values exactly; use a suitable lossless parser when handling integers beyond Dart's signed 64-bit range. Regular `jsonDecode` may round such values. `infoJson`, `tabsJson` are conveniences.

Use `callRaw(method, parametersJson, timeoutMs: 15000)` when request parameters contain unsigned 64-bit JSON integers beyond Dart's signed range. The original JSON text is transmitted without numeric conversion.

The included native bridge talks directly to Windows named pipes. No Python, extra Dart dependencies, driver installer, or app launcher is used. Native calls are synchronous: use a worker isolate to keep a Flutter UI responsive. See [API guide](doc/API.md) and [example](example/main.dart).

For a deployed Flutter Windows app or compiled Dart executable, bundle `lib/native/BrowserControlBridge.dll` next to the executable. This is a plain Dart FFI package: it does not automatically modify your Flutter build. The `windows/CMakeLists.txt` file provides the DLL list for explicit CMake integration. Development with `dart run` resolves the bundled DLL from the package automatically.
