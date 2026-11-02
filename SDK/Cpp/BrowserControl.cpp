#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "BrowserControl.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <vector>

namespace BrowserControlSDK {
namespace {
constexpr unsigned MaxRequest = 1024 * 1024;
constexpr unsigned MaxResponse = 64 * 1024 * 1024;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw Error("invalid_endpoint", "The browser endpoint has an invalid UTF-8 pipe name");
    std::wstring value(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), size) != size)
        throw Error("invalid_endpoint", "The browser endpoint pipe name could not be decoded");
    return value;
}
DWORD Remaining(ULONGLONG deadline) {
    const auto now = GetTickCount64();
    if (now >= deadline) throw Error("transport_timeout", "The browser transport deadline expired");
    return static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, MAXDWORD - 1));
}
DWORD Transfer(HANDLE pipe, void* buffer, DWORD length, bool write, ULONGLONG deadline) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.value) throw Error("transport_failed", "Could not create a browser IO event");
    OVERLAPPED operation{};
    operation.hEvent = event.value;
    DWORD done = 0;
    const auto timeout = Remaining(deadline);
    const auto immediate = write ? WriteFile(pipe, buffer, length, &done, &operation) : ReadFile(pipe, buffer, length, &done, &operation);
    if (!immediate) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) throw Error("transport_failed", "The browser pipe closed or rejected IO", {{"windows_error", error}});
        if (WaitForSingleObject(event.value, timeout) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation);
            // The buffer and OVERLAPPED remain alive until cancellation completes.
            GetOverlappedResult(pipe, &operation, &done, TRUE);
            throw Error("transport_timeout", "The browser transport deadline expired");
        }
        if (!GetOverlappedResult(pipe, &operation, &done, FALSE))
            throw Error("transport_failed", "The browser pipe could not complete IO", {{"windows_error", GetLastError()}});
    }
    if (!done) throw Error("connection_closed", "The browser closed its pipe before the response completed");
    return done;
}
void Exact(HANDLE pipe, void* buffer, unsigned length, bool write, ULONGLONG deadline) {
    auto bytes = static_cast<unsigned char*>(buffer);
    unsigned offset = 0;
    while (offset < length) offset += Transfer(pipe, bytes + offset, length - offset, write, deadline);
}
void Object(const Json& value) {
    if (!value.is_object()) throw std::invalid_argument("Browser parameters must be a JSON object");
}
std::wstring Argument(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
}

