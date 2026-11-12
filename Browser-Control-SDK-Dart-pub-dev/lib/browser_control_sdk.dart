/// Direct native BrowserControl SDK for Windows x64.
library;

import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

typedef _ConnectN = Uint64 Function(Pointer<Uint8>, Pointer<Pointer<Uint8>>);
typedef _ConnectD = int Function(Pointer<Uint8>, Pointer<Pointer<Uint8>>);
typedef _CallN = Int32 Function(
    Uint64, Pointer<Uint8>, Pointer<Uint8>, Uint32, Pointer<Pointer<Uint8>>);
typedef _CallD = int Function(
    int, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Pointer<Uint8>>);
typedef _CloseN = Void Function(Uint64);
typedef _CloseD = void Function(int);
typedef _FreeN = Void Function(Pointer<Void>);
typedef _FreeD = void Function(Pointer<Void>);
typedef _HeapN = Pointer<Void> Function();
typedef _HeapD = Pointer<Void> Function();
typedef _AllocN = Pointer<Void> Function(Pointer<Void>, Uint32, IntPtr);
typedef _AllocD = Pointer<Void> Function(Pointer<Void>, int, int);
typedef _ReleaseN = Int32 Function(Pointer<Void>, Uint32, Pointer<Void>);
typedef _ReleaseD = int Function(Pointer<Void>, int, Pointer<Void>);

/// A structured native or remote protocol error.
class BrowserControlError implements Exception {
  final String code;
  final String message;
  final Object? details;
  BrowserControlError(this.code, this.message, [this.details]);
  @override
  String toString() => '$code: $message';
}

class _Native {
  final DynamicLibrary library;
  late final _ConnectD connect =
      library.lookupFunction<_ConnectN, _ConnectD>('bcs_connect');
  late final _CallD call = library.lookupFunction<_CallN, _CallD>('bcs_call');
  late final _CloseD close =
      library.lookupFunction<_CloseN, _CloseD>('bcs_close');
  late final _FreeD free = library.lookupFunction<_FreeN, _FreeD>('bcs_free');
  final DynamicLibrary kernel = DynamicLibrary.open('kernel32.dll');
  late final _HeapD heap =
      kernel.lookupFunction<_HeapN, _HeapD>('GetProcessHeap');
  late final _AllocD heapAlloc =
      kernel.lookupFunction<_AllocN, _AllocD>('HeapAlloc');
  late final _ReleaseD heapFree =
      kernel.lookupFunction<_ReleaseN, _ReleaseD>('HeapFree');
  _Native(this.library);
  Pointer<Uint8> allocate(int size) {
    final ptr = heapAlloc(heap(), 8, size);
    if (ptr == nullptr) throw StateError('Native allocation failed');
    return ptr.cast<Uint8>();
  }

  void release(Pointer pointer) {
    if (pointer != nullptr) heapFree(heap(), 0, pointer.cast<Void>());
  }

  Pointer<Uint8> text(String value) {
    if (value.contains('\u0000'))
      throw ArgumentError('Text must not contain NUL bytes');
    final bytes = utf8.encode(value);
    final ptr = allocate(bytes.length + 1);
    ptr.asTypedList(bytes.length).setAll(0, bytes);
    return ptr;
  }

  String take(Pointer<Uint8> pointer) {
    if (pointer == nullptr)
      throw BrowserControlError('sdk_error', 'Native SDK returned no response');
    try {
      var length = 0;
      while (length < 64 * 1024 * 1024 + 16384 && pointer[length] != 0) {
        length++;
      }
      if (length == 64 * 1024 * 1024 + 16384)
        throw StateError('Native reply exceeds supported bound');
      return utf8.decode(pointer.asTypedList(length));
    } finally {
      free(pointer.cast<Void>());
    }
  }
}

/// A running BrowserControl instance. Always call [close] after use.
class Browser {
  final _Native _native;
  int _handle;
  Browser._(this._native, this._handle);

  /// Confirms a running instance; null endpoint discovers one for this user.
  static Future<Browser> connect({String? endpoint}) async {
    if (!Platform.isWindows || sizeOf<IntPtr>() != 8) {
      throw UnsupportedError('BrowserControl requires Windows x64');
    }
    final bundled = File(
        '${File(Platform.resolvedExecutable).parent.path}${Platform.pathSeparator}BrowserControlBridge.dll');
    final String libraryPath;
    if (bundled.existsSync()) {
      libraryPath = bundled.path;
    } else {
      final uri = await Isolate.resolvePackageUri(Uri.parse(
          'package:browser_control_sdk/native/BrowserControlBridge.dll'));
      if (uri == null)
        throw StateError('Packaged native bridge could not be located');
      libraryPath = uri.toFilePath();
    }
    final native = _Native(DynamicLibrary.open(libraryPath));
    final out =
        native.allocate(sizeOf<Pointer<Uint8>>()).cast<Pointer<Uint8>>();
    Pointer<Uint8> path = nullptr;
    try {
      if (endpoint != null) path = native.text(endpoint);
      final handle = native.connect(path, out);
      if (handle == 0) _error(native.take(out.value));
      return Browser._(native, handle);
    } finally {
      native.release(path);
      native.release(out);
    }
  }

  static Never _error(String text) {
    final data = jsonDecode(text) as Map<String, dynamic>;
    throw BrowserControlError(
        data['code'] as String, data['message'] as String, data['details']);
  }

  /// Calls any protocol method and returns UTF-8 JSON text without numeric loss.
  /// Native calls are synchronous; use a worker isolate for a Flutter UI.
  /// Timeout is in milliseconds, 1..120000. Requests are never retried.
  String callJson(String method,
          {Map<String, Object?> parameters = const {},
          int timeoutMs = 15000}) =>
      callRaw(method, jsonEncode(parameters), timeoutMs: timeoutMs);

  /// Invokes a method using JSON text parameters, preserving large integers.
  String callRaw(String method, String parametersJson,
      {int timeoutMs = 15000}) {
    if (jsonDecode(parametersJson) is! Map)
      throw ArgumentError('Parameters must be a JSON object');
    if (_handle == 0)
      throw BrowserControlError('closed', 'Browser client is closed');
    if (method.isEmpty ||
        method.contains('\u0000') ||
        timeoutMs < 1 ||
        timeoutMs > 120000) {
      throw ArgumentError('Use a nonempty method and timeout 1..120000');
    }
    Pointer<Uint8> name = nullptr;
    Pointer<Uint8> body = nullptr;
    final out =
        _native.allocate(sizeOf<Pointer<Uint8>>()).cast<Pointer<Uint8>>();
    try {
      name = _native.text(method);
      body = _native.text(parametersJson);
      final code = _native.call(_handle, name, body, timeoutMs, out);
      final reply = _native.take(out.value);
      if (code != 0) _error(reply);
      return reply;
    } finally {
      _native.release(name);
      _native.release(body);
      _native.release(out);
    }
  }

  String infoJson() => callJson('browser.info');
  String tabsJson() => callJson('tabs.list');

  /// Releases the native client. Safe to call more than once.
  void close() {
    if (_handle != 0) {
      _native.close(_handle);
      _handle = 0;
    }
  }
}
