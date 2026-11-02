#pragma once
#include "Common.h"
#include <filesystem>
#include <mutex>
#include <stdexcept>

namespace BrowserControl {
class VaultError : public std::runtime_error {
public:
    VaultError(std::string code, std::string message, DWORD windowsError = 0);
    const std::string code;
    const DWORD windows_error;
};

// Passwords remain inside this browser process. List returns metadata only;
// WithCredential exposes one exact profile/origin match to a transient callback.
// DPAPI protects storage for the current Windows user, not against that user.
class CredentialVault {
public:
    explicit CredentialVault(const std::filesystem::path& privateBrowserRoot);
    Json List(const std::string& profile, const std::string& origin = "") const;
    std::string Put(const std::string& profile, const std::string& origin,
                    const std::string& username, const std::string& password, bool autoFill);
    void WithCredential(const std::string& profile, const std::string& origin,
                        const std::string& id,
                        const std::function<void(const std::wstring&, const std::wstring&)>& callback) const;
    bool Delete(const std::string& profile, const std::string& origin, const std::string& id);
    void SetAutoFill(const std::string& profile, const std::string& origin,
                     const std::string& id, bool enabled);
    // Accepts an origin (optional trailing slash), not a page URL or credentials.
    // HTTP is accepted only for localhost, 127.0.0.1 and [::1] fixture origins.
    static std::string NormalizeOrigin(const std::string& origin);
private:
    std::filesystem::path file_;
    mutable std::mutex mutex_;
};
}