namespace Detail {
struct Connection { std::wstring pipe; std::string token; mutable std::atomic<unsigned long long> sequence{0}; };
std::shared_ptr<Connection> Read(const std::filesystem::path& path) {
    try {
        if (std::filesystem::file_size(path) > 16384) throw std::runtime_error("oversized");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("missing");
        auto data = Json::parse(stream);
        auto result = std::make_shared<Connection>();
        const auto pipe = data.at("pipe").get<std::string>();
        result->token = data.at("token").get<std::string>();
        if (data.at("version") != 1 || pipe.rfind("\\\\.\\pipe\\BrowserControl-", 0) != 0 || pipe.size() > 240 ||
            result->token.size() < 16 || result->token.size() > 1024) throw std::runtime_error("invalid");
        result->pipe = Wide(pipe);
        return result;
    } catch (...) { throw Error("invalid_endpoint", "The private browser endpoint file is missing or invalid"); }
}
Json Call(const std::shared_ptr<Connection>& connection, const std::string& method, Json params, unsigned timeoutMs) {
    Object(params);
    if (timeoutMs < 1 || timeoutMs > 120000 || method.empty() || method.size() > 128)
        throw std::invalid_argument("Use a nonempty method and timeout between 1 and 120000 milliseconds");
    const auto id = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++connection->sequence);
    auto request = Json{{"version", 1}, {"id", id}, {"token", connection->token}, {"method", method},
                        {"params", std::move(params)}, {"timeout_ms", timeoutMs}}.dump();
    if (request.size() > MaxRequest) throw Error("request_too_large", "The browser request exceeds 1 MiB");
    const auto deadline = GetTickCount64() + timeoutMs + 1000;
    Handle pipe;
    for (;;) {
        pipe.value = CreateFileW(connection->pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe.value != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            throw Error("browser_not_running", "The selected browser instance is not running");
        if (error != ERROR_PIPE_BUSY)
            throw Error("connection_failed", "Could not open the browser pipe", {{"windows_error", error}});
        WaitNamedPipeW(connection->pipe.c_str(), std::min<DWORD>(Remaining(deadline), 50));
    }
    std::string response;
    try {
        auto length = static_cast<unsigned>(request.size());
        Exact(pipe.value, &length, 4, true, deadline);
        Exact(pipe.value, request.data(), length, true, deadline);
        Exact(pipe.value, &length, 4, false, deadline);
        if (!length || length > MaxResponse) throw Error("invalid_response", "Browser response length is outside the supported bound");
        response.resize(length);
        Exact(pipe.value, response.data(), length, false, deadline);
    } catch (const Error& error) {
        throw Error("result_unknown", "No confirmed browser reply; inspect state before retrying the action",
                    {{"may_have_executed", true}, {"cause_code", error.code}, {"method", method}});
    }
    Json value;
    try {
        value = Json::parse(response);
        if (!value.is_object() || value.value("id", "") != id || !value.contains("ok") || !value["ok"].is_boolean())
            throw std::runtime_error("invalid");
        if (value["ok"] == true && value.contains("result")) return value["result"];
        if (value["ok"] != false || !value["error"].is_object() ||
            !value["error"].value("code", Json()).is_string() || !value["error"].value("message", Json()).is_string())
            throw std::runtime_error("invalid");
    } catch (...) {
        throw Error("result_unknown", "The browser reply could not be validated; inspect state before retrying",
                    {{"may_have_executed", true}, {"method", method}});
    }
    throw Error(value["error"]["code"].get<std::string>(), value["error"]["message"].get<std::string>(), value["error"].value("details", Json::object()));
}
}

Error::Error(std::string errorCode, std::string message, Json info)
    : std::runtime_error(errorCode + ": " + message), code(std::move(errorCode)), details(std::move(info)) {}
