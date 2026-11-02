#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace BrowserControlSDK {
using Json = nlohmann::json;
class Error : public std::runtime_error {
public:
    Error(std::string code, std::string message, Json details = Json::object());
    const std::string code;
    const Json details;
};
namespace Detail { struct Connection; }
class Tab;

class Browser {
public:
    // An empty path discovers the newest responding instance of the current user.
    static Browser Connect(const std::filesystem::path& endpoint = {});
    static Browser Launch(const std::filesystem::path& executable,
                          const std::filesystem::path& profileRoot = {}, unsigned timeoutMs = 30000);
    Json Call(const std::string& method, Json params = Json::object(), unsigned timeoutMs = 15000) const;
    Json Info() const;
    Json Capabilities() const;
    Json Tabs() const;
    Tab NewTab(Json options = Json::object()) const;
    Tab GetTab(std::string id) const;
    Json Profiles() const;
    Json CreateProfile(const std::string& name, bool inPrivate = false) const;
    Json SavedLogins(Json options = Json::object()) const;
    Json DeleteSavedLogin(const std::string& credentialId) const;
    Json SetSavedLoginAutofill(const std::string& credentialId, bool enabled) const;
    Json Window(Json options = Json()) const;
    Json Events(unsigned long long since = 0, unsigned limit = 100) const;
    Json Shutdown() const;
private:
    explicit Browser(std::shared_ptr<Detail::Connection> connection);
    std::shared_ptr<Detail::Connection> connection_;
};

class Tab {
public:
    const std::string& Id() const noexcept { return id_; }
    Json Call(const std::string& method, Json params = Json::object(), unsigned timeoutMs = 15000) const;
    Json Goto(const std::string& url, const std::string& wait = "load", unsigned timeoutMs = 15000) const;
    Json Info() const;
    Json Activate() const;
    Json Close() const;
    Json Snapshot() const;
    Json Observe(Json options = Json::object()) const;
    Json Act(Json actions, bool observe = true, unsigned timeoutMs = 15000, Json guards = Json::object()) const;
    Json Login(const std::string& operation = "status", Json options = Json::object()) const;
    Json Click(Json target) const;
    Json Fill(Json target, const std::string& text) const;
    Json Type(const std::string& text) const;
    Json Press(const std::string& key, const std::string& action = "press") const;
    Json Mouse(const std::string& type, double x, double y, Json options = Json::object()) const;
    Json Scroll(double deltaY, double deltaX = 0) const;
    // Returns base64 image data + MIME/viewport metadata. The caller chooses storage.
    Json Screenshot(Json options = Json::object()) const;
    Json Evaluate(const std::string& expression, bool awaitPromise = true, const std::string& world = "main") const;
    Json Wait(Json target, const std::string& state = "visible", unsigned timeoutMs = 15000) const;
    Json Viewport(unsigned width, unsigned height, double deviceScaleFactor = 1) const;
    Json Settings(Json settings) const;
    Json Cookies(const std::string& operation = "list", Json options = Json::object()) const;
    Json Storage(const std::string& operation = "get", Json options = Json::object()) const;
    Json Cdp(const std::string& method, Json params = Json::object()) const;
private:
    friend class Browser;
    Tab(std::shared_ptr<Detail::Connection> connection, std::string id);
    std::shared_ptr<Detail::Connection> connection_;
    std::string id_;
};
}
