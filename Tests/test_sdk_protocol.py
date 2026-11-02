"""Offline framing/ownership/error checks; no browser, account or website opened."""
import base64
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "SDK" / "Python"))
import browser_control as sdk
spec = importlib.util.spec_from_file_location("browser_control_mcp", ROOT / "MCP" / "server.py")
mcp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mcp)


class FakePipe:
    mode = "success"
    writes = 0
    last_request = None
    def __init__(self, _name, _deadline):
        self.payload = b""
        self.closed = False
    def write(self, data):
        FakePipe.writes += 1
        request = json.loads(data[4:].decode("utf-8"))
        FakePipe.last_request = request
        if FakePipe.mode == "timeout":
            raise sdk.BrowserError("transport_timeout", "Timed out")
        response = {"id": request["id"], "ok": True, "result": {"text": "Café → 東京"}}
        if FakePipe.mode == "id": response["id"] = "wrong"
        if FakePipe.mode == "server_error":
            response = {"id": request["id"], "ok": False,
                        "error": {"code": "stale_ref", "message": "Acquire a new snapshot", "details": {"generation": 2}}}
        value = json.dumps(response, ensure_ascii=False).encode("utf-8")
        self.payload = struct.pack("<I", sdk.MAX_RESPONSE + 1) if FakePipe.mode == "oversize" else struct.pack("<I", len(value)) + value
    def read(self, size):
        value, self.payload = self.payload[:size], self.payload[size:]
        return value
    def close(self):
        self.closed = True


class ClientTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.file = Path(self.folder.name) / "endpoint.json"
        self.file.write_text(json.dumps({"version": 1, "pipe": sdk.PIPE_PREFIX + "offline-test",
                                         "token": "not-a-real-capability-0000000000", "pid": 42}), encoding="utf-8")
        self.browser = sdk.Browser(self.file)
        FakePipe.mode, FakePipe.writes, FakePipe.last_request = "success", 0, None
    def tearDown(self):
        self.folder.cleanup()
    def test_unicode_framing_and_token_not_in_repr(self):
        with patch.object(sdk, "_WindowsPipe", FakePipe):
            value = self.browser.call("page.evaluate", {"expression": "'Café → 東京'"})
        self.assertEqual(value["text"], "Café → 東京")
        self.assertEqual(FakePipe.last_request["params"]["expression"], "'Café → 東京'")
        self.assertNotIn("not-a-real-capability", repr(self.browser))
        self.assertEqual(FakePipe.writes, 1)
    def test_lost_reply_is_unknown_without_retry(self):
        FakePipe.mode = "timeout"
        with patch.object(sdk, "_WindowsPipe", FakePipe), self.assertRaises(sdk.BrowserError) as error:
            self.browser.call("page.click", {"selector": "#send"})
        self.assertEqual(error.exception.code, "result_unknown")
        self.assertTrue(error.exception.details["may_have_executed"])
        self.assertEqual(FakePipe.writes, 1)
    def test_response_id_and_length_are_bounded(self):
        for mode in ("id", "oversize"):
            FakePipe.mode = mode
            with patch.object(sdk, "_WindowsPipe", FakePipe), self.assertRaises(sdk.BrowserError) as error:
                self.browser.call("page.snapshot")
            self.assertEqual(error.exception.code, "result_unknown")
    def test_declared_server_error_preserved(self):
        FakePipe.mode = "server_error"
        with patch.object(sdk, "_WindowsPipe", FakePipe), self.assertRaises(sdk.BrowserError) as error:
            self.browser.call("page.click", ref="old")
        self.assertEqual(error.exception.code, "stale_ref")
        self.assertEqual(error.exception.details["generation"], 2)
    def test_request_bound_prevents_transport(self):
        with patch.object(sdk, "_WindowsPipe", FakePipe), self.assertRaises(sdk.BrowserError) as error:
            self.browser.call("page.type", text="x" * sdk.MAX_REQUEST)
        self.assertEqual(error.exception.code, "request_too_large")
        self.assertEqual(FakePipe.writes, 0)
    def test_nonfinite_json_and_invalid_deadline_rejected(self):
        with self.assertRaises(ValueError): self.browser.call("page.mouse", x=float("nan"))
        for value in (0, 120001, True, 1.5):
            with self.assertRaises(ValueError): self.browser.call("page.info", timeout_ms=value)
    def test_wrong_endpoint_type_is_not_accepted(self):
        value = json.loads(self.file.read_text(encoding="utf-8"))
        for invalid in (True, "1", 2):
            value["version"] = invalid
            self.file.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(sdk.BrowserError): sdk.Browser(self.file)
    def test_private_profile_mode_is_not_silently_overridden(self):
        with patch.object(self.browser, "call", return_value={"id": "t1"}) as call:
            self.browser.new_tab(profile="private-existing")
        self.assertNotIn("in_private", call.call_args.args[1])
    def test_selector_and_ref_are_not_ambiguous(self):
        with self.assertRaises(ValueError): self.browser.tab("t1").click("e1", selector="#other")
        with self.assertRaises(ValueError): self.browser.tab("t1").click(x=10)
    def test_raw_cdp_method_survives_wrapper(self):
        with patch.object(self.browser, "call", return_value={}) as call:
            self.browser.tab("t1").cdp("Runtime.evaluate", {"expression": "1+1"})
        self.assertEqual(call.call_args.args[0], "page.cdp")
        self.assertEqual(call.call_args.args[1]["method"], "Runtime.evaluate")
    def test_live_frames_are_lazy_and_stop_without_an_extra_capture(self):
        tab = self.browser.tab("t1")
        stopped = threading.Event()
        with patch.object(tab, "screenshot", return_value={"data": "fake", "mime_type": "image/png"}) as screenshot:
            frames = tab.frames(interval_ms=0, stop_event=stopped, full_page=True)
            self.assertEqual(screenshot.call_count, 0)
            self.assertEqual(next(frames)["data"], "fake")
            self.assertEqual(screenshot.call_count, 1)
            stopped.set()
            with self.assertRaises(StopIteration): next(frames)
            self.assertEqual(screenshot.call_count, 1)
            self.assertTrue(screenshot.call_args.kwargs["full_page"])
    def test_invalid_live_interval_does_not_capture(self):
        tab = self.browser.tab("t1")
        for interval in (-1, float("inf"), True):
            with patch.object(tab, "screenshot") as screenshot, self.assertRaises(ValueError):
                next(tab.frames(interval_ms=interval))
            screenshot.assert_not_called()