Browser::Browser(std::shared_ptr<Detail::Connection> connection) : connection_(std::move(connection)) {}
Browser Browser::Launch(const std::filesystem::path& executable, const std::filesystem::path& profileRoot, unsigned timeoutMs) {
    if (timeoutMs < 1 || timeoutMs > 120000) throw std::invalid_argument("Use a startup timeout between 1 and 120000 milliseconds");
    const auto image = std::filesystem::absolute(executable);
    if (!std::filesystem::is_regular_file(image)) throw Error("executable_missing", "Supply the path to BrowserControl.exe");
    wchar_t local[32768]{};
    const auto localLength = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (!localLength || localLength >= 32768) throw Error("launch_failed", "LOCALAPPDATA is unavailable for private endpoint discovery");
    static std::atomic<unsigned long long> launchSequence{0};
    const auto folder = std::filesystem::path(local) / L"TitanCSA/BrowserControl/instances";
    std::filesystem::create_directories(folder);
    const auto endpoint = folder / (L"sdk-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++launchSequence) + L".json");
    if (std::filesystem::exists(endpoint)) throw Error("endpoint_exists", "A launched instance needs an unused private endpoint path");
    std::wstring command = Argument(image.wstring()) + L" --endpoint " + Argument(endpoint.wstring());
    if (!profileRoot.empty()) command += L" --profile-root " + Argument(std::filesystem::absolute(profileRoot).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // No shell, inherited handles, credentials or existing browser profile are passed.
    if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        throw Error("launch_failed", "Windows could not start BrowserControl.exe", {{"windows_error", GetLastError()}});
    Handle processHandle(process.hProcess), threadHandle(process.hThread);
    const auto deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(processHandle.value, 0) == WAIT_OBJECT_0) {
            DWORD exitCode = 0;
            GetExitCodeProcess(processHandle.value, &exitCode);
            throw Error("launch_failed", "BrowserControl.exe exited during startup", {{"exit_code", exitCode}});
        }
        if (std::filesystem::is_regular_file(endpoint)) try {
            Browser browser(Detail::Read(endpoint));
            const auto info = browser.Call("browser.info", Json::object(), std::min<DWORD>(Remaining(deadline), 500));
            if (info.is_object() && info.value("ready", false)) return browser;
        } catch (const Error&) {}
        Sleep(50);
    }
    throw Error("launch_timeout", "The browser did not become ready; its window may still be open");
}
Browser Browser::Connect(const std::filesystem::path& endpoint) {
    if (!endpoint.empty()) {
        Browser browser(Detail::Read(endpoint));
        browser.Call("browser.info");
        return browser;
    }
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) throw Error("browser_not_running", "LOCALAPPDATA is unavailable; supply an endpoint file");
    const auto folder = std::filesystem::path(local) / L"TitanCSA/BrowserControl/instances";
    std::vector<std::filesystem::directory_entry> paths;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == L".json") paths.push_back(entry);
    }
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.last_write_time() > b.last_write_time(); });
    const auto deadline = GetTickCount64() + 1500;
    if (paths.size() > 128) paths.resize(128);
    for (const auto& entry : paths) try {
        if (GetTickCount64() >= deadline) break;
        Browser browser(Detail::Read(entry.path()));
        browser.Call("browser.info", Json::object(), std::min<DWORD>(Remaining(deadline), 500));
        return browser;
    } catch (const Error&) {}
    throw Error("browser_not_running", "Start BrowserControl.exe before connecting the SDK");
}
Json Browser::Call(const std::string& method, Json params, unsigned timeoutMs) const { return Detail::Call(connection_, method, std::move(params), timeoutMs); }
Json Browser::Info() const { return Call("browser.info"); }
Json Browser::Capabilities() const { return Call("browser.capabilities"); }
Json Browser::Tabs() const { return Call("tabs.list"); }
Tab Browser::NewTab(Json options) const { const auto value = Call("tabs.create", std::move(options)); return Tab(connection_, value.at("id").get<std::string>()); }
Tab Browser::GetTab(std::string id) const { return Tab(connection_, std::move(id)); }
Json Browser::Profiles() const { return Call("profiles.list"); }
Json Browser::CreateProfile(const std::string& name, bool inPrivate) const { return Call("profiles.create", {{"name", name}, {"in_private", inPrivate}}); }
Json Browser::SavedLogins(Json options) const { return Call("saved_logins.list", std::move(options)); }
Json Browser::DeleteSavedLogin(const std::string& credentialId) const { return Call("saved_logins.delete", {{"credential_id", credentialId}}); }
Json Browser::SetSavedLoginAutofill(const std::string& credentialId, bool enabled) const { return Call("saved_logins.autofill", {{"credential_id", credentialId}, {"enabled", enabled}}); }
Json Browser::Window(Json options) const { return options.is_null() ? Call("window.info") : Call("window.set", std::move(options)); }
Json Browser::Events(unsigned long long since, unsigned limit) const { return Call("events.read", {{"since", since}, {"limit", limit}}); }
Json Browser::Shutdown() const { return Call("browser.shutdown"); }
Tab::Tab(std::shared_ptr<Detail::Connection> connection, std::string id) : connection_(std::move(connection)), id_(std::move(id)) { if (id_.empty()) throw std::invalid_argument("tab id must not be empty"); }
Json Tab::Call(const std::string& method, Json params, unsigned timeoutMs) const { Object(params); params["tab"] = id_; return Detail::Call(connection_, method, std::move(params), timeoutMs); }
Json Tab::Goto(const std::string& url, const std::string& wait, unsigned timeoutMs) const { return Call("page.navigate", {{"url", url}, {"wait", wait}, {"timeout_ms", timeoutMs}}, timeoutMs); }
Json Tab::Info() const { return Call("page.info"); }
Json Tab::Activate() const { return Call("tabs.activate"); }
Json Tab::Close() const { return Call("tabs.close"); }
Json Tab::Snapshot() const { return Call("page.snapshot"); }
Json Tab::Observe(Json options) const { return Call("page.observe", std::move(options)); }
Json Tab::Act(Json actions, bool observe, unsigned timeoutMs, Json guards) const {
    Object(guards);
    for (const auto& item : guards.items())
        if ((item.key() != "expected_generation" && item.key() != "expected_url") ||
            (item.key() == "expected_generation" && !item.value().is_number_unsigned() &&
             (!item.value().is_number_integer() || item.value().get<long long>() < 0)) ||
            (item.key() == "expected_url" && !item.value().is_string()))
            throw std::invalid_argument("Only expected_generation (unsigned) and expected_url (string) guards are accepted");
    if (!actions.is_array() || actions.empty() || actions.size() > 20)
        throw std::invalid_argument("actions must contain between 1 and 20 action objects");
    for (const auto& action : actions) {
        if (!action.is_object() || !action.contains("action") || !action["action"].is_string())
            throw std::invalid_argument("Each action must have a supported action name");
        const auto name = action["action"].get<std::string>();
        if (name != "click" && name != "fill" && name != "type" && name != "key" && name != "mouse" &&
            name != "scroll" && name != "wait" && name != "navigate" && name != "observe")
            throw std::invalid_argument("Each action must have a supported action name");
    }
    guards["actions"] = std::move(actions);
    guards["observe"] = observe;
    return Call("page.act", std::move(guards), timeoutMs);
}
Json Tab::Login(const std::string& operation, Json options) const {
    Object(options);
    if (operation != "status" && operation != "fill") throw std::invalid_argument("operation must be status or fill");
    options["operation"] = operation;
    return Call("page.login", std::move(options));
}
Json Tab::Click(Json target) const { return Call("page.click", std::move(target)); }
Json Tab::Fill(Json target, const std::string& text) const { Object(target); target["text"] = text; return Call("page.fill", std::move(target)); }
Json Tab::Type(const std::string& text) const { return Call("page.type", {{"text", text}}); }
Json Tab::Press(const std::string& key, const std::string& action) const { return Call("page.key", {{"key", key}, {"action", action}}); }
Json Tab::Mouse(const std::string& type, double x, double y, Json options) const { Object(options); options["type"] = type; options["x"] = x; options["y"] = y; return Call("page.mouse", std::move(options)); }
Json Tab::Scroll(double deltaY, double deltaX) const { return Call("page.scroll", {{"delta_y", deltaY}, {"delta_x", deltaX}}); }
Json Tab::Screenshot(Json options) const { return Call("page.screenshot", std::move(options)); }
Json Tab::Evaluate(const std::string& expression, bool awaitPromise, const std::string& world) const { const auto value = Call("page.evaluate", {{"expression", expression}, {"await_promise", awaitPromise}, {"world", world}}); if (value.is_object()) { if (value.contains("value")) return value["value"]; if (value.value("undefined", false)) return Json(); } return value; }
Json Tab::Wait(Json target, const std::string& state, unsigned timeoutMs) const { Object(target); target["state"] = state; target["timeout_ms"] = timeoutMs; return Call("page.wait", std::move(target), timeoutMs); }
Json Tab::Viewport(unsigned width, unsigned height, double scale) const { return Call("page.viewport", {{"width", width}, {"height", height}, {"device_scale_factor", scale}}); }
Json Tab::Settings(Json settings) const { return Call("page.settings", {{"settings", std::move(settings)}}); }
Json Tab::Cookies(const std::string& operation, Json options) const { Object(options); options["operation"] = operation; return Call("page.cookies", std::move(options)); }
Json Tab::Storage(const std::string& operation, Json options) const { Object(options); options["operation"] = operation; return Call("page.storage", std::move(options)); }
Json Tab::Cdp(const std::string& method, Json params) const { return Call("page.cdp", {{"method", method}, {"params", std::move(params)}}); }
}
