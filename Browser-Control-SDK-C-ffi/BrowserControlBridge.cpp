#include "BrowserControlBridge.h"
#include "SDK/Cpp/BrowserControl.h"
#include <mutex>
#include <unordered_map>
#include <limits>
#include <cstdlib>
#include <cstring>
namespace {
using Browser=BrowserControlSDK::Browser;
using Json=BrowserControlSDK::Json;
std::mutex gate;
std::unordered_map<uint64_t,std::shared_ptr<Browser>> instances;
uint64_t next_id=1;
void text(char** out,const std::string& value) {
    auto data=static_cast<char*>(std::malloc(value.size()+1));
    if (!data) throw std::bad_alloc();
    std::memcpy(data,value.c_str(),value.size()+1);*out=data;
}
void error(char** out) noexcept {
    if (!out) return;
    try { throw; }
    catch (const BrowserControlSDK::Error& e) { try {text(out,Json{{"code",e.code},{"message",e.what()},{"details",e.details}}.dump());} catch (...) {} }
    catch (const std::exception& e) { try {text(out,Json{{"code","sdk_error"},{"message",e.what()},{"details",Json::object()}}.dump());} catch (...) {} }
    catch (...) {}
}
}
uint64_t bcs_connect(const char* endpoint,char** out) {
    if (!out) return 0;*out=nullptr;
    try {
        auto browser=std::make_shared<Browser>(Browser::Connect(endpoint ? std::filesystem::u8path(endpoint) : std::filesystem::path{}));
        std::lock_guard<std::mutex> lock(gate);
        if (next_id==std::numeric_limits<uint64_t>::max()) throw std::runtime_error("SDK handle capacity exhausted");
        auto id=next_id++;instances.emplace(id,std::move(browser));return id;
    } catch (...) {error(out);return 0;}
}
int bcs_call(uint64_t id,const char* method,const char* params,uint32_t timeout,char** out) {
    if (!out) return 1;*out=nullptr;
    try {
        std::shared_ptr<Browser> browser;
        { std::lock_guard<std::mutex> lock(gate);auto it=instances.find(id);
          if (it==instances.end()) throw std::runtime_error("Browser client is closed");browser=it->second; }
        if (!method || !params) throw std::invalid_argument("Method and JSON parameters are required");
        text(out,browser->Call(method,Json::parse(params),timeout).dump());return 0;
    } catch (...) {error(out);return 1;}
}
void bcs_close(uint64_t id) {std::lock_guard<std::mutex> lock(gate);instances.erase(id);}
void bcs_free(void* value) {std::free(value);}
