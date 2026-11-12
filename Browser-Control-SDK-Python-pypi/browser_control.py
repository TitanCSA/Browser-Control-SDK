"""Small standard-library client for the owned Browser Control Windows browser."""
from __future__ import annotations

import base64
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import threading
import time
import uuid
from typing import Any, Optional

MAX_REQUEST = 1024 * 1024
MAX_RESPONSE = 64 * 1024 * 1024
MAX_ENDPOINT = 16384
PIPE_PREFIX = "\\\\.\\pipe\\BrowserControl-"


class BrowserError(RuntimeError):
    """A stable English error code/message, with optional structured details."""
    def __init__(self, code: str, message: str, details: Any = None):
        self.code, self.message, self.details = code, message, details
        super().__init__(f"{code}: {message}")


def _timeout(value: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= 120000:
        raise ValueError("timeout_ms must be an integer between 1 and 120000")
    return value


def _endpoint(path: Path) -> dict:
    try:
        if path.stat().st_size > MAX_ENDPOINT:
            raise ValueError()
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        if (not isinstance(data, dict) or type(data.get("version")) is not int or data.get("version") != 1 or
                not isinstance(data.get("pipe"), str) or
                not data["pipe"].startswith(PIPE_PREFIX) or
                len(data["pipe"]) > 240 or
                not isinstance(data.get("token"), str) or not 16 <= len(data["token"]) <= 1024):
            raise ValueError()
        return data
    except (OSError, ValueError, TypeError, UnicodeError):
        raise BrowserError("invalid_endpoint", "The private browser endpoint file is missing or invalid") from None


def _decode_response(payload: bytes, request_id: str) -> Any:
    try:
        response = json.loads(payload.decode("utf-8"))
        if not isinstance(response, dict) or response.get("id") != request_id:
            raise ValueError()
        if response.get("ok") is True and "result" in response:
            return response["result"]
        error = response.get("error")
        if response.get("ok") is False and isinstance(error, dict):
            code, message = error.get("code"), error.get("message")
            if isinstance(code, str) and isinstance(message, str):
                raise BrowserError(code, message, error.get("details"))
        raise ValueError()
    except BrowserError:
        raise
    except (ValueError, TypeError, UnicodeError):
        raise BrowserError("invalid_response", "The browser returned an invalid response") from None


class _WindowsPipe:
    """Overlapped IO with deadlines; no worker thread or permanent connection."""
    def __init__(self, name: str, deadline: float):
        if os.name != "nt":
            raise BrowserError("unsupported_platform", "The browser named-pipe client requires Windows")
        from ctypes import WinDLL
        self.api = WinDLL("kernel32", use_last_error=True)
        self.deadline = deadline
        self.handle = None
        self._bind()
        while True:
            handle = self.api.CreateFileW(name, 0xC0000000, 0, None, 3, 0x40000000, None)
            if handle != ctypes.c_void_p(-1).value:
                self.handle = handle
                break
            error = ctypes.get_last_error()
            if error == 2:
                raise BrowserError("browser_not_running", "The selected browser instance is not running")
            if error != 231:
                raise BrowserError("connection_failed", "Could not open the browser pipe", {"windows_error": error})
            left = self._remaining()
            self.api.WaitNamedPipeW(name, min(left, 50))

    def _bind(self):
        class OVERLAPPED(ctypes.Structure):
            _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                        ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                        ("hEvent", wintypes.HANDLE)]
        self.OVERLAPPED = OVERLAPPED
        api = self.api
        api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                   ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        api.CreateFileW.restype = wintypes.HANDLE
        api.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
        api.WaitNamedPipeW.restype = wintypes.BOOL
        api.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
        api.CreateEventW.restype = wintypes.HANDLE
        api.CloseHandle.argtypes = [wintypes.HANDLE]
        api.CloseHandle.restype = wintypes.BOOL
        api.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                 ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(OVERLAPPED)]
        api.ReadFile.restype = wintypes.BOOL
        api.WriteFile.argtypes = api.ReadFile.argtypes
        api.WriteFile.restype = wintypes.BOOL
        api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        api.WaitForSingleObject.restype = wintypes.DWORD
        api.CancelIoEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(OVERLAPPED)]
        api.CancelIoEx.restype = wintypes.BOOL
        api.GetOverlappedResult.argtypes = [wintypes.HANDLE, ctypes.POINTER(OVERLAPPED),
                                          ctypes.POINTER(wintypes.DWORD), wintypes.BOOL]
        api.GetOverlappedResult.restype = wintypes.BOOL

    def _remaining(self) -> int:
        remaining = int((self.deadline - time.monotonic()) * 1000)
        if remaining <= 0:
            raise BrowserError("transport_timeout", "The browser transport deadline expired")
        return remaining

    def _io(self, value: bytes | int, write: bool) -> bytes | int:
        count = len(value) if write else int(value)
        buffer = ctypes.create_string_buffer(value, count) if write else ctypes.create_string_buffer(count)
        event = self.api.CreateEventW(None, True, False, None)
        if not event:
            raise BrowserError("transport_failed", "Could not create a browser IO event")
        operation = self.OVERLAPPED()
        operation.hEvent = event
        done = wintypes.DWORD()
        try:
            timeout = self._remaining()
            immediate = (self.api.WriteFile if write else self.api.ReadFile)(
                self.handle, buffer, count, ctypes.byref(done), ctypes.byref(operation))
            if not immediate:
                error = ctypes.get_last_error()
                if error != 997:
                    raise BrowserError("transport_failed", "The browser pipe closed or rejected IO", {"windows_error": error})
                status = self.api.WaitForSingleObject(event, timeout)
                if status != 0:
                    self.api.CancelIoEx(self.handle, ctypes.byref(operation))
                    # Cancellation completion owns the buffer until its event is signaled.
                    self.api.GetOverlappedResult(self.handle, ctypes.byref(operation), ctypes.byref(done), True)
                    raise BrowserError("transport_timeout", "The browser transport deadline expired")
                if not self.api.GetOverlappedResult(self.handle, ctypes.byref(operation), ctypes.byref(done), False):
                    raise BrowserError("transport_failed", "The browser pipe could not complete IO", {"windows_error": ctypes.get_last_error()})
            if not done.value:
                raise BrowserError("connection_closed", "The browser closed its pipe before the response completed")
            return done.value if write else buffer.raw[:done.value]
        finally:
            self.api.CloseHandle(event)

    def write(self, data: bytes):
        offset = 0
        while offset < len(data):
            offset += int(self._io(data[offset:], True))

    def read(self, count: int) -> bytes:
        chunks, remaining = [], count
        while remaining:
            chunk = self._io(remaining, False)
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def close(self):
        if self.handle is not None:
            self.api.CloseHandle(self.handle)
            self.handle = None


