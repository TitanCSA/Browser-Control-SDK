#include "CredentialVault.h"
#include "Ipc.h"
#include <wincrypt.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

using BrowserControl::CredentialVault;
using BrowserControl::Json;
using BrowserControl::VaultError;
namespace fs = std::filesystem;
namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Action> void Reject(Action action, const char* code) {
    try { action(); } catch (const VaultError& error) {
        Require(error.code == code, "The vault returned an unexpected error code"); return;
    }
    throw std::runtime_error("The vault accepted an invalid operation");
}
std::vector<char> Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Require(static_cast<bool>(stream), "Cannot read the owned test vault");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void Write(const fs::path& path, const std::vector<char>& data) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Cannot open the owned test vault for tamper validation");
    DWORD written = 0;
    const bool okay = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) != FALSE &&
                      written == data.size() && SetEndOfFile(file) != FALSE;
    CloseHandle(file);
    Require(okay, "Cannot write the owned test vault");
}
struct Cleanup {
    fs::path root;
    ~Cleanup() {
        std::error_code error;
        for (fs::directory_iterator it(root, error), end; it != end && !error; it.increment(error)) fs::remove(it->path(), error);
        fs::remove(root, error);
    }
};
}
int main() try {
    wchar_t local[32768]{};
    Require(GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768) != 0, "No current-user Local AppData folder");
    auto root = (fs::path(local) / L"TitanCSA" / L"BrowserControl" / L"Validation" /
        (L"Vault-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()))).native();
    std::wstring error;
    Require(BrowserControl::PreparePrivateDirectory(root, error), "Cannot create an owned private test directory");
    Cleanup cleanup{root};
    const fs::path file = fs::path(root) / L"credentials.v1.dpapi";
    CredentialVault vault(root);
    Require(vault.List("default").empty(), "New vault must be empty");

    Require(CredentialVault::NormalizeOrigin("HTTPS://Example.COM:443/") == "https://example.com", "HTTPS origin canonicalization failed");
    Require(CredentialVault::NormalizeOrigin("http://localhost:8765") == "http://localhost:8765", "Local HTTP origin was rejected");
    Require(CredentialVault::NormalizeOrigin("http://[0:0:0:0:0:0:0:1]:8765") == "http://[::1]:8765", "IPv6 origin canonicalization failed");
    Reject([] { CredentialVault::NormalizeOrigin("http://example.com"); }, "insecure_origin");
    Reject([] { CredentialVault::NormalizeOrigin("https://user@example.com"); }, "invalid_origin");
    Reject([] { CredentialVault::NormalizeOrigin("https://example.com/login"); }, "invalid_origin");
    Reject([] { CredentialVault::NormalizeOrigin("https://example.com:65536"); }, "invalid_origin");
    std::cout << "PASS exact secure-origin normalization and insecure-origin refusal\n";

    const std::string fakePassword = "LOCAL-VAULT-TEST-PASSWORD-not-a-real-login-7f4922";
    const std::string fakeUsername = "local-test-user";
    const auto id = vault.Put("default", "https://EXAMPLE.com:443/", "local-test-user", fakePassword, false);
    const auto metadata = vault.List("default", "https://example.com");
    Require(metadata.size() == 1 && !metadata[0].contains("password") && metadata[0]["id"] == id &&
            metadata[0]["auto_fill"] == false, "Metadata exposed a password or lost credential identity");
    bool called = false;
    vault.WithCredential("default", "https://example.com", id, [&](const std::wstring& username, const std::wstring& password) {
        called = username == L"local-test-user" && password == std::wstring(fakePassword.begin(), fakePassword.end());
    });
    Require(called, "Current-user DPAPI round trip failed");
    const auto ciphertext = Read(file);
    Require(std::search(ciphertext.begin(), ciphertext.end(), fakePassword.begin(), fakePassword.end()) == ciphertext.end(),
            "A plaintext password appeared in encrypted storage");
    Require(std::search(ciphertext.begin(), ciphertext.end(), fakeUsername.begin(), fakeUsername.end()) == ciphertext.end(),
            "The vault JSON was not fully encrypted");
    CredentialVault reopened(root);
    Require(reopened.List("default").size() == 1, "A reopened vault lost its saved record");
    std::cout << "PASS current-user DPAPI persistence and metadata-only listing\n";

    Require(vault.List("other").empty() && vault.List("default", "https://other.example").empty() &&
            vault.List("default", "https://example.com:444").empty(), "Credentials leaked between profile or origin scopes");
    Reject([&] { vault.WithCredential("other", "https://example.com", id, [](const auto&, const auto&) {}); }, "credential_not_found");
    Reject([&] { vault.WithCredential("default", "https://example.com:444", id, [](const auto&, const auto&) {}); }, "credential_not_found");
    Reject([&] { vault.WithCredential("default", "http://example.com", id, [](const auto&, const auto&) {}); }, "insecure_origin");
    std::cout << "PASS profile, scheme, host and port isolation\n";

    vault.SetAutoFill("default", "https://example.com", id, true);
    Require(vault.List("default")[0]["auto_fill"] == true, "Autofill consent was not persisted");
    const auto sameId = vault.Put("default", "https://example.com", "local-test-user", "UPDATED-FAKE-PASSWORD", false);
    Require(sameId == id && vault.List("default").size() == 1, "Updating a saved login duplicated its record");
    called = false;
    vault.WithCredential("default", "https://example.com", id, [&](const auto&, const std::wstring& password) {
        called = password == L"UPDATED-FAKE-PASSWORD";
    });
    Require(called && vault.List("default")[0]["auto_fill"] == false, "Updated password/consent was not persisted");
    std::cout << "PASS explicit autofill consent and atomic login update\n";

    const auto valid = Read(file);
    auto altered = valid; altered.back() ^= 0x7f; Write(file, altered);
    Reject([&] { vault.List("default"); }, "vault_decryption_failed");
    Reject([&] { vault.Put("default", "https://example.com", "new-user", "FAKE-PASSWORD", false); }, "vault_decryption_failed");
    Require(Read(file) == altered, "A failed decryption silently reset or overwrote saved storage");
    Write(file, std::vector<char>{'b','a','d'});
    Reject([&] { vault.List("default"); }, "vault_corrupt");
    Require(Read(file).size() == 3, "Corrupt storage was reset silently");
    Write(file, valid);
    std::cout << "PASS DPAPI tamper/corruption errors without silent reset\n";

    Reject([&] { vault.Put("default", "https://example.com", "local-user", std::string(4097, 'x'), false); }, "invalid_credential");
    Reject([&] { vault.Put("default", "https://example.com", std::string(513, 'x'), "fake", false); }, "invalid_credential");
    Reject([&] { vault.Put("../other", "https://example.com", "user", "fake", false); }, "invalid_profile");
    Require(Read(file) == valid, "Input validation changed saved credentials");
    std::cout << "PASS bounded credential fields and invalid profile refusal\n";

    Write(file, std::vector<char>(1024 * 1024 + 1, 'x'));
    Reject([&] { vault.List("default"); }, "vault_corrupt");
    Require(fs::file_size(file) == 1024 * 1024 + 1, "Oversized storage was reset silently");
    Write(file, valid);
    for (unsigned index = 0; index < 99; ++index)
        vault.Put("limits", "https://limits.example", "fake-user-" + std::to_string(index), "FAKE-LIMIT-PASSWORD", false);
    Reject([&] { vault.Put("limits", "https://limits.example", "one-too-many", "FAKE-LIMIT-PASSWORD", false); }, "vault_full");
    Require(vault.List("limits").size() == 99 && fs::file_size(file) <= 1024 * 1024, "Vault record/file bounds were not enforced");
    std::cout << "PASS 100-record and 1-MiB encrypted storage limits\n";

    Require(!vault.Delete("other", "https://example.com", id), "Deletion crossed profile boundaries");
    Require(vault.Delete("default", "https://example.com", id) && vault.List("default").empty(), "Credential deletion failed");
    Reject([&] { vault.WithCredential("default", "https://example.com", id, [](const auto&, const auto&) {}); }, "credential_not_found");
    Require(CredentialVault(root).List("default").empty(), "Deleted credentials returned after reopen");
    std::cout << "PASS persistent exact-scope credential deletion\n";
    std::cout << "All 8 current-user vault validation cases passed; only fake local credentials were used.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Vault validation failed: " << error.what() << '\n'; return 1;
}
