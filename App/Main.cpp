#include "Host.h"
#include "Ipc.h"
#include <unknwn.h>
#include <WebView2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <string>

namespace {
struct ProfileLock {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~ProfileLock() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void WriteText(DWORD destination, const std::wstring& text) {
    const HANDLE handle = GetStdHandle(destination);
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0, written = 0;
    if (GetConsoleMode(handle, &mode)) {
        WriteConsoleW(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(bytes), '\0');
        if (bytes) {
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr, nullptr);
            WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    using namespace BrowserControl;
    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring endpoint, profileRoot;
    bool help = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg(argv[index]);
        if (arg == L"--help" || arg == L"--version") help = true;
        else if ((arg == L"--endpoint" || arg == L"--profile-root") && index + 1 < argc) {
            (arg == L"--endpoint" ? endpoint : profileRoot) = argv[++index];
        } else { LocalFree(argv); WriteText(STD_ERROR_HANDLE, L"Unknown or incomplete argument. Use --help.\n"); return 2; }
    }
    LocalFree(argv);
    if (help) {
        const auto message = L"Browser Control SDK 0.2.0\n\nBrowserControl.exe [--profile-root PATH] [--endpoint PATH]\n\nWindows x64; WebView2 Evergreen Runtime required.\nAutomation is local to this browser instance and requires its private endpoint file.";
        if (!GetStdHandle(STD_OUTPUT_HANDLE) || GetStdHandle(STD_OUTPUT_HANDLE) == INVALID_HANDLE_VALUE)
            AttachConsole(ATTACH_PARENT_PROCESS);
        WriteText(STD_OUTPUT_HANDLE, std::wstring(message) + L"\n");
        return 0;
    }
    if (profileRoot.empty()) {
        PWSTR local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) return 1;
        profileRoot = (std::filesystem::path(local) / L"TitanCSA" / L"BrowserControl" / L"Profiles").native();
        CoTaskMemFree(local);
    }
    std::wstring privateDirectoryError;
    if (!PreparePrivateDirectory(profileRoot, privateDirectoryError)) {
        if (!endpoint.empty()) WriteText(STD_ERROR_HANDLE, privateDirectoryError + L"\n");
        else MessageBoxW(nullptr, privateDirectoryError.c_str(), L"Browser profile unavailable", MB_OK | MB_ICONERROR);
        return 4;
    }
    // One writer per data root keeps profile metadata and runtime state coherent.
    ProfileLock profileLock;
    const auto lockPath = std::filesystem::path(profileRoot) / L".browser-instance.lock";
    profileLock.value = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (profileLock.value == INVALID_HANDLE_VALUE) {
        const std::wstring message = L"The browser data folder is unavailable or already in use. Connect to its running instance, or choose another private --profile-root.\n";
        if (!endpoint.empty()) WriteText(STD_ERROR_HANDLE, message);
        else MessageBoxW(nullptr, message.c_str(), L"Browser profile already in use", MB_OK | MB_ICONERROR);
        return 5;
    }
    LPWSTR runtime = nullptr;
    const HRESULT available = GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtime);
    if (runtime) CoTaskMemFree(runtime);
    if (FAILED(available)) {
        if (!endpoint.empty()) {
            WriteText(STD_ERROR_HANDLE, L"Microsoft Edge WebView2 Runtime is not installed. Download it from https://developer.microsoft.com/microsoft-edge/webview2/\n");
            return 3;
        }
        if (MessageBoxW(nullptr, L"Microsoft Edge WebView2 Runtime is not installed.\n\nOpen Microsoft's official Runtime download page? No software will be installed automatically.",
            L"Browser Control SDK - runtime required", MB_YESNO | MB_ICONINFORMATION) == IDYES)
            ShellExecuteW(nullptr, L"open", L"https://developer.microsoft.com/microsoft-edge/webview2/", nullptr, nullptr, SW_SHOWNORMAL);
        return 3;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    int result = 1;
    {
        Host host;
        IpcServer server;
        if (host.Create(instance, profileRoot, show)) {
            std::wstring error;
            if (server.Start(host.Window(), endpoint, profileRoot, error)) {
                host.SetEndpoint(server.EndpointPath());
                MSG message{};
                BOOL received = 0;
                while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                    TranslateMessage(&message); DispatchMessageW(&message);
                }
                const int exitCode = received < 0 ? 1 : static_cast<int>(message.wParam);
                server.Stop();
                while (PeekMessageW(&message, nullptr, RpcMessage, RpcMessage, PM_REMOVE)) {
                    auto* envelope = reinterpret_cast<Envelope*>(message.lParam);
                    if (envelope) { if (envelope->canceled) envelope->canceled->store(true); delete envelope; }
                }
                result = exitCode;
            } else {
                if (!endpoint.empty()) WriteText(STD_ERROR_HANDLE, error + L"\n");
                else MessageBoxW(host.Window(), error.c_str(), L"Browser SDK channel unavailable", MB_OK | MB_ICONERROR);
                DestroyWindow(host.Window());
            }
        }
    }
    CoUninitialize();
    return result;
}
