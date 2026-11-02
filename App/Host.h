#pragma once
#include "Common.h"
#include <memory>

namespace BrowserControl {
class Host {
public:
    Host();
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    bool Create(HINSTANCE instance, const std::wstring& profileRoot, int showCommand);
    HWND Window() const noexcept;
    void SetEndpoint(const std::wstring& path);
    // Called on the window/UI thread. Invoke reply exactly once, asynchronously if needed.
    // The result is Ok(value) or Fail(code,message,details); IPC preserves request IDs.
    void Dispatch(Json request, Reply reply);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
