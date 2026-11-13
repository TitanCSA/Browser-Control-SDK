#include "SDK/Cpp/BrowserControl.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{

std::unique_ptr<BrowserControlSDK::Browser> browser;
BrowserControlSDK::Browser& Get(){Require(bool(browser),"Connect to a BrowserControl instance first");return *browser;}
Json BrowserResult(Json value){
 if(value.is_number_unsigned() && value.get<std::uint64_t>()>9007199254740991ull)return std::to_string(value.get<std::uint64_t>());
 if(value.is_number_integer()){auto n=value.get<std::int64_t>();if(n>9007199254740991ll || n< -9007199254740991ll)return std::to_string(n);}
 if(value.is_object() || value.is_array())for(auto& item:value)item=BrowserResult(std::move(item));return value;
}

Json Invoke(const std::string& op,const Json& a){

if(op=="check"){auto method=a.at("method").get<std::string>();Require(!method.empty() && method.size()<=256 && method.find('\0')==std::string::npos,"Invalid method name");auto params=a.value("params",Json::object());Require(params.is_object(),"Parameters must be an object");return {{"valid",true},{"scope","Request shape only; the browser validates method parameters"}};}
if(op=="connect"){browser=std::make_unique<BrowserControlSDK::Browser>(BrowserControlSDK::Browser::Connect(std::filesystem::path(Wide(a.value("endpoint",std::string())))));return Json::object();}
if(op=="launch"){auto exe=std::filesystem::path(Wide(a.at("executable").get<std::string>()));auto profile=std::filesystem::path(Wide(a.value("profile_root",std::string())));browser=std::make_unique<BrowserControlSDK::Browser>(BrowserControlSDK::Browser::Launch(exe,profile,Number(a,"timeout",30000,120000)));return Json::object();}
if(op=="disconnect"){browser.reset();return Json::object();}
if(op=="request"){try{return BrowserResult(Get().Call(a.at("method").get<std::string>(),a.value("params",Json::object()),Number(a,"timeout",15000,120000)));}catch(const BrowserControlSDK::Error& e){throw SdkError(31,e.code+": "+e.what()+"; details="+e.details.dump());}}
if(op=="info")return BrowserResult(Get().Info());
if(op=="capabilities")return BrowserResult(Get().Capabilities());
if(op=="tabs")return BrowserResult(Get().Tabs());
if(op=="profiles")return BrowserResult(Get().Profiles());

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned browser_control_sdk_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned browser_control_sdk_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* browser_control_sdk_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void browser_control_sdk_json_free(char* response)noexcept{delete[] response;}
