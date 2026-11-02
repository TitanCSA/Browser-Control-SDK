#include "CredentialVault.h"
#include "Ipc.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <sddl.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace BrowserControl {
namespace {
constexpr size_t MaxFile = 1024 * 1024;
constexpr size_t MaxRecords = 100;
constexpr size_t MaxPassword = 4096;
constexpr size_t MaxUsername = 512;
constexpr size_t MaxOrigin = 2048;
constexpr std::array<BYTE, 8> Magic{'B','C','V','A','U','L','T','1'};
constexpr BYTE Entropy[] = "BrowserControl credential storage v1";

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    void Close() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } }
};
struct SecretText {
    std::string value;
    ~SecretText() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); }
};
struct SecretWide {
    std::wstring value;
    ~SecretWide() { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); }
};
struct LocalBlob {
    DATA_BLOB value{};
    ~LocalBlob() { if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); } }
};
void Wipe(Json& value) noexcept {
    if (value.is_string()) {
        auto& text = value.get_ref<std::string&>();
        if (!text.empty()) SecureZeroMemory(text.data(), text.size());
    } else if (value.is_array() || value.is_object()) {
        for (auto& item : value) Wipe(item);
    }
}
struct Document {
    Json value = {{"version", 1}, {"records", Json::array()}};
    ~Document() { Wipe(value); }
};
std::wstring Wide(const std::string& input) {
    if (input.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                       static_cast<int>(input.size()), nullptr, 0);
    if (!size) throw VaultError("invalid_credential", "Credential fields must contain valid UTF-8 text");
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), result.data(), size);
    return result;
}
void Profile(const std::string& profile) {
    if (profile.empty() || profile.size() > 64 || !std::all_of(profile.begin(), profile.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-';
    })) throw VaultError("invalid_profile", "Use a valid 1-64 character browser profile name");
}
void Field(const std::string& text, size_t maximum, bool required) {
    if ((required && text.empty()) || text.size() > maximum || text.find('\0') != std::string::npos)
        throw VaultError("invalid_credential", "A credential field is empty, too long or contains a null character");
    SecretWide checked{Wide(text)};
}
std::string RandomId() {
    std::array<BYTE, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw VaultError("vault_unavailable", "Windows could not create a secure credential identifier");
    constexpr char hex[] = "0123456789abcdef";
    std::string value(bytes.size() * 2, '0');
    for (size_t index = 0; index < bytes.size(); ++index) {
        value[index * 2] = hex[bytes[index] >> 4]; value[index * 2 + 1] = hex[bytes[index] & 15];
    }
    return value;
}
struct FileSecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    FileSecurity() {
        Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
            throw VaultError("vault_unavailable", "Windows could not identify the current account", GetLastError());
        DWORD size = 0; GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> bytes(size);
        if (!size || !GetTokenInformation(token.value, TokenUser, bytes.data(), size, &size))
            throw VaultError("vault_unavailable", "Windows could not identify the current account", GetLastError());
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid))
            throw VaultError("vault_unavailable", "Windows could not format the current account", GetLastError());
        const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid + L")S:(ML;;NW;;;ME)";
        LocalFree(sid);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
            throw VaultError("vault_unavailable", "Windows could not protect credential storage", GetLastError());
    }
    ~FileSecurity() { if (descriptor) LocalFree(descriptor); }
};
Json Metadata(const Json& record) {
    return {{"id", record.at("id")}, {"profile", record.at("profile")}, {"origin", record.at("origin")},
            {"username", record.at("username")}, {"auto_fill", record.at("auto_fill")}};
}
bool Match(const Json& record, const std::string& profile, const std::string& origin, const std::string& id) {
    return record["profile"] == profile && record["origin"] == origin && record["id"] == id;
}
void Validate(Document& document) {
    const auto& data = document.value;
    if (!data.is_object() || data.size() != 2 || data.value("version", Json()) != 1 ||
        !data.contains("records") || !data["records"].is_array() || data["records"].size() > MaxRecords)
        throw VaultError("vault_corrupt", "Saved credential storage has an unsupported or damaged structure");
    std::vector<std::string> identifiers;
    for (const auto& record : data["records"]) {
        if (!record.is_object() || record.size() != 6 || !record.contains("auto_fill") || !record["auto_fill"].is_boolean())
            throw VaultError("vault_corrupt", "Saved credential storage contains an invalid record");
        const auto& id = record.at("id").get_ref<const std::string&>();
        const auto& profile = record.at("profile").get_ref<const std::string&>();
        const auto& origin = record.at("origin").get_ref<const std::string&>();
        const auto& username = record.at("username").get_ref<const std::string&>();
        const auto& password = record.at("password").get_ref<const std::string&>();
        if (id.size() != 32 || !std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }) || std::find(identifiers.begin(), identifiers.end(), id) != identifiers.end())
            throw VaultError("vault_corrupt", "Saved credential storage contains an invalid identifier");
        identifiers.push_back(id);
        Profile(profile); Field(username, MaxUsername, false); Field(password, MaxPassword, true);
        if (CredentialVault::NormalizeOrigin(origin) != origin)
            throw VaultError("vault_corrupt", "Saved credential storage contains a noncanonical origin");
    }
}
std::unique_ptr<Document> Load(const std::filesystem::path& path) {
    auto document = std::make_unique<Document>();
    Handle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return document;
        throw VaultError("vault_unavailable", "Saved credential storage could not be opened", error);
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file.value, &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        throw VaultError("vault_unavailable", "Saved credential storage is not a regular local file");
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.value, &length) || length.QuadPart <= static_cast<LONGLONG>(Magic.size()) ||
        length.QuadPart > static_cast<LONGLONG>(MaxFile))
        throw VaultError("vault_corrupt", "Saved credential storage has an invalid size");
    std::vector<BYTE> ciphertext(static_cast<size_t>(length.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.value, ciphertext.data(), static_cast<DWORD>(ciphertext.size()), &read, nullptr) || read != ciphertext.size())
        throw VaultError("vault_unavailable", "Saved credential storage could not be read", GetLastError());
    if (!std::equal(Magic.begin(), Magic.end(), ciphertext.begin()))
        throw VaultError("vault_corrupt", "Saved credential storage has an unsupported format");
    DATA_BLOB protectedData{static_cast<DWORD>(ciphertext.size() - Magic.size()), ciphertext.data() + Magic.size()};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(Entropy)), const_cast<BYTE*>(Entropy)};
    LocalBlob plaintext;
    if (!CryptUnprotectData(&protectedData, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plaintext.value))
        throw VaultError("vault_decryption_failed", "Windows could not unlock saved credentials for this account; storage may be damaged or belong to another Windows account", GetLastError());
    if (!plaintext.value.cbData || plaintext.value.cbData > MaxFile)
        throw VaultError("vault_corrupt", "Decrypted credential storage exceeds its supported size");
    try {
        bool tooDeep = false;
        auto bounded = [&tooDeep](int depth, Json::parse_event_t, Json&) {
            if (depth > 8) tooDeep = true;
            return !tooDeep;
        };
        document->value = Json::parse(plaintext.value.pbData, plaintext.value.pbData + plaintext.value.cbData, bounded, false);
        if (tooDeep || document->value.is_discarded()) throw std::runtime_error("Invalid JSON");
        Validate(*document);
    } catch (...) {
        throw VaultError("vault_corrupt", "Saved credential storage is damaged; it has not been reset or overwritten");
    }
    return document;
}
void Save(const std::filesystem::path& path, const Document& document) {
    SecretText plaintext{document.value.dump()};
    if (plaintext.value.size() > MaxFile)
        throw VaultError("vault_full", "Saved credential storage exceeds its supported size");
    DATA_BLOB input{static_cast<DWORD>(plaintext.value.size()), reinterpret_cast<BYTE*>(plaintext.value.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(Entropy)), const_cast<BYTE*>(Entropy)};
    LocalBlob encrypted;
    if (!CryptProtectData(&input, L"Browser Control saved credentials", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted.value))
        throw VaultError("vault_unavailable", "Windows could not encrypt saved credentials", GetLastError());
    if (encrypted.value.cbData > MaxFile - Magic.size())
        throw VaultError("vault_full", "Encrypted credential storage exceeds its supported size");
    std::vector<BYTE> bytes(Magic.begin(), Magic.end());
    bytes.insert(bytes.end(), encrypted.value.pbData, encrypted.value.pbData + encrypted.value.cbData);
    const auto temporary = path.parent_path() / (L".credentials-" + std::filesystem::path(RandomId()).wstring() + L".tmp");
    FileSecurity security;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), security.descriptor, FALSE};
    Handle file;
    file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW,
                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    if (file.value == INVALID_HANDLE_VALUE)
        throw VaultError("vault_unavailable", "Windows could not create encrypted credential storage", GetLastError());
    try {
        DWORD written = 0;
        if (!WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.value))
            throw VaultError("vault_unavailable", "Windows could not finish encrypted credential storage", GetLastError());
        file.Close();
        const DWORD existing = GetFileAttributesW(path.c_str());
        if (existing != INVALID_FILE_ATTRIBUTES && (existing & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
            throw VaultError("vault_unavailable", "Saved credential storage is not a regular local file");
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw VaultError("vault_unavailable", "Windows could not atomically replace saved credential storage", GetLastError());
    } catch (...) {
        file.Close(); DeleteFileW(temporary.c_str()); throw;
    }
}
}

