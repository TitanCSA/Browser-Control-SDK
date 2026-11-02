#include "Ipc.h"
#include <sddl.h>
#include <aclapi.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using BrowserControl::Json;
namespace {
std::atomic_uint mutations{0};
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::wstring Wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == BrowserControl::RpcMessage) {
        std::unique_ptr<BrowserControl::Envelope> command(reinterpret_cast<BrowserControl::Envelope*>(lparam));
        unsigned queued = 0;
        if ((command->phase && !command->phase->compare_exchange_strong(queued, 1)) ||
            (command->canceled && command->canceled->load())) return 0;
        if (command->request["method"] == "test.mutate") ++mutations;
        command->reply(BrowserControl::Ok(command->request["params"]));
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void Pump() { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
HANDLE Connect(const std::wstring& pipe, DWORD access = GENERIC_READ | GENERIC_WRITE) {
    Require(WaitNamedPipeW(pipe.c_str(), 5000) != FALSE, "Local named pipe did not become available");
    return CreateFileW(pipe.c_str(), access, 0, nullptr, OPEN_EXISTING, 0, nullptr);
}
void Exact(HANDLE pipe, void* data, DWORD bytes, bool write) {
    DWORD position = 0;
    while (position < bytes) {
        DWORD count = 0;
        const BOOL okay = write ? WriteFile(pipe, static_cast<BYTE*>(data) + position, bytes - position, &count, nullptr) :
            ReadFile(pipe, static_cast<BYTE*>(data) + position, bytes - position, &count, nullptr);
        Require(okay && count, "Local pipe framing failed"); position += count;
    }
}
Json Exchange(const std::wstring& pipe, const std::string& text) {
    HANDLE handle = Connect(pipe);
    Require(handle != INVALID_HANDLE_VALUE, "Pipe connection failed");
    try {
        DWORD size = static_cast<DWORD>(text.size());
        Exact(handle, &size, sizeof(size), true);
        Exact(handle, const_cast<char*>(text.data()), size, true);
        Exact(handle, &size, sizeof(size), false);
        Require(size && size < 65536, "Unexpected test response size");
        std::string response(size, '\0'); Exact(handle, response.data(), size, false);
        CloseHandle(handle); return Json::parse(response);
    } catch (...) { CloseHandle(handle); throw; }
}
Json Call(const std::wstring& pipe, const Json& request, bool pump = true) {
    auto future = std::async(std::launch::async, [&] { return Exchange(pipe, request.dump()); });
    if (pump) while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) Pump();
    return future.get();
}
void VerifyRestrictedDenied(const std::wstring& pipe) {
    HANDLE process = nullptr, restricted = nullptr;
    Require(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE, &process) != FALSE,
            "Cannot create the owned restricted-token test");
    BYTE sidStorage[SECURITY_MAX_SID_SIZE]{}; DWORD sidSize = sizeof(sidStorage);
    Require(CreateWellKnownSid(WinRestrictedCodeSid, nullptr, sidStorage, &sidSize) != FALSE, "Restricted SID unavailable");
    SID_AND_ATTRIBUTES restriction{sidStorage, 0};
    const BOOL made = CreateRestrictedToken(process, DISABLE_MAX_PRIVILEGE, 0, nullptr, 0, nullptr, 1, &restriction, &restricted);
    CloseHandle(process); Require(made != FALSE, "Cannot create restricted token");
    Require(ImpersonateLoggedOnUser(restricted) != FALSE, "Cannot impersonate owned restricted token");
    HANDLE attempt = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    const DWORD error = GetLastError(); RevertToSelf(); CloseHandle(restricted);
    if (attempt != INVALID_HANDLE_VALUE) CloseHandle(attempt);
    Require(attempt == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED, "Restricted token reached the private pipe");
}
}
int main() try {
    WNDCLASSW windowClass{}; windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr); windowClass.lpszClassName = L"BrowserControl.IpcTest";
    Require(RegisterClassW(&windowClass) != 0, "Cannot register the local test window");
    HWND window = CreateWindowW(windowClass.lpszClassName, L"Owned IPC test", 0, 0, 0, 1, 1,
                                HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    Require(window != nullptr, "Cannot create the local test dispatcher");
    wchar_t localData[32768]{};
    Require(GetEnvironmentVariableW(L"LOCALAPPDATA", localData, 32768) != 0, "No current-user Local AppData folder");
    const auto folder = std::filesystem::path(localData) / L"TitanCSA" / L"BrowserControl" / L"Validation" /
        (L"Ipc-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const auto endpoint = folder / L"instance.json";
    BrowserControl::IpcServer server; std::wstring error;
    if (!server.Start(window, endpoint.native(), folder.native(), error)) {
        std::wcerr << error << L'\n'; throw std::runtime_error("Private IPC startup failed");
    }
    Json info; std::ifstream stream(endpoint); stream >> info; stream.close();
    const auto pipe = Wide(info["pipe"].get<std::string>());
    Json request{{"version",1},{"id",1},{"token",info["token"]},{"method","test.echo"},{"params",{{"value",42}}}};
    auto reply = Call(pipe, request);
    Require(reply["ok"] == true && reply["id"] == 1 && reply["result"]["value"] == 42, "Valid request response mismatch");
    std::cout << "PASS local framing and authenticated UI dispatch\n";
    auto wrong = request; wrong["token"] = std::string(64, '0'); reply = Call(pipe, wrong);
    Require(reply["ok"] == false && reply["error"]["code"] == "unauthorized", "Wrong capability token accepted");
    std::cout << "PASS incorrect token rejection\n";
    wrong = request; wrong["version"] = 2; reply = Call(pipe, wrong);
    Require(reply["error"]["code"] == "invalid_request", "Unsupported protocol version accepted");
    std::cout << "PASS protocol version validation\n";
    wrong = request; wrong["timeout_ms"] = 120001; reply = Call(pipe, wrong);
    Require(reply["error"]["code"] == "invalid_timeout", "Unbounded timeout accepted");
    std::cout << "PASS timeout bounds\n";
    std::string nested(100, '['); nested += "0"; nested += std::string(100, ']');
    auto future = std::async(std::launch::async, [&] { return Exchange(pipe, nested); });
    while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) Pump();
    reply = future.get(); Require(reply["error"]["code"] == "invalid_json", "Excessive JSON nesting accepted");
    std::cout << "PASS bounded JSON parser\n";
    VerifyRestrictedDenied(pipe); std::cout << "PASS restricted-token access refusal\n";
    wrong = request; wrong["method"] = "test.mutate"; wrong["timeout_ms"] = 30;
    reply = Call(pipe, wrong, false);
    Require(reply["error"]["code"] == "timeout" && reply["error"]["details"]["may_have_executed"] == false,
            "Queued timeout did not report pre-dispatch cancellation");
    Pump(); Require(mutations.load() == 0, "A queued timed-out command executed later");
    std::cout << "PASS atomic cancellation before dispatch\n";
    HANDLE slow = Connect(pipe); Require(slow != INVALID_HANDLE_VALUE, "Cannot test a stalled peer");
    DWORD partial = 100; Exact(slow, &partial, sizeof(partial), true);
    const auto start = GetTickCount64(); server.Stop();
    Require(GetTickCount64() - start < 1500, "Shutdown waited for a stalled client's read deadline");
    CloseHandle(slow); Pump(); DestroyWindow(window);
    Require(!std::filesystem::exists(endpoint), "Private endpoint was not removed on shutdown");
    std::filesystem::remove(folder);
    std::cout << "PASS prompt shutdown and private endpoint cleanup\n";
    std::cout << "All 8 native IPC validation cases passed; no browser or website was opened.\n";
    return 0;
} catch (const std::exception& error) { RevertToSelf(); std::cerr << "FAIL " << error.what() << '\n'; return 1; }