class FakeTab:
    def screenshot(self, **_options):
        return {"data": base64.b64encode(b"test-image").decode(), "mime_type": "image/png", "width": 1, "height": 1}


class FakeBrowser:
    def info(self): return {"ready": True}
    def capabilities(self): return {"methods": ["page.snapshot"]}
    def tab(self, _tab): return FakeTab()
    def call(self, method, params, **options):
        raise sdk.BrowserError("result_unknown", "Inspect before retrying", {"may_have_executed": True})


class McpTests(unittest.TestCase):
    def setUp(self): self.bridge = mcp.Bridge(browser=FakeBrowser())
    def legacy(self):
        return self.bridge.request({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                    "params": {"protocolVersion": "2025-11-25", "capabilities": {}}})
    def test_legacy_discovery_does_not_open_a_browser(self):
        initial = self.legacy()
        self.assertEqual(initial["result"]["protocolVersion"], "2025-11-25")
        listed = self.bridge.request({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertIn("browser_screenshot", [x["name"] for x in listed["result"]["tools"]])
    def test_modern_per_request_discovery(self):
        message = {"jsonrpc": "2.0", "id": "d1", "method": "server/discover", "params": {"_meta": {
            "io.modelcontextprotocol/protocolVersion": "2026-07-28", "io.modelcontextprotocol/clientCapabilities": {}}}}
        self.assertEqual(self.bridge.request(message)["result"]["resultType"], "complete")
        del message["params"]["_meta"]["io.modelcontextprotocol/clientCapabilities"]
        with self.assertRaises(mcp.RpcError) as error: self.bridge.request(message)
        self.assertEqual(error.exception.code, -32602)
    def test_screenshot_is_a_protocol_image(self):
        self.legacy()
        result = self.bridge.request({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
            "params": {"name": "browser_screenshot", "arguments": {"tab": "t1"}}})["result"]
        self.assertEqual(result["content"][0]["type"], "image")
        self.assertEqual(result["content"][0]["mimeType"], "image/png")
        self.assertNotIn('"data"', result["content"][1]["text"])
    def test_action_uncertainty_is_not_hidden(self):
        self.legacy()
        result = self.bridge.request({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
            "params": {"name": "browser_call", "arguments": {"method": "page.click", "params": {"tab": "t1", "ref": "e1"}}}})["result"]
        self.assertTrue(result["isError"])
        self.assertTrue(json.loads(result["content"][0]["text"])["details"]["may_have_executed"])
    def test_stdio_emits_only_json_and_preserves_ids(self):
        requests = [{"jsonrpc": "2.0", "id": "first", "method": "initialize", "params": {"protocolVersion": "2025-06-18"}},
                    {"jsonrpc": "2.0", "method": "notifications/initialized"},
                    {"jsonrpc": "2.0", "id": "second", "method": "tools/list"}]
        data = b"".join((json.dumps(x) + "\n").encode() for x in requests) + b"invalid-json\n"
        result = subprocess.run([sys.executable, str(ROOT / "MCP" / "server.py")], input=data, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        replies = [json.loads(x) for x in result.stdout.splitlines()]
        self.assertEqual(len(replies), 3)
        self.assertEqual(replies[0]["id"], "first")
        self.assertEqual(replies[1]["id"], "second")
        self.assertEqual(replies[2]["error"]["code"], -32700)
        self.assertEqual(result.stderr, b"")


if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
