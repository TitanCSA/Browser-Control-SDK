#include "Ipc.h"
#include <sddl.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace BrowserControl {
namespace {
constexpr DWORD MaxRequest = 1024 * 1024;
constexpr DWORD MaxResponse = 64 * 1024 * 1024;
constexpr unsigned Workers = 4;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid Unicode path");
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}
std::wstring UserSid() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
        throw std::runtime_error("Cannot read the current Windows account");
    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    if (!bytes || !GetTokenInformation(token.value, TokenUser, storage.data(), bytes, &bytes))
        throw std::runtime_error("Cannot read the current Windows SID");
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &text))
        throw std::runtime_error("Cannot format the current Windows SID");
    std::wstring result(text);
    LocalFree(text);
    return result;
}
struct Descriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    explicit Descriptor(const std::wstring& sid) {
        const auto sddl = L"O:" + sid + L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;" + sid + L")S:(ML;;NW;;;ME)";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
            &value, nullptr)) throw std::runtime_error("Cannot create the private Windows ACL");
    }
    ~Descriptor() { if (value) LocalFree(value); }
};
std::string RandomHex(size_t bytes) {
    std::vector<BYTE> data(bytes);
    if (BCryptGenRandom(nullptr, data.data(), static_cast<ULONG>(data.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("Secure random generation failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result(bytes * 2, '0');
    for (size_t i = 0; i < bytes; ++i) {
        result[2 * i] = hex[data[i] >> 4]; result[2 * i + 1] = hex[data[i] & 15];
    }
    return result;
}
bool SameToken(const std::string& actual, const std::string& expected) {
    if (actual.size() != expected.size()) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < expected.size(); ++i)
        difference |= static_cast<unsigned char>(actual[i]) ^ static_cast<unsigned char>(expected[i]);
    return difference == 0;
}
std::filesystem::path LocalRoot() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path)))
        throw std::runtime_error("Cannot locate Local AppData");
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result / L"TitanCSA" / L"BrowserControl";
}
bool PrivateEnoughDirectory(const std::filesystem::path& path, const std::wstring& user) {
    PACL acl = nullptr; PSID owner = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION, &owner,
        nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS) return false;
    bool okay = acl != nullptr;
    PSID userSid = nullptr;
    if (!ConvertStringSidToSidW(user.c_str(), &userSid)) okay = false;
    BYTE systemStorage[SECURITY_MAX_SID_SIZE]{}, adminStorage[SECURITY_MAX_SID_SIZE]{};
    DWORD systemSize = sizeof(systemStorage), adminSize = sizeof(adminStorage);
    CreateWellKnownSid(WinLocalSystemSid, nullptr, systemStorage, &systemSize);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminStorage, &adminSize);
    if (okay && (!owner || (!EqualSid(owner, userSid) && !EqualSid(owner, systemStorage) &&
                           !EqualSid(owner, adminStorage)))) okay = false;
    if (okay) for (DWORD index = 0; index < acl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(acl, index, &raw)) { okay = false; break; }
        auto* header = static_cast<ACE_HEADER*>(raw);
        if (header->AceFlags & INHERIT_ONLY_ACE) continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { okay = false; break; }
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        const DWORD write = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD |
                            DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
        if (!(ace->Mask & write)) continue;
        PSID sid = reinterpret_cast<PSID>(&ace->SidStart);
        if (!EqualSid(sid, userSid) && !EqualSid(sid, systemStorage) && !EqualSid(sid, adminStorage)) {
            okay = false; break;
        }
    }
    if (userSid) LocalFree(userSid);
    if (descriptor) LocalFree(descriptor);
    return okay;
}
std::filesystem::path ResolveLocalDirectory(const std::filesystem::path& path) {
    Handle directory(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (directory.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open the private data directory");
    const DWORD count = GetFinalPathNameByHandleW(directory.value, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count > 32768) throw std::runtime_error("Cannot resolve the private data directory");
    std::wstring finalPath(count, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(directory.value, finalPath.data(), count,
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= count) throw std::runtime_error("Cannot resolve the private data directory");
    finalPath.resize(written);
    if (finalPath.rfind(L"\\\\?\\", 0) == 0) finalPath.erase(0, 4);
    if (finalPath.size() < 3 || finalPath[1] != L':' || finalPath[2] != L'\\')
        throw std::runtime_error("Browser data and endpoint files must be on a local drive");
    const std::wstring volume = finalPath.substr(0, 3);
    const UINT drive = GetDriveTypeW(volume.c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE)
        throw std::runtime_error("Browser data and endpoint files must be on a local drive");
    return std::filesystem::path(finalPath);
}
bool Transfer(HANDLE pipe, HANDLE stop, void* data, DWORD size, bool write, ULONGLONG deadline) {
    DWORD position = 0;
    while (position < size) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) return false;
        OVERLAPPED overlapped{}; overlapped.hEvent = event.value;
        DWORD done = 0;
        const DWORD chunk = (std::min)(size - position, DWORD{65536});
        const BOOL immediate = write ? WriteFile(pipe, static_cast<BYTE*>(data) + position,
            chunk, &done, &overlapped) : ReadFile(pipe, static_cast<BYTE*>(data) + position,
            chunk, &done, &overlapped);
        if (!immediate) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            const auto now = GetTickCount64();
            const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>((std::min)(deadline - now, ULONGLONG{120000}));
            const HANDLE waits[] = {stop, event.value};
            if (WaitForMultipleObjects(2, waits, FALSE, remaining) != WAIT_OBJECT_0 + 1) {
                CancelIoEx(pipe, &overlapped);
                GetOverlappedResult(pipe, &overlapped, &done, TRUE);
                return false;
            }
            if (!GetOverlappedResult(pipe, &overlapped, &done, FALSE)) return false;
        }
        if (!done) return false;
        position += done;
    }
    return true;
}
struct Pending {
    Handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::mutex mutex;
    Json response;
    bool replied = false;
    std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::atomic_uint> phase = std::make_shared<std::atomic_uint>(0);
};
}