class Browser:
    def __init__(self, endpoint: str | Path):
        self.endpoint_path = Path(endpoint).resolve()
        self._endpoint = _endpoint(self.endpoint_path)
        self._process: Optional[subprocess.Popen] = None

    def __repr__(self):
        return f"Browser(pid={self._endpoint.get('pid')!r}, endpoint={str(self.endpoint_path)!r})"

    @staticmethod
    def instances_directory() -> Path:
        local = os.environ.get("LOCALAPPDATA")
        if not local:
            raise BrowserError("unsupported_platform", "LOCALAPPDATA is unavailable; supply an endpoint file")
        return Path(local) / "TitanCSA" / "BrowserControl" / "instances"

    @classmethod
    def connect(cls, endpoint: str | Path | None = None, timeout_ms: int = 1500) -> Browser:
        _timeout(timeout_ms)
        if endpoint is not None:
            browser = cls(endpoint)
            browser.call("browser.info", timeout_ms=timeout_ms)
            return browser
        folder = cls.instances_directory()
        try:
            candidates = sorted(folder.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
        except OSError:
            candidates = []
        deadline = time.monotonic() + timeout_ms / 1000
        for candidate in candidates[:128]:
            remaining = int((deadline - time.monotonic()) * 1000)
            if remaining <= 0:
                break
            try:
                browser = cls(candidate)
                browser.call("browser.info", timeout_ms=min(remaining, 500))
                return browser
            except BrowserError:
                continue
        raise BrowserError("browser_not_running", "Start BrowserControl.exe or call Browser.launch(executable=...)")

    @classmethod
    def launch(cls, executable: str | Path | None = None, *, profile_root: str | Path | None = None,
               endpoint: str | Path | None = None, timeout_ms: int = 30000) -> Browser:
        _timeout(timeout_ms)
        if os.name != "nt":
            raise BrowserError("unsupported_platform", "BrowserControl.exe requires Windows")
        if executable is None:
            repository = Path(__file__).resolve().parents[2]
            choices = [Path.cwd() / "BrowserControl.exe", repository / "BrowserControl.exe",
                       repository / "obj/Build/bin/Release/BrowserControl.exe",
                       repository / "bin/Release/BrowserControl.exe"]
            executable = next((p for p in choices if p.is_file()), None)
        if executable is None or not Path(executable).is_file():
            raise BrowserError("executable_missing", "Supply the path to BrowserControl.exe")
        target = Path(endpoint) if endpoint else cls.instances_directory() / ("sdk-" + uuid.uuid4().hex + ".json")
        target = target.resolve()
        if target.exists():
            raise BrowserError("endpoint_exists", "A new launched instance needs an unused endpoint path")
        target.parent.mkdir(parents=True, exist_ok=True)
        arguments = [str(Path(executable).resolve()), "--endpoint", str(target)]
        if profile_root is not None:
            arguments += ["--profile-root", str(Path(profile_root).resolve())]
        try:
            process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        except OSError:
            raise BrowserError("launch_failed", "Windows could not start BrowserControl.exe") from None
        stderr_tail = bytearray()
        stderr_lock = threading.Lock()
        def drain_stderr():
            try:
                while True:
                    chunk = process.stderr.read1(2048)
                    if not chunk:
                        break
                    with stderr_lock:
                        stderr_tail.extend(chunk)
                        del stderr_tail[:-8192]
            except (OSError, ValueError):
                pass
        stderr_reader = threading.Thread(target=drain_stderr, name="browser-startup-diagnostics", daemon=True)
        stderr_reader.start()
        deadline = time.monotonic() + timeout_ms / 1000
        while time.monotonic() < deadline:
            if process.poll() is not None:
                stderr_reader.join(timeout=0.1)
                with stderr_lock:
                    detail = stderr_tail.decode("utf-8", errors="replace").strip()
                message = "BrowserControl.exe exited during startup" + (": " + detail if detail else "")
                raise BrowserError("launch_failed", message, {"exit_code": process.returncode})
            if target.is_file():
                try:
                    browser = cls(target)
                    info = browser.call("browser.info", timeout_ms=min(1000, max(1, int((deadline-time.monotonic())*1000))))
                    if not isinstance(info, dict) or info.get("ready", True):
                        browser._process = process
                        return browser
                except BrowserError:
                    pass
            time.sleep(0.05)
        raise BrowserError("launch_timeout", "The browser did not become ready before the startup deadline; its window may still be open")

    def call(self, method: str, params: dict | None = None, *, timeout_ms: int = 15000, **arguments) -> Any:
        timeout_ms = _timeout(timeout_ms)
        if not isinstance(method, str) or not method or len(method) > 128:
            raise ValueError("method must be a nonempty name of at most 128 characters")
        if params is not None and not isinstance(params, dict):
            raise TypeError("params must be a dictionary")
        values = dict(params or {})
        if set(values) & set(arguments):
            raise ValueError("A parameter was supplied twice")
        values.update(arguments)
        request_id = uuid.uuid4().hex
        request = {"version": 1, "id": request_id, "token": self._endpoint["token"],
                   "method": method, "params": values, "timeout_ms": timeout_ms}
        try:
            payload = json.dumps(request, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
        except (TypeError, ValueError, UnicodeError):
            raise ValueError("Browser parameters must be finite UTF-8 JSON values") from None
        if len(payload) > MAX_REQUEST:
            raise BrowserError("request_too_large", "The browser request exceeds 1 MiB")
        channel = _WindowsPipe(self._endpoint["pipe"], time.monotonic() + timeout_ms / 1000 + 1)
        try:
            channel.write(struct.pack("<I", len(payload)) + payload)
            length = struct.unpack("<I", channel.read(4))[0]
            if not 1 <= length <= MAX_RESPONSE:
                raise BrowserError("invalid_response", "Browser response length is outside the supported bound")
            response_payload = channel.read(length)
        except BrowserError as error:
            raise BrowserError("result_unknown", "No confirmed browser reply; inspect state before retrying the action",
                               {"may_have_executed": True, "cause_code": error.code, "method": method}) from None
        finally:
            channel.close()
        try:
            return _decode_response(response_payload, request_id)
        except BrowserError as error:
            if error.code == "invalid_response":
                raise BrowserError("result_unknown", "The browser reply could not be validated; inspect state before retrying",
                                   {"may_have_executed": True, "cause_code": error.code, "method": method}) from None
            raise

    def info(self):
        return self.call("browser.info")

    def capabilities(self):
        return self.call("browser.capabilities")

    def tabs(self):
        return self.call("tabs.list")

    def new_tab(self, url: str = "about:blank", *, profile: str = "default", in_private: bool | None = None,
                background: bool = False) -> Tab:
        options = {"url": url, "profile": profile, "background": background}
        if in_private is not None:
            options["in_private"] = in_private
        value = self.call("tabs.create", options)
        return Tab(self, value["id"])

    def tab(self, tab_id: str) -> Tab:
        return Tab(self, tab_id)

    def profiles(self):
        return self.call("profiles.list")

    def create_profile(self, name: str, *, in_private: bool = False):
        return self.call("profiles.create", name=name, in_private=in_private)

    def saved_logins(self, profile: str | None = None, origin: str | None = None):
        """List saved-login metadata only. Passwords never leave the browser vault."""
        options = {key: value for key, value in (("profile", profile), ("origin", origin)) if value is not None}
        return self.call("saved_logins.list", **options)

    def delete_saved_login(self, credential_id: str):
        return self.call("saved_logins.delete", credential_id=credential_id)

    def set_saved_login_autofill(self, credential_id: str, enabled: bool):
        if not isinstance(enabled, bool):
            raise TypeError("enabled must be a boolean")
        return self.call("saved_logins.autofill", credential_id=credential_id, enabled=enabled)

    def window(self, **options):
        return self.call("window.set" if options else "window.info", **options)

    def events(self, since: int = 0, limit: int = 100):
        return self.call("events.read", since=since, limit=limit)

    def downloads(self, **options):
        return self.call("downloads.configure" if options else "downloads.list", **options)

    def shutdown(self):
        """Explicitly close this browser instance, including its tabs."""
        return self.call("browser.shutdown")


class Tab:
    def __init__(self, browser: Browser, tab_id: str):
        if not isinstance(tab_id, str) or not tab_id:
            raise ValueError("tab_id must be a nonempty string")
        self.browser, self.id = browser, tab_id

    def call(self, method: str, *, timeout_ms: int = 15000, **params):
        return self.browser.call(method, {"tab": self.id, **params}, timeout_ms=timeout_ms)

    def info(self):
        return self.call("page.info")

    def activate(self):
        return self.call("tabs.activate")

    def close(self):
        return self.call("tabs.close")

    def goto(self, url: str, *, wait: str = "load", timeout_ms: int = 15000):
        return self.browser.call("page.navigate", {"tab": self.id, "url": url, "wait": wait,
                                                  "timeout_ms": timeout_ms}, timeout_ms=timeout_ms)

    def back(self):
        return self.call("page.back")

    def forward(self):
        return self.call("page.forward")

    def reload(self):
        return self.call("page.reload")

    def stop(self):
        return self.call("page.stop")

    def snapshot(self):
        return self.call("page.snapshot")

    def observe(self, *, max_elements: int = 80, max_text: int = 6000, include_text: bool = True,
                role: str | None = None, name: str | None = None):
        options = {"max_elements": max_elements, "max_text": max_text, "include_text": include_text}
        if role is not None:
            options["role"] = role
        if name is not None:
            options["name"] = name
        return self.call("page.observe", **options)

    def act(self, actions: list[dict], *, observe: bool = True, timeout_ms: int = 15000,
            expected_generation: int | None = None, expected_url: str | None = None):
        """Run one finite native batch; stop on first error, without retries."""
        _actions(actions)
        if not isinstance(observe, bool):
            raise TypeError("observe must be a boolean")
        guards = {}
        if expected_generation is not None:
            if type(expected_generation) is not int or not 0 <= expected_generation <= 0xffffffffffffffff:
                raise ValueError("expected_generation must be an unsigned integer")
            guards["expected_generation"] = expected_generation
        if expected_url is not None:
            if not isinstance(expected_url, str):
                raise TypeError("expected_url must be a string")
            guards["expected_url"] = expected_url
        return self.call("page.act", actions=actions, observe=observe, timeout_ms=timeout_ms, **guards)

    def login(self, operation: str = "status", *, credential_id: str | None = None, submit: bool = False):
        """Inspect login fields, or explicitly fill a saved login authorized by the user.

        Filling requires the saved login's exact origin and profile. Passwords
        are never exported; submission requires submit=True explicitly. Status
        never fills anything. Submission does not prove authentication.
        """
        if operation not in ("status", "fill"):
            raise ValueError("operation must be status or fill")
        if not isinstance(submit, bool):
            raise TypeError("submit must be a boolean")
        params = {"operation": operation, "submit": submit}
        if credential_id is not None:
            params["credential_id"] = credential_id
        return self.call("page.login", **params)

    def evaluate(self, expression: str, *, await_promise: bool = True, world: str = "main", timeout_ms: int = 15000):
        value = self.call("page.evaluate", expression=expression, await_promise=await_promise, world=world, timeout_ms=timeout_ms)
        return value.get("value") if isinstance(value, dict) and ("value" in value or value.get("undefined")) else value

    def wait(self, *, selector: str | None = None, ref: str | None = None,
             name: str | None = None, role: str | None = None,
             state: str = "visible", timeout_ms: int = 15000):
        target = _target(ref, selector, name=name, role=role)
        return self.browser.call("page.wait", {"tab": self.id, **target, "state": state,
                                              "timeout_ms": timeout_ms}, timeout_ms=timeout_ms)

    def click(self, ref: str | None = None, *, selector: str | None = None,
              name: str | None = None, role: str | None = None,
              x: float | None = None, y: float | None = None, button: str = "left", click_count: int = 1):
        return self.call("page.click", **_target(ref, selector, x, y, name=name, role=role), button=button, click_count=click_count)

    def fill(self, text: str, *, ref: str | None = None, selector: str | None = None,
             name: str | None = None, role: str | None = None):
        return self.call("page.fill", **_target(ref, selector, name=name, role=role), text=text)

    def type(self, text: str):
        return self.call("page.type", text=text)

    def press(self, key: str, *, action: str = "press"):
        return self.call("page.key", key=key, action=action)

    def mouse(self, type: str, x: float, y: float, **options):
        return self.call("page.mouse", type=type, x=x, y=y, **options)

    def scroll(self, delta_y: float, delta_x: float = 0, **position):
        return self.call("page.scroll", delta_y=delta_y, delta_x=delta_x, **position)

    def screenshot(self, path: str | Path | None = None, *, full_page: bool = False,
                   clip: dict | None = None, format: str = "png", **options):
        params = {"full_page": full_page, "format": format, **options}
        if clip is not None:
            params["clip"] = clip
        result = self.call("page.screenshot", **params)
        if path is not None:
            try:
                data = base64.b64decode(result["data"], validate=True)
            except (ValueError, KeyError, TypeError):
                raise BrowserError("invalid_response", "The browser returned invalid screenshot data") from None
            Path(path).write_bytes(data)
        return result

    def frames(self, interval_ms: float = 100, stop_event=None, **screenshot_options):
        """Yield fresh captures only as consumed, with no background queue.

        interval_ms is a minimum capture-start period. Zero adds no SDK delay.
        stop_event may be a threading.Event; an in-flight capture still completes.
        """
        if (isinstance(interval_ms, bool) or not isinstance(interval_ms, (int, float)) or
                not math.isfinite(interval_ms) or not 0 <= interval_ms <= 120000):
            raise ValueError("interval_ms must be a finite number between 0 and 120000")
        if stop_event is not None and (not callable(getattr(stop_event, "is_set", None)) or
                                       not callable(getattr(stop_event, "wait", None))):
            raise TypeError("stop_event must provide is_set() and wait(timeout)")
        while stop_event is None or not stop_event.is_set():
            started = time.monotonic()
            frame = self.screenshot(**screenshot_options)
            if stop_event is not None and stop_event.is_set():
                return
            yield frame
            remaining = max(0, interval_ms / 1000 - (time.monotonic() - started))
            if stop_event is not None:
                if stop_event.wait(remaining):
                    return
            elif remaining:
                time.sleep(remaining)

    def viewport(self, width: int, height: int, device_scale_factor: float = 1):
        return self.call("page.viewport", width=width, height=height, device_scale_factor=device_scale_factor)

    def settings(self, **settings):
        return self.call("page.settings", settings=settings)

    def cookies(self, operation: str = "list", **options):
        return self.call("page.cookies", operation=operation, **options)

    def storage(self, operation: str = "get", **options):
        return self.call("page.storage", operation=operation, **options)

    def cdp(self, method: str, params: dict | None = None):
        return self.browser.call("page.cdp", {"tab": self.id, "method": method, "params": params or {}})


_ACTION_NAMES = frozenset(("click", "fill", "type", "key", "mouse", "scroll", "wait", "navigate", "observe"))


def _actions(actions):
    if not isinstance(actions, list) or not 1 <= len(actions) <= 20:
        raise ValueError("actions must be a list containing between 1 and 20 actions")
    for action in actions:
        if (not isinstance(action, dict) or not isinstance(action.get("action"), str) or
                action["action"] not in _ACTION_NAMES):
            raise ValueError("Each action must be an object with a supported action name")


def _target(ref=None, selector=None, x=None, y=None, *, name=None, role=None):
    choices = (int(ref is not None) + int(selector is not None) +
               int(x is not None or y is not None) + int(name is not None))
    if choices != 1 or ((x is None) != (y is None)) or (role is not None and name is None):
        raise ValueError("Specify one target: ref, selector, name with optional role, or both x and y")
    if ref is not None:
        return {"ref": ref}
    if selector is not None:
        return {"selector": selector}
    if name is not None:
        if not isinstance(name, str) or not name or (role is not None and (not isinstance(role, str) or not role)):
            raise ValueError("A semantic target needs a nonempty name and, when supplied, role")
        return {"name": name, **({"role": role} if role is not None else {})}
    return {"x": x, "y": y}
