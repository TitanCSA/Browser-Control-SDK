#pragma once
#include "Common.h"
#include <memory>

namespace BrowserControl {
// Creates missing private directories and validates existing ones without
// changing their ACLs. Resolves junctions/mapped drives and requires local storage.
bool PreparePrivateDirectory(std::wstring& path, std::wstring& error) noexcept;
class IpcServer {
public:
    IpcServer();
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    bool Start(HWND target, const std::wstring& endpointPath, const std::wstring& profileRoot,
               std::wstring& error) noexcept;
    void Stop() noexcept;
    std::wstring EndpointPath() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