bool PreparePrivateDirectory(std::wstring& path, std::wstring& error) noexcept {
    try {
        const auto user = UserSid();
        Descriptor descriptor(user);
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor.value, FALSE};
        auto wanted = std::filesystem::absolute(path).lexically_normal();
        const auto absoluteText = wanted.native();
        if (absoluteText.size() < 3 || absoluteText[1] != L':' || absoluteText[2] != L'\\')
            throw std::runtime_error("Browser data and endpoint files must be on a local drive");
        const auto initialVolume = absoluteText.substr(0, 3);
        const UINT initialDrive = GetDriveTypeW(initialVolume.c_str());
        if (initialDrive != DRIVE_FIXED && initialDrive != DRIVE_REMOVABLE)
            throw std::runtime_error("Browser data and endpoint files must be on a local drive");
        auto ancestor = wanted;
        std::vector<std::filesystem::path> missing;
        while (!std::filesystem::exists(ancestor)) {
            const auto parent = ancestor.parent_path();
            if (parent.empty() || parent == ancestor) throw std::runtime_error("The data directory has no existing local parent");
            missing.push_back(ancestor.filename()); ancestor = parent;
        }
        ancestor = ResolveLocalDirectory(ancestor);
        if (!PrivateEnoughDirectory(ancestor, user))
            throw std::runtime_error("The data directory is writable or owned by another account; choose a private folder");
        for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
            ancestor /= *it;
            if (!CreateDirectoryW(ancestor.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Cannot create the private browser directory");
            ancestor = ResolveLocalDirectory(ancestor);
            if (!PrivateEnoughDirectory(ancestor, user))
                throw std::runtime_error("A data directory has unsafe permissions");
        }
        path = ancestor.native();
        return true;
    } catch (const std::exception& caught) {
        const std::string message(caught.what()); error.assign(message.begin(), message.end());
    } catch (...) { error = L"Cannot prepare the private local directory."; }
    return false;
}

struct IpcServer::Impl {
    HWND target = nullptr;
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle endpointHandle;
    std::wstring endpoint;
    std::wstring pipeName;
    std::string token;
    std::array<HANDLE, Workers> pipes{};
    std::vector<std::thread> workers;
    bool started = false;

