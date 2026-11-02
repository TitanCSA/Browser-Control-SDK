#pragma once
#include <windows.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <atomic>
#include <memory>

namespace BrowserControl {
using Json = nlohmann::json;
using Reply = std::function<void(Json)>;
inline constexpr UINT RpcMessage = WM_APP + 42;
struct Envelope {
    Json request;
    Reply reply;
    std::shared_ptr<std::atomic_bool> canceled;
    // 0 queued, 1 dispatched, 2 canceled before dispatch. UI claims 0 -> 1 atomically.
    std::shared_ptr<std::atomic_uint> phase;
    // Optional thread-safe partial receipt publication; never action arguments.
    Reply progress;
};
inline Json Ok(Json value = Json::object()) { return {{"ok", true}, {"result", std::move(value)}}; }
inline Json Fail(const std::string& code, const std::string& message, Json details = Json::object()) {
    return {{"ok", false}, {"error", {{"code", code}, {"message", message}, {"details", std::move(details)}}}};
}
}