VaultError::VaultError(std::string value, std::string message, DWORD windowsError)
    : std::runtime_error(std::move(message)), code(std::move(value)), windows_error(windowsError) {}

CredentialVault::CredentialVault(const std::filesystem::path& privateBrowserRoot) {
    if (!std::filesystem::is_directory(privateBrowserRoot))
        throw VaultError("vault_unavailable", "Create a private browser data folder before opening saved credentials");
    auto root = privateBrowserRoot.native();
    std::wstring error;
    if (!PreparePrivateDirectory(root, error))
        throw VaultError("vault_unavailable", "Saved credentials require an existing private local browser data folder");
    file_ = std::filesystem::path(root) / L"credentials.v1.dpapi";
}

std::string CredentialVault::NormalizeOrigin(const std::string& input) {
    if (input.empty() || input.size() > MaxOrigin || input.find_first_of("\\?#@") != std::string::npos ||
        std::any_of(input.begin(), input.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
        throw VaultError("invalid_origin", "Use an exact HTTP or HTTPS origin without credentials, a path, query or fragment");
    const auto split = input.find("://");
    if (split == std::string::npos) throw VaultError("invalid_origin", "Use an exact HTTP or HTTPS origin");
    std::string scheme = input.substr(0, split);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    if (scheme != "http" && scheme != "https") throw VaultError("invalid_origin", "Saved credentials require HTTP or HTTPS");
    std::string authority = input.substr(split + 3);
    if (!authority.empty() && authority.back() == '/') authority.pop_back();
    if (authority.empty() || authority.find('/') != std::string::npos)
        throw VaultError("invalid_origin", "Pass a page origin, not a full page URL");
    std::string host, port;
    bool loopback = false;
    if (authority.front() == '[') {
        const auto end = authority.find(']');
        if (end == std::string::npos) throw VaultError("invalid_origin", "The IPv6 origin is invalid");
        const auto address = Wide(authority.substr(1, end - 1));
        IN6_ADDR parsed{}; wchar_t formatted[INET6_ADDRSTRLEN]{};
        if (InetPtonW(AF_INET6, address.c_str(), &parsed) != 1 || !InetNtopW(AF_INET6, &parsed, formatted, INET6_ADDRSTRLEN))
            throw VaultError("invalid_origin", "The IPv6 origin is invalid");
        host = "[";
        for (const wchar_t c : std::wstring(formatted)) host.push_back(static_cast<char>(c));
        host += "]";
        loopback = IN6_IS_ADDR_LOOPBACK(&parsed) != FALSE;
        if (end + 1 < authority.size()) {
            if (authority[end + 1] != ':') throw VaultError("invalid_origin", "The origin port is invalid");
            port = authority.substr(end + 2);
            if (port.empty()) throw VaultError("invalid_origin", "The origin port is invalid");
        }
    } else {
        const auto separator = authority.find(':');
        host = authority.substr(0, separator);
        if (separator != std::string::npos) {
            port = authority.substr(separator + 1);
            if (port.empty()) throw VaultError("invalid_origin", "The origin port is invalid");
        }
        auto unicode = Wide(host);
        const int count = IdnToAscii(IDN_USE_STD3_ASCII_RULES, unicode.data(), static_cast<int>(unicode.size()), nullptr, 0);
        if (count < 1 || count > 253) throw VaultError("invalid_origin", "The origin hostname is invalid");
        std::wstring ascii(static_cast<size_t>(count), L'\0');
        if (!IdnToAscii(IDN_USE_STD3_ASCII_RULES, unicode.data(), static_cast<int>(unicode.size()), ascii.data(), count))
            throw VaultError("invalid_origin", "The origin hostname is invalid");
        host.clear();
        for (const wchar_t c : ascii) host.push_back(static_cast<char>(c));
        std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        if (host.empty() || host.front() == '.' || host.find("..") != std::string::npos || host.find(':') != std::string::npos)
            throw VaultError("invalid_origin", "The origin hostname is invalid");
        loopback = host == "localhost" || host == "127.0.0.1";
    }
    if (!port.empty()) {
        unsigned value = 0;
        if (port.size() > 5) throw VaultError("invalid_origin", "The origin port is invalid");
        for (const unsigned char c : port) {
            if (c < '0' || c > '9') throw VaultError("invalid_origin", "The origin port is invalid");
            value = value * 10 + c - '0';
        }
        if (value == 0 || value > 65535) throw VaultError("invalid_origin", "The origin port is invalid");
        port = (value == 443 && scheme == "https") || (value == 80 && scheme == "http") ? "" : std::to_string(value);
    }
    if (scheme == "http" && !loopback)
        throw VaultError("insecure_origin", "Saved passwords require HTTPS; HTTP is allowed only for local fixture origins");
    return scheme + "://" + host + (port.empty() ? "" : ":" + port);
}

Json CredentialVault::List(const std::string& profile, const std::string& origin) const {
    Profile(profile);
    const auto normalized = origin.empty() ? "" : NormalizeOrigin(origin);
    std::lock_guard<std::mutex> guard(mutex_);
    auto document = Load(file_);
    Json result = Json::array();
    for (const auto& record : document->value["records"])
        if (record["profile"] == profile && (normalized.empty() || record["origin"] == normalized)) result.push_back(Metadata(record));
    return result;
}

std::string CredentialVault::Put(const std::string& profile, const std::string& origin,
                                  const std::string& username, const std::string& password, bool autoFill) {
    Profile(profile); Field(username, MaxUsername, false); Field(password, MaxPassword, true);
    const auto normalized = NormalizeOrigin(origin);
    std::lock_guard<std::mutex> guard(mutex_);
    auto document = Load(file_);
    for (auto& record : document->value["records"]) {
        if (record["profile"] == profile && record["origin"] == normalized && record["username"] == username) {
            Wipe(record["password"]); record["password"] = password; record["auto_fill"] = autoFill;
            Save(file_, *document); return record["id"].get<std::string>();
        }
    }
    if (document->value["records"].size() >= MaxRecords) throw VaultError("vault_full", "At most 100 saved logins can be stored");
    const auto id = RandomId();
    Document record;
    record.value = {{"id", id}, {"profile", profile}, {"origin", normalized},
                    {"username", username}, {"auto_fill", autoFill}};
    record.value["password"] = password;
    document->value["records"].push_back(std::move(record.value));
    Save(file_, *document); return id;
}

void CredentialVault::WithCredential(const std::string& profile, const std::string& origin, const std::string& id,
    const std::function<void(const std::wstring&, const std::wstring&)>& callback) const {
    Profile(profile); const auto normalized = NormalizeOrigin(origin);
    std::unique_ptr<Document> document;
    { std::lock_guard<std::mutex> guard(mutex_); document = Load(file_); }
    for (const auto& record : document->value["records"]) if (Match(record, profile, normalized, id)) {
        SecretWide username{Wide(record["username"].get_ref<const std::string&>())};
        SecretWide password{Wide(record["password"].get_ref<const std::string&>())};
        callback(username.value, password.value); return;
    }
    throw VaultError("credential_not_found", "No saved credential matches this profile and exact origin");
}

bool CredentialVault::Delete(const std::string& profile, const std::string& origin, const std::string& id) {
    Profile(profile); const auto normalized = NormalizeOrigin(origin);
    std::lock_guard<std::mutex> guard(mutex_); auto document = Load(file_);
    auto& records = document->value["records"];
    for (auto it = records.begin(); it != records.end(); ++it) if (Match(*it, profile, normalized, id)) {
        Wipe(*it); records.erase(it); Save(file_, *document); return true;
    }
    return false;
}

void CredentialVault::SetAutoFill(const std::string& profile, const std::string& origin, const std::string& id, bool enabled) {
    Profile(profile); const auto normalized = NormalizeOrigin(origin);
    std::lock_guard<std::mutex> guard(mutex_); auto document = Load(file_);
    for (auto& record : document->value["records"]) if (Match(record, profile, normalized, id)) {
        record["auto_fill"] = enabled; Save(file_, *document); return;
    }
    throw VaultError("credential_not_found", "No saved credential matches this profile and exact origin");
}
}