    Json HandleRequest(const std::string& raw, bool& shutdown) {
        Json request;
        try {
            request = Json::parse(raw, [](int depth, Json::parse_event_t, Json&) {
                if (depth > 64) throw std::runtime_error("JSON depth");
                return true;
            });
        } catch (...) { return Fail("invalid_json", "Request must be valid UTF-8 JSON with depth at most 64."); }
        if (!request.is_object()) return Fail("invalid_request", "The request must be an object.");
        Json id = request.contains("id") ? request["id"] : Json();
        const bool validId = (id.is_string() && id.get_ref<const std::string&>().size() <= 128) || id.is_number_integer();
        auto finish = [&](Json response) { response["id"] = validId ? id : Json(); return response; };
        if (!request.contains("token") || !request["token"].is_string() ||
            !SameToken(request["token"].get_ref<const std::string&>(), token))
            return finish(Fail("unauthorized", "The instance capability token is missing or invalid."));
        if (!validId || !request.contains("version") || !request["version"].is_number_integer() ||
            request["version"] != 1 || !request.contains("method") || !request["method"].is_string() ||
            request["method"].get_ref<const std::string&>().empty() ||
            request["method"].get_ref<const std::string&>().size() > 100 ||
            (request.contains("params") && !request["params"].is_object()))
            return finish(Fail("invalid_request", "Use protocol version 1, an ID, a method string and object params."));
        DWORD timeout = 15000;
        if (request.contains("timeout_ms")) {
            if (!request["timeout_ms"].is_number_integer())
                return finish(Fail("invalid_timeout", "timeout_ms must be an integer from 1 to 120000."));
            const auto value = request["timeout_ms"].get<int64_t>();
            if (value < 1 || value > 120000)
                return finish(Fail("invalid_timeout", "timeout_ms must be an integer from 1 to 120000."));
            timeout = static_cast<DWORD>(value);
        }
        request["timeout_ms"] = timeout;
        if (!request.contains("params")) request["params"] = Json::object();
        if (request["method"] == "browser.shutdown") {
            shutdown = true;
            return finish(Ok({{"closing", true}}));
        }
        request.erase("token");
        auto pending = std::make_shared<Pending>();
        if (!pending->done.value) return finish(Fail("internal_error", "Cannot allocate a completion event."));
        auto envelope = std::make_unique<Envelope>();
        envelope->request = std::move(request);
        envelope->canceled = pending->canceled;
        envelope->phase = pending->phase;
        envelope->reply = [pending](Json response) {
            std::lock_guard<std::mutex> guard(pending->mutex);
            if (pending->replied || pending->canceled->load()) return;
            pending->response = std::move(response); pending->replied = true;
            SetEvent(pending->done.value);
        };
        if (!PostMessageW(target, RpcMessage, 0, reinterpret_cast<LPARAM>(envelope.get())))
            return finish(Fail("browser_closed", "The browser window is unavailable."));
        envelope.release();
        const HANDLE waits[] = {stop.value, pending->done.value};
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, timeout);
        if (wait != WAIT_OBJECT_0 + 1) {
            unsigned queued = 0;
            const bool skipped = pending->phase->compare_exchange_strong(queued, 2);
            pending->canceled->store(true);
            return finish(Fail(wait == WAIT_TIMEOUT ? "timeout" : "browser_closed",
                wait == WAIT_TIMEOUT ? "The browser command timed out; inspect may_have_executed before deciding what to do next." : "The browser instance is closing.",
                {{"may_have_executed", !skipped}}));
        }
        std::lock_guard<std::mutex> guard(pending->mutex);
        return finish(pending->response);
    }

    void Serve(unsigned index) noexcept {
        const HANDLE pipe = pipes[index];
        try {
            for (;;) {
                if (WaitForSingleObject(stop.value, 0) == WAIT_OBJECT_0) break;
                Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                if (!event.value) break;
                OVERLAPPED overlapped{}; overlapped.hEvent = event.value;
                BOOL connected = ConnectNamedPipe(pipe, &overlapped);
                if (!connected) {
                    const DWORD error = GetLastError();
                    if (error == ERROR_PIPE_CONNECTED) connected = TRUE;
                    else if (error == ERROR_IO_PENDING) {
                        const HANDLE waits[] = {stop.value, event.value};
                        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                            CancelIoEx(pipe, &overlapped); DWORD ignored = 0;
                            GetOverlappedResult(pipe, &overlapped, &ignored, TRUE); break;
                        }
                        DWORD ignored = 0;
                        connected = GetOverlappedResult(pipe, &overlapped, &ignored, FALSE);
                    }
                }
                if (!connected) { DisconnectNamedPipe(pipe); continue; }
                DWORD bytes = 0;
                const auto deadline = GetTickCount64() + 5000;
                if (Transfer(pipe, stop.value, &bytes, sizeof(bytes), false, deadline) && bytes && bytes <= MaxRequest) {
                    std::string raw(bytes, '\0');
                    if (Transfer(pipe, stop.value, raw.data(), bytes, false, deadline)) {
                        Json response;
                        bool shutdown = false;
                        try { response = HandleRequest(raw, shutdown); }
                        catch (...) { response = Fail("internal_error", "The request could not be completed safely."); }
                        auto text = response.dump(-1, ' ', false, Json::error_handler_t::replace);
                        if (text.size() > MaxResponse) text = Fail("response_too_large", "The response exceeds the 64 MiB limit.").dump();
                        bytes = static_cast<DWORD>(text.size());
                        const auto writeDeadline = GetTickCount64() + 5000;
                        if (Transfer(pipe, stop.value, &bytes, sizeof(bytes), true, writeDeadline) &&
                            Transfer(pipe, stop.value, text.data(), bytes, true, writeDeadline)) {
                            // A completed write can still reside in the pipe buffer. Keep the
                            // connection until the client consumes the response and closes it.
                            BYTE acknowledgement = 0;
                            Transfer(pipe, stop.value, &acknowledgement, 1, false, GetTickCount64() + 5000);
                            if (shutdown) PostMessageW(target, WM_CLOSE, 0, 0);
                        }
                    }
                }
                DisconnectNamedPipe(pipe);
            }
        } catch (...) { /* The process and other bounded workers remain available. */ }
    }
};

IpcServer::IpcServer() : impl_(std::make_unique<Impl>()) {}
IpcServer::~IpcServer() { Stop(); }
std::wstring IpcServer::EndpointPath() const { return impl_->endpoint; }

bool IpcServer::Start(HWND target, const std::wstring& requested, const std::wstring& profileRoot,
                      std::wstring& error) noexcept {
    try {
        if (impl_->started || !IsWindow(target)) throw std::runtime_error("Invalid browser window or server already started");
        const auto sid = UserSid();
        Descriptor descriptor(sid);
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor.value, FALSE};
        const auto instance = RandomHex(16);
        const std::wstring wideInstance(instance.begin(), instance.end());
        const auto defaultFolder = LocalRoot() / L"instances";
        std::filesystem::path endpoint = requested.empty() ? defaultFolder / (wideInstance + L".json") : std::filesystem::absolute(requested);
        if (!endpoint.has_parent_path() || endpoint.filename().empty()) throw std::runtime_error("Choose a private endpoint file path");
        if (endpoint.native().rfind(L"\\\\", 0) == 0) throw std::runtime_error("Endpoint files must be on a local drive");
        auto parent = endpoint.parent_path().native();
        std::wstring directoryError;
        if (!PreparePrivateDirectory(parent, directoryError)) throw std::runtime_error(Utf8(directoryError));
        const auto filename = endpoint.filename().native();
        if (filename.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos ||
            filename.back() == L'.' || filename.back() == L' ')
            throw std::runtime_error("Choose a regular endpoint JSON filename");
        auto stem = filename.substr(0, filename.find(L'.'));
        CharUpperBuffW(stem.data(), static_cast<DWORD>(stem.size()));
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
            stem == L"CONIN$" || stem == L"CONOUT$" ||
            (stem.size() == 4 && (stem.rfind(L"COM",0) == 0 || stem.rfind(L"LPT",0) == 0) && stem[3] >= L'1' && stem[3] <= L'9'))
            throw std::runtime_error("Reserved Windows device names cannot be endpoint files");
        endpoint = std::filesystem::path(parent) / filename;
        impl_->endpointHandle.value = CreateFileW(endpoint.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (impl_->endpointHandle.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create a private endpoint file; the path may already be in use");
        impl_->endpoint = endpoint.native();
        impl_->token = RandomHex(32);
        impl_->pipeName = L"\\\\.\\pipe\\BrowserControl-" + wideInstance;
        impl_->target = target;
        for (unsigned index = 0; index < Workers; ++index) {
            impl_->pipes[index] = CreateNamedPipeW(impl_->pipeName.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (index == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                Workers, 16384, 16384, 0, &attributes);
            if (impl_->pipes[index] == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create the private local named pipe");
        }
        for (unsigned index = 0; index < Workers; ++index) impl_->workers.emplace_back([this, index] { impl_->Serve(index); });
        const Json information{{"version",1},{"pipe",Utf8(impl_->pipeName)},{"token",impl_->token},
            {"pid",GetCurrentProcessId()},{"instance",instance},{"profile_root",Utf8(profileRoot)}};
        const auto text = information.dump(2) + "\n";
        DWORD written = 0;
        if (!WriteFile(impl_->endpointHandle.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
            written != text.size() || !FlushFileBuffers(impl_->endpointHandle.value))
            throw std::runtime_error("Cannot finish the private endpoint file");
        impl_->started = true;
        return true;
    } catch (const std::exception& caught) {
        const std::string message(caught.what());
        error.assign(message.begin(), message.end());
    } catch (...) { error = L"Cannot start the local browser SDK channel."; }
    Stop(); return false;
}

void IpcServer::Stop() noexcept {
    if (!impl_) return;
    SetEvent(impl_->stop.value);
    for (const auto pipe : impl_->pipes) if (pipe && pipe != INVALID_HANDLE_VALUE) CancelIoEx(pipe, nullptr);
    for (auto& worker : impl_->workers) if (worker.joinable()) worker.join();
    impl_->workers.clear();
    for (auto& pipe : impl_->pipes) {
        if (pipe && pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }
    if (impl_->endpointHandle.value != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->endpointHandle.value); impl_->endpointHandle.value = INVALID_HANDLE_VALUE;
    }
    if (!impl_->endpoint.empty()) { DeleteFileW(impl_->endpoint.c_str()); impl_->endpoint.clear(); }
    impl_->started = false;
}
}
