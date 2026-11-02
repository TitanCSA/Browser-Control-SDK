#include "Host.h"
#include "DomScripts.h"
#include "CredentialVault.h"
#include <wrl.h>
#include <WebView2.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace BrowserControl {
namespace {
constexpr int ToolbarBottom=98, StatusHeight=26;
constexpr int AddressId=110, BackId=111, ForwardId=112, ReloadId=113, NewId=114, CloseId=115, TabsId=116, GoId=117, ProfileId=118, DownloadId=119, LoginId=120;
constexpr int SaveLoginId=130, FillLoginId=131, ForgetLoginId=132, AutoLoginId=133;
constexpr COLORREF Navy=RGB(16,24,39), Bar=RGB(26,38,57), Blue=RGB(40,112,235), White=RGB(240,245,252);
std::wstring Wide(const std::string& s) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n);return result;
}
std::string Utf8(const std::wstring& s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.data(),n,nullptr,nullptr);return result;
}
std::wstring Take(LPWSTR value,size_t maximum=32768) { std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> owned(value,CoTaskMemFree);return value?std::wstring(value,wcsnlen_s(value,maximum)):std::wstring(); }
struct CookieText {LPWSTR value=nullptr;~CookieText(){CoTaskMemFree(value);}std::string Read(size_t limit)const{if(!value)return {};size_t count=wcsnlen_s(value,limit+1);if(count>limit)throw std::runtime_error("Cookie field exceeds the bounded response limit");return Utf8(std::wstring(value,count));}};
std::string Hr(HRESULT value){std::ostringstream s;s<<"0x"<<std::hex<<static_cast<unsigned long>(value);return s.str();}
bool SafeProfile(const std::string& s){return !s.empty()&&s.size()<=64&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';});}
std::wstring ProfileDirectoryName(const std::string& name){const wchar_t* digits=L"0123456789abcdef";std::wstring result;result.reserve(name.size()*2);for(unsigned char c:name){result.push_back(digits[c>>4]);result.push_back(digits[c&15]);}return result;}
std::string EventUrl(std::string s){auto pos=s.find_first_of("?#");if(pos!=std::string::npos)s.resize(pos);return s.substr(0,2048);}
std::string NavigationKey(std::string value){
    auto hash=value.find('#');if(hash!=std::string::npos)value.resize(hash);
    auto scheme=value.find("://");if(scheme!=std::string::npos){auto end=value.find_first_of("/?",scheme+3);if(end==std::string::npos){value+="/";end=value.size()-1;}else if(value[end]=='?')value.insert(end,"/");for(size_t i=0;i<end;++i)if(value[i]>='A'&&value[i]<='Z')value[i]=static_cast<char>(value[i]-'A'+'a');}
    return value;
}
std::vector<std::string> Methods(){return {"browser.info","browser.capabilities","browser.shutdown","tabs.list","tabs.create","tabs.activate","tabs.close","profiles.list","profiles.create","page.navigate","page.back","page.forward","page.reload","page.stop","page.info","page.snapshot","page.observe","page.act","page.login","saved_logins.list","saved_logins.delete","saved_logins.autofill","page.evaluate","page.wait","page.click","page.fill","page.type","page.key","page.mouse","page.scroll","page.screenshot","page.viewport","page.settings","page.cookies","page.storage","page.cdp","window.set","window.info","downloads.list","downloads.configure","events.read"};}
void EraseText(std::string& value){if(!value.empty())SecureZeroMemory(value.data(),value.size());value.clear();}
void EraseLoginJson(Json& value){for(const auto* key:{"username","password"})if(value.contains(key)&&value[key].is_string())EraseText(value[key].get_ref<std::string&>());}
struct LoginJsonGuard {Json& value;~LoginJsonGuard(){EraseLoginJson(value);}};
struct SecretTextGuard {std::string& value;~SecretTextGuard(){EraseText(value);}};
struct Operation {
    Reply reply,progress;ULONGLONG deadline=0;bool done=false;std::function<Json()> timeoutResult;
    void Finish(Json value){if(done)return;done=true;auto f=std::move(reply);if(f)try{f(std::move(value));}catch(...){}}
    void Publish(const Json& value){if(!done&&progress)try{progress(value);}catch(...){}}
};
struct Profile {std::string name;bool inPrivate=false;ComPtr<ICoreWebView2Environment> environment;bool creating=false;std::vector<std::function<void(HRESULT)>> waiting;};
struct Tab {
    std::string id,profile,title="New tab",url="about:blank",initialUrl="about:blank";
    bool inPrivate=false,ready=false,loading=true,closed=false,externalUrisBlocked=false;
    uint64_t generation=0,snapshot=0;int context=0,viewportWidth=0,viewportHeight=0;double scale=1;
    uint64_t autoFillGeneration=UINT64_MAX;
    unsigned buttons=0;ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> core;
    std::shared_ptr<Operation> navigation;std::string wait="load",expectedUrl;uint64_t currentNavigation=0,expectedNavigation=0;
};
struct Waiting {std::weak_ptr<Tab> tab;Json target;std::string state;std::shared_ptr<Operation> operation;bool evaluating=false;};
struct State : std::enable_shared_from_this<State> {
    HWND hwnd=nullptr,address=nullptr,tabsControl=nullptr,profileBox=nullptr;
    HINSTANCE instance=nullptr;HFONT font=nullptr;HBRUSH navyBrush=nullptr,barBrush=nullptr;
    std::wstring root,endpoint;std::string runtime,error,status="Starting Chromium runtime...";
    std::vector<std::shared_ptr<Tab>> tabs;std::map<std::string,std::shared_ptr<Profile>> profiles;
    std::string active;uint64_t nextTab=1,eventSequence=0,dropped=0;bool closing=false,downloadsEnabled=false;
    fs::path downloadDirectory;std::deque<Json> events;std::vector<Json> downloads;std::map<std::string,ComPtr<ICoreWebView2DownloadOperation>> downloadOperations;
    std::vector<std::shared_ptr<Operation>> pending;std::vector<std::shared_ptr<Waiting>> waiting;
    std::vector<HWND> buttons;
    std::unique_ptr<CredentialVault> vault;std::string vaultError;

    ~State(){if(font)DeleteObject(font);if(navyBrush)DeleteObject(navyBrush);if(barBrush)DeleteObject(barBrush);}
    std::shared_ptr<Operation> NewOperation(Reply reply,const Json& request){
        auto op=std::make_shared<Operation>();op->reply=std::move(reply);
        int64_t ms=15000;try{if(request.contains("timeout_ms")){if(!request.at("timeout_ms").is_number_integer())throw std::invalid_argument("timeout_ms must be an integer from 1 to 120000");ms=request.at("timeout_ms").get<int64_t>();}if(ms<1||ms>120000)throw std::invalid_argument("timeout_ms must be between 1 and 120000");op->deadline=GetTickCount64()+static_cast<ULONGLONG>(ms);if(request.contains("_deadline_ticks"))op->deadline=std::min(op->deadline,request.at("_deadline_ticks").get<ULONGLONG>());}catch(const std::exception& error){op->Finish(Fail("invalid_argument",error.what()));return op;}
        if(GetTickCount64()>=op->deadline){op->Finish(Fail("timeout","The request expired before the browser operation started"));return op;}
        pending.erase(std::remove_if(pending.begin(),pending.end(),[](const auto& entry){return entry->done;}),pending.end());
        if(pending.size()>=128){op->Finish(Fail("limit_exceeded","Too many browser operations are pending"));return op;}
        pending.push_back(op);return op;
    }
    void Event(const std::string& type,const std::string& tab,Json data=Json::object()){
        events.push_back({{"seq",++eventSequence},{"time_ms",GetTickCount64()},{"type",type},{"tab",tab},{"data",std::move(data)}});
        if(events.size()>512){events.pop_front();++dropped;}
    }
    std::shared_ptr<Tab> Find(const std::string& id){for(auto& t:tabs)if(t->id==id&&!t->closed)return t;return {};}
    Json Info(const std::shared_ptr<Tab>& t){return {{"id",t->id},{"title",t->title},{"url",t->url},{"profile",t->profile},{"in_private",t->inPrivate},{"ready",t->ready},{"loading",t->loading},{"active",t->id==active},{"generation",t->generation},{"external_uri_launches_blocked",t->externalUrisBlocked}};}
    void Note(std::string value){status=std::move(value);if(hwnd)InvalidateRect(hwnd,nullptr,FALSE);}
    void Layout(){
        if(!hwnd)return;RECT r{};GetClientRect(hwnd,&r);const int width=r.right;
        MoveWindow(tabsControl,12,8,std::max(100,width-470),30,TRUE);
        MoveWindow(GetDlgItem(hwnd,NewId),std::max(120,width-448),8,34,30,TRUE);
        MoveWindow(GetDlgItem(hwnd,CloseId),std::max(156,width-410),8,34,30,TRUE);
        MoveWindow(profileBox,std::max(192,width-368),9,160,250,TRUE);
        MoveWindow(GetDlgItem(hwnd,LoginId),std::max(360,width-200),8,90,30,TRUE);
        MoveWindow(GetDlgItem(hwnd,DownloadId),std::max(360,width-110),8,98,30,TRUE);
        MoveWindow(GetDlgItem(hwnd,BackId),12,49,35,34,TRUE);MoveWindow(GetDlgItem(hwnd,ForwardId),52,49,35,34,TRUE);MoveWindow(GetDlgItem(hwnd,ReloadId),92,49,35,34,TRUE);
        MoveWindow(address,139,49,std::max(100,width-211),34,TRUE);MoveWindow(GetDlgItem(hwnd,GoId),width-64,49,52,34,TRUE);
        RECT area{0,ToolbarBottom,width,std::max(ToolbarBottom,static_cast<int>(r.bottom)-StatusHeight)};
        for(auto& t:tabs)if(t->controller)t->controller->put_Bounds(area);
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    void RefreshTabs(){
        TabCtrl_DeleteAllItems(tabsControl);int i=0,index=0;
        for(auto& t:tabs){if(t->closed)continue;std::wstring name=Wide(t->title.empty()?"New tab":t->title);if(name.size()>35)name=name.substr(0,32)+L"...";TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=name.data();TabCtrl_InsertItem(tabsControl,i,&item);if(t->id==active)index=i;++i;}
        TabCtrl_SetCurSel(tabsControl,index);
        if(auto t=Find(active)){SetWindowTextW(address,Wide(t->url).c_str());SetWindowTextW(hwnd,(Wide(t->title)+L" - Browser Control SDK").c_str());}
        Layout();
    }
    void Activate(const std::shared_ptr<Tab>& chosen){
        active=chosen->id;for(auto& t:tabs)if(t->controller)t->controller->put_IsVisible(t==chosen?TRUE:FALSE);
        if(chosen->controller)chosen->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        RefreshTabs();Event("tab.activated",chosen->id);
    }
    void ProfileList(){
        std::wstring selected=L"default";int n=static_cast<int>(SendMessageW(profileBox,CB_GETCURSEL,0,0));if(n>=0){wchar_t b[128]{};SendMessageW(profileBox,CB_GETLBTEXT,static_cast<WPARAM>(n),reinterpret_cast<LPARAM>(b));selected=b;}
        SendMessageW(profileBox,CB_RESETCONTENT,0,0);int i=0,select=0;
        for(const auto& entry:profiles){auto text=Wide(entry.first);SendMessageW(profileBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(text==selected)select=i;++i;}
        SendMessageW(profileBox,CB_SETCURSEL,static_cast<WPARAM>(select),0);
    }
    void SaveProfiles(){
        Json names=Json::array();for(const auto& entry:profiles)names.push_back({{"name",entry.first},{"in_private",entry.second->inPrivate}});
        fs::path target=fs::path(root)/L"profiles.json",temp=fs::path(root)/L"profiles.json.tmp";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);auto data=names.dump(2);out.write(data.data(),static_cast<std::streamsize>(data.size()));if(!out)throw std::runtime_error("Profile metadata could not be saved");}
        if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Profile metadata could not be committed");
    }
    void EvalLogin(const std::shared_ptr<Tab>& tab,const std::string& script,const std::shared_ptr<Operation>& op,std::function<void(Json)> completed,bool allowNavigation=false){
        if(op->done)return;
        const auto now=GetTickCount64();if(now>=op->deadline){op->Finish(Fail("timeout","Saved login operation expired"));return;}
        auto guarded=NewOperation([op,completed=std::move(completed)](Json reply){
            if(op->done){if(reply.contains("result"))EraseLoginJson(reply["result"]);return;}
            if(!reply.value("ok",false)){const auto code=reply.value("error",Json::object()).value("code",std::string("engine_error"));op->Finish(Fail("login_failed","The login form could not be accessed safely",{{"stage","login_form"},{"cause",code}}));return;}
            auto value=std::move(reply["result"]);try{completed(std::move(value));}catch(const VaultError& error){EraseLoginJson(value);op->Finish(Fail(error.code,error.what(),{{"windows_error",error.windows_error}}));}catch(...){EraseLoginJson(value);op->Finish(Fail("login_failed","The saved login operation could not be completed safely"));}
        },{{"timeout_ms",static_cast<int>(std::min<ULONGLONG>(120000,op->deadline-now))}});
        guarded->deadline=op->deadline;
        Eval(tab,script,guarded,[guarded](Json value){guarded->Finish(Ok(std::move(value)));},true,true,allowNavigation);
    }
    void Login(const std::shared_ptr<Tab>& tab,const Json& p,const std::shared_ptr<Operation>& op,bool autoOnly=false){
        if(!vault){op->Finish(Fail("vault_unavailable","Saved logins are unavailable",{{"detail",vaultError}}));return;}
        if(tab->inPrivate){op->Finish(Fail("private_profile","Saved logins cannot be saved or filled in private profiles"));return;}
        auto action=p.value("operation",std::string("status"));if(action!="status"&&action!="fill")throw std::invalid_argument("Login operation must be status or fill");
        const auto generation=tab->generation;auto self=shared_from_this();
        EvalLogin(tab,LoginInspectScript(false),op,[self,tab,p,op,action,generation,autoOnly](Json form){
            auto origin=CredentialVault::NormalizeOrigin(form.at("origin").get<std::string>());auto items=self->vault->List(tab->profile,origin);
            if(action=="status"){op->Finish(Ok({{"profile",tab->profile},{"origin",origin},{"form",form},{"saved_logins",items},{"available",form.value("status",std::string())=="ready"&&!items.empty()}}));return;}
            if(form.value("status",std::string())!="ready"){op->Finish(Fail(form.value("status",std::string("no_login_form")),"A unique visible login form is required; focus the intended form first"));return;}
            auto selected=p.value("credential_id",std::string());Json matches=Json::array();for(const auto& item:items)if((selected.empty()||item.at("id")==selected)&&(!autoOnly||item.value("auto_fill",false)))matches.push_back(item);
            if(matches.size()!=1){op->Finish(Fail(matches.empty()?"login_not_found":"ambiguous_login",matches.empty()?"No saved login matches this exact origin and profile":"Choose one credential_id for this origin and profile"));return;}
            if(op->done||tab->closed||tab->generation!=generation){op->Finish(Fail("stale_document","The page changed before the saved login could be filled"));return;}
            const auto id=matches[0].at("id").get<std::string>();const bool submit=p.value("submit",false);
            self->vault->WithCredential(tab->profile,origin,id,[self,tab,op,generation,origin,id,submit](const std::wstring& user,const std::wstring& pass){
                if(op->done||tab->closed||tab->generation!=generation){op->Finish(Fail("stale_document","The page changed before the saved login could be filled"));return;}
                Json credential{{"origin",origin},{"username",Utf8(user)},{"password",Utf8(pass)},{"submit",submit}};LoginJsonGuard clearCredential{credential};std::string encoded=credential.dump();SecretTextGuard clearEncoded{encoded};std::string script=LoginFillScript(encoded);SecretTextGuard clearScript{script};EraseLoginJson(credential);EraseText(encoded);
                self->EvalLogin(tab,script,op,[op,id,origin](Json value){const auto status=value.value("status",std::string());if(status!="filled"){op->Finish(Fail(status.empty()?"login_failed":status,"The login form changed or could not be filled safely"));return;}value["credential_id"]=id;value["origin"]=origin;value["authentication_verified"]=false;op->Finish(Ok(value));},submit);
            });
        });
    }
    void MaybeAutoFill(const std::shared_ptr<Tab>& tab){
        if(!vault||tab->inPrivate||tab->closed||tab->autoFillGeneration==tab->generation)return;
        tab->autoFillGeneration=tab->generation;auto saved=vault->List(tab->profile);if(std::none_of(saved.begin(),saved.end(),[](const Json& item){return item.value("auto_fill",false);}))return;auto weak=weak_from_this();const auto id=tab->id;
        auto op=NewOperation([weak,id](Json result){if(auto state=weak.lock()){if(result.value("ok",false)){state->Event("login.autofilled",id);state->Note("Saved login filled for its exact origin; complete sign-in or MFA on the page");}}},{{"timeout_ms",5000}});
        Login(tab,{{"operation","fill"},{"submit",false}},op,true);
    }
    void UiLogin(int command){
        auto tab=Find(active);if(!tab||!tab->ready){Note("Open a ready browser tab first");return;}if(tab->inPrivate){Note("Saved logins are disabled in private profiles");return;}if(!vault){Note("Saved login storage is unavailable: "+vaultError);return;}
        auto weak=weak_from_this();auto op=NewOperation([weak](Json reply){if(auto state=weak.lock())if(!reply.value("ok",false))state->Note(reply.value("error",Json::object()).value("message",std::string("Saved login operation failed")));},{{"timeout_ms",120000}});
        const auto generation=tab->generation;auto self=shared_from_this();
        EvalLogin(tab,LoginInspectScript(command==SaveLoginId),op,[self,tab,command,generation,op](Json value){
            LoginJsonGuard cleanup{value};
            auto origin=CredentialVault::NormalizeOrigin(value.at("origin").get<std::string>());
            if(command==SaveLoginId){
                if(value.value("status",std::string())!="ready"||!value.contains("password")||value.at("password").get_ref<const std::string&>().empty()){op->Finish(Fail("no_login_form","Enter a username and password in one visible login form first"));return;}
                const std::wstring prompt=L"Save the login currently entered on this page?\n\nOrigin: "+Wide(origin)+L"\nProfile: "+Wide(tab->profile)+L"\n\nThe password will be protected for this Windows user.\nAutomatic filling remains off until you enable it.\nOnly save credentials you are authorized to use.";
                if(MessageBoxW(self->hwnd,prompt.c_str(),L"Save login - explicit permission",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES){op->Finish(Ok({{"saved",false}}));return;}
                if(op->done||tab->closed||tab->generation!=generation){op->Finish(Fail("stale_document","The page changed while permission was requested; save again on the current page"));return;}
                const auto id=self->vault->Put(tab->profile,origin,value.value("username",std::string()),value.at("password").get_ref<const std::string&>(),false);self->Note("Login saved for this exact origin and profile; use Logins to fill or enable auto-fill");self->Event("login.saved",tab->id,{{"credential_id",id}});op->Finish(Ok({{"saved",true},{"credential_id",id}}));return;
            }
            auto items=self->vault->List(tab->profile,origin);if(items.empty()){op->Finish(Fail("login_not_found","No saved login matches this exact origin and profile"));return;}
            size_t index=0;
            if(items.size()>1){
                HMENU menu=CreatePopupMenu();auto heading=Wide(origin);AppendMenuW(menu,MF_STRING|MF_GRAYED,0,heading.c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
                for(size_t i=0;i<items.size();++i){auto user=items[i].value("username",std::string());auto label=Wide(user.empty()?"(no username)":user);if(label.size()>120)label.resize(120);size_t at=0;while((at=label.find(L'&',at))!=std::wstring::npos){label.insert(at,L"&");at+=2;}if(items[i].value("auto_fill",false))label+=L"  [auto-fill]";AppendMenuW(menu,MF_STRING,static_cast<UINT_PTR>(1000+i),label.c_str());}
                POINT point{};GetCursorPos(&point);int choice=static_cast<int>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,point.x,point.y,0,self->hwnd,nullptr));DestroyMenu(menu);
                if(choice<1000||static_cast<size_t>(choice-1000)>=items.size()){op->Finish(Ok({{"canceled",true}}));return;}index=static_cast<size_t>(choice-1000);
            }
            if(op->done||tab->closed||tab->generation!=generation){op->Finish(Fail("stale_document","The page changed while choosing an account; choose again on the current page"));return;}
            auto id=items[index].at("id").get<std::string>();if(command==FillLoginId){self->Login(tab,{{"operation","fill"},{"credential_id",id}},op);return;}
            if(command==ForgetLoginId){self->vault->Delete(tab->profile,origin,id);self->Note("Saved login removed for this origin and profile");op->Finish(Ok({{"deleted",true}}));}
            else if(command==AutoLoginId){bool enabled=!items[index].value("auto_fill",false);self->vault->SetAutoFill(tab->profile,origin,id,enabled);self->Note(enabled?"Auto-fill enabled for this exact origin and profile; forms are never submitted automatically":"Auto-fill disabled for this saved login");op->Finish(Ok({{"auto_fill",enabled}}));}
        });
    }
    void Cdp(const std::shared_ptr<Tab>& tab,const std::string& method,Json params,const std::shared_ptr<Operation>& op,std::function<void(Json)> completed){
        if(op->done)return;if(closing||tab->closed||!tab->core){op->Finish(Fail("tab_closed","The tab is closed or unavailable"));return;}
        auto weak=weak_from_this();std::weak_ptr<Tab> weakTab=tab;
        HRESULT hr=tab->core->CallDevToolsProtocolMethod(Wide(method).c_str(),Wide(params.dump()).c_str(),Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [weak,weakTab,op,completed=std::move(completed)](HRESULT code,LPCWSTR value)->HRESULT{
                if(op->done)return S_OK;auto state=weak.lock();auto current=weakTab.lock();
                if(!state||state->closing||!current||current->closed){op->Finish(Fail("tab_closed","The tab closed during the operation"));return S_OK;}
                if(FAILED(code)){op->Finish(Fail("engine_error","Chromium rejected the command",{{"hresult",Hr(code)}}));return S_OK;}
                try{Json data=Json::parse(Utf8(value?value:L"{}"));if(data.contains("error")){op->Finish(Fail("protocol_error","Chromium reported an error",data["error"]));return S_OK;}completed(std::move(data));}
                catch(const std::exception& e){op->Finish(Fail("engine_response_error",e.what()));}
                return S_OK;
            }).Get());
        if(FAILED(hr))op->Finish(Fail("engine_error","The browser command could not be submitted",{{"hresult",Hr(hr)},{"method",method}}));
    }
    void Eval(const std::shared_ptr<Tab>& tab,const std::string& expression,const std::shared_ptr<Operation>& op,std::function<void(Json)> completed,bool await=true,bool isolated=true,bool allowNavigation=false){
        if(op->done)return;
        auto self=shared_from_this();const auto generation=tab->generation;
        if(isolated&&!tab->context){
            Cdp(tab,"Page.getFrameTree",Json::object(),op,[self,tab,expression,op,completed,await,generation,allowNavigation](Json frames){
                if(tab->generation!=generation){op->Finish(Fail("stale_document","The page navigated; retry on the new document"));return;}
                const auto id=frames.at("frameTree").at("frame").at("id").get<std::string>();
                self->Cdp(tab,"Page.createIsolatedWorld",{{"frameId",id},{"worldName","BrowserControlSDK"},{"grantUniveralAccess",false}},op,
                    [self,tab,expression,op,completed,await,generation,allowNavigation](Json context){
                        if(tab->generation!=generation){op->Finish(Fail("stale_document","The page navigated; retry on the new document"));return;}
                        tab->context=context.at("executionContextId").get<int>();self->Eval(tab,expression,op,completed,await,true,allowNavigation);
                    });
            });return;
        }
        Json params{{"expression",expression},{"returnByValue",true},{"awaitPromise",await},{"userGesture",true}};if(isolated)params["contextId"]=tab->context;
        Cdp(tab,"Runtime.evaluate",std::move(params),op,
            [op,completed,generation,tab,allowNavigation](Json data){
                if(!allowNavigation&&tab->generation!=generation){op->Finish(Fail("stale_document","The page navigated during evaluation"));return;}
                if(data.contains("exceptionDetails")){
                    auto detail=data["exceptionDetails"];std::string message=detail.value("text",std::string("JavaScript exception"));
                    if(detail.contains("exception"))message=detail["exception"].value("description",message);
                    auto code=message.find("STALE_REF")!=std::string::npos?"stale_ref":message.find("AMBIGUOUS_TARGET")!=std::string::npos?"ambiguous_target":"javascript_error";
                    op->Finish(Fail(code,message.substr(0,16000)));return;
                }
                Json result=data.value("result",Json::object());if(result.value("type",std::string())=="undefined")completed(Json());else completed(result.value("value",Json()));
            });
    }
    void Sequence(const std::shared_ptr<Tab>& tab,std::vector<std::pair<std::string,Json>> calls,const std::shared_ptr<Operation>& op,Json result){
        auto queue=std::make_shared<std::vector<std::pair<std::string,Json>>>(std::move(calls));auto index=std::make_shared<size_t>(0);
        auto next=std::make_shared<std::function<void()>>();std::weak_ptr<std::function<void()>> weakNext=next;auto self=shared_from_this();
        *next=[self,tab,queue,index,op,result,weakNext](){
            if(op->done)return;if(*index==queue->size()){op->Finish(Ok(result));return;}
            auto call=(*queue)[(*index)++];auto retained=weakNext.lock();
            self->Cdp(tab,call.first,std::move(call.second),op,[retained](Json){if(retained)(*retained)();});
        };(*next)();
    }
    void NavigationDone(const std::shared_ptr<Tab>& tab,uint64_t id,bool dom,bool success=true,int errorCode=0){
        if(!tab->navigation||!tab->expectedNavigation||tab->expectedNavigation!=id)return;if(dom&&tab->wait!="domcontentloaded")return;
        auto op=std::move(tab->navigation);if(!success)op->Finish(Fail("navigation_failed","The browser could not load the page",{{"web_error_status",errorCode}}));else op->Finish(Ok(Info(tab)));
    }
    void Navigate(const std::shared_ptr<Tab>& tab,const std::string& url,const std::string& wait,const std::shared_ptr<Operation>& op,const std::string& operation="navigate"){
        if(wait!="load"&&wait!="domcontentloaded"&&wait!="none"){op->Finish(Fail("invalid_argument","wait must be load, domcontentloaded or none"));return;}
        if(tab->navigation)tab->navigation->Finish(Fail("navigation_replaced","A newer navigation replaced this request"));
        tab->navigation=wait=="none"?nullptr:op;tab->wait=wait;tab->expectedNavigation=0;tab->expectedUrl=operation=="navigate"?NavigationKey(url):std::string();
        HRESULT hr=E_FAIL;
        if(operation=="back")hr=tab->core->GoBack();else if(operation=="forward")hr=tab->core->GoForward();else if(operation=="reload")hr=tab->core->Reload();else hr=tab->core->Navigate(Wide(url).c_str());
        if(FAILED(hr)){tab->navigation.reset();op->Finish(Fail("navigation_failed","The navigation could not be started",{{"hresult",Hr(hr)}}));}
        else if(wait=="none")op->Finish(Ok(Info(tab)));
    }
    void FinishController(const std::shared_ptr<Tab>& tab,const std::shared_ptr<Operation>& op,HRESULT code,ICoreWebView2Controller* controller,bool background){
        if(closing||tab->closed){if(controller)controller->Close();op->Finish(Fail("tab_closed","Tab creation was canceled"));return;}
        if(FAILED(code)||!controller){tab->loading=false;op->Finish(Fail("runtime_error","Could not create the webview controller",{{"hresult",Hr(code)}}));return;}
        tab->controller=controller;HRESULT hr=controller->get_CoreWebView2(&tab->core);
        if(FAILED(hr)||!tab->core){op->Finish(Fail("runtime_error","The controller did not expose a browser"));return;}
        ComPtr<ICoreWebView2Settings> settings;tab->core->get_Settings(&settings);if(settings){settings->put_IsStatusBarEnabled(FALSE);settings->put_AreDevToolsEnabled(TRUE);settings->put_IsWebMessageEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);}
        ComPtr<ICoreWebView2Settings4> passwordSettings;if(settings&&SUCCEEDED(settings.As(&passwordSettings)))passwordSettings->put_IsPasswordAutosaveEnabled(FALSE);
        auto weak=weak_from_this();std::weak_ptr<Tab> wt=tab;EventRegistrationToken token{};
        ComPtr<ICoreWebView2_18> core18;
        if(SUCCEEDED(tab->core.As(&core18))){
            HRESULT blocked=core18->add_LaunchingExternalUriScheme(Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>([weak,wt](ICoreWebView2*,ICoreWebView2LaunchingExternalUriSchemeEventArgs* args)->HRESULT{
                args->put_Cancel(TRUE);
                try{
                    auto state=weak.lock();auto current=wt.lock();if(!state||state->closing||!current||current->closed)return S_OK;
                    LPWSTR raw=nullptr;args->get_Uri(&raw);auto value=Utf8(Take(raw,128));auto colon=value.find(':');std::string scheme="unknown";
                    if(colon!=std::string::npos&&colon>0&&colon<=32){auto candidate=value.substr(0,colon);if(std::all_of(candidate.begin(),candidate.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='+'||c=='-'||c=='.';})){scheme=std::move(candidate);for(auto& c:scheme)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');}}
                    state->Event("external_uri.blocked",current->id,{{"scheme",scheme}});
                    state->Note("An external application URI was blocked; this SDK controls only browser content");
                    if(current->navigation){current->navigation->Finish(Fail("external_uri_blocked","External application URI launches are disabled",{{"scheme",scheme}}));current->navigation.reset();}
                }catch(...){/* The cancel decision was applied before processing untrusted URI text. */}
                return S_OK;
            }).Get(),&token);
            tab->externalUrisBlocked=SUCCEEDED(blocked);
        }
        tab->core->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak,wt](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
            auto s=weak.lock();auto t=wt.lock();if(!s||!t||t->closed)return S_OK;LPWSTR value=nullptr;args->get_Uri(&value);t->url=Utf8(Take(value));UINT64 navigation=0;args->get_NavigationId(&navigation);t->currentNavigation=navigation;if(t->navigation&&!t->expectedNavigation&&(t->expectedUrl.empty()||NavigationKey(t->url)==t->expectedUrl))t->expectedNavigation=navigation;++t->generation;t->context=0;t->loading=true;s->Event("navigation.started",t->id,{{"url",EventUrl(t->url)},{"navigation_id",navigation}});s->RefreshTabs();return S_OK;
        }).Get(),&token);
        tab->core->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([weak,wt](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{
            auto s=weak.lock();auto t=wt.lock();if(!s||!t||t->closed)return S_OK;BOOL success=FALSE;args->get_IsSuccess(&success);COREWEBVIEW2_WEB_ERROR_STATUS error=COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;args->get_WebErrorStatus(&error);UINT64 navigation=0;args->get_NavigationId(&navigation);if(t->currentNavigation==navigation)t->loading=false;s->NavigationDone(t,navigation,false,success!=FALSE,static_cast<int>(error));s->Event("navigation.completed",t->id,{{"success",success!=FALSE},{"web_error_status",static_cast<int>(error)},{"navigation_id",navigation}});s->Note(success?"Ready - automation is local to this Windows user":"Navigation failed - see events.read for details");s->RefreshTabs();return S_OK;
        }).Get(),&token);
        ComPtr<ICoreWebView2_2> core2;if(SUCCEEDED(tab->core.As(&core2)))core2->add_DOMContentLoaded(Callback<ICoreWebView2DOMContentLoadedEventHandler>([weak,wt](ICoreWebView2*,ICoreWebView2DOMContentLoadedEventArgs* args)->HRESULT{auto s=weak.lock();auto t=wt.lock();UINT64 navigation=0;args->get_NavigationId(&navigation);if(s&&t&&!t->closed){s->NavigationDone(t,navigation,true);try{s->MaybeAutoFill(t);}catch(...){s->Note("Saved login auto-fill could not be checked; use Logins to try manually");}}return S_OK;}).Get(),&token);
        tab->core->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>([weak,wt](ICoreWebView2* core,IUnknown*)->HRESULT{auto s=weak.lock();auto t=wt.lock();if(s&&t&&!t->closed){LPWSTR text=nullptr;core->get_DocumentTitle(&text);t->title=Utf8(Take(text,1200));s->RefreshTabs();}return S_OK;}).Get(),&token);
        tab->core->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>([weak,wt](ICoreWebView2* core,ICoreWebView2SourceChangedEventArgs*)->HRESULT{auto s=weak.lock();auto t=wt.lock();if(s&&t&&!t->closed){LPWSTR text=nullptr;core->get_Source(&text);t->url=Utf8(Take(text));s->RefreshTabs();}return S_OK;}).Get(),&token);
        tab->core->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([weak,wt](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{auto s=weak.lock();auto t=wt.lock();if(!s||!t||s->closing)return S_OK;LPWSTR text=nullptr;args->get_Uri(&text);auto url=Utf8(Take(text));args->put_Handled(TRUE);auto request=Json{{"timeout_ms",15000}};auto next=s->NewOperation([](Json){},request);s->CreateTab({{"profile",t->profile},{"in_private",t->inPrivate},{"url",url}},next);return S_OK;}).Get(),&token);
        tab->core->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([weak,wt](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*)->HRESULT{auto s=weak.lock();auto t=wt.lock();if(s&&t){t->ready=false;t->context=0;s->Event("process.failed",t->id);s->Note("A Chromium process failed. Close/reopen the affected tab.");if(t->navigation){t->navigation->Finish(Fail("process_failed","The Chromium process failed"));t->navigation.reset();}}return S_OK;}).Get(),&token);
        ComPtr<ICoreWebView2_4> core4;if(SUCCEEDED(tab->core.As(&core4)))core4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([weak,wt](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* args)->HRESULT{
            auto s=weak.lock();auto t=wt.lock();if(!s||!t||s->closing){args->put_Cancel(TRUE);return S_OK;}if(!s->downloadsEnabled){args->put_Cancel(TRUE);s->Event("download.blocked",t->id);s->Note("Download blocked - enable Downloads in the toolbar or SDK");return S_OK;}
            try{LPWSTR value=nullptr;args->get_ResultFilePath(&value);auto path=fs::path(Take(value));auto filename=path.filename().wstring();if(filename.empty())filename=L"download.bin";for(auto& c:filename)if(c<32||std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos)c=L'_';while(!filename.empty()&&(filename.back()==L'.'||filename.back()==L' '))filename.pop_back();if(filename.empty())filename=L"download.bin";if(filename.size()>160)filename.resize(160);GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("A unique download identity could not be created");wchar_t guidText[40]{};StringFromGUID2(guid,guidText,40);std::wstring identity=guidText;identity.erase(std::remove(identity.begin(),identity.end(),L'{'),identity.end());identity.erase(std::remove(identity.begin(),identity.end(),L'}'),identity.end());path=s->downloadDirectory/(L"download-"+identity+L"-"+filename);if(fs::exists(path))throw std::runtime_error("Unique download destination unexpectedly exists");if(FAILED(args->put_ResultFilePath(path.c_str())))throw std::runtime_error("Download destination could not be set");args->put_Handled(TRUE);ComPtr<ICoreWebView2DownloadOperation> download;if(FAILED(args->get_DownloadOperation(&download))||!download)throw std::runtime_error("Download operation unavailable");auto id=Utf8(identity);Json info{{"id",id},{"tab",t->id},{"path",Utf8(path.wstring())},{"state","started"}};LPWSTR uri=nullptr;download->get_Uri(&uri);info["url"]=EventUrl(Utf8(Take(uri)));s->downloadOperations[id]=download;s->downloads.push_back(info);if(s->downloads.size()>200){s->downloadOperations.erase(s->downloads.front().at("id").get<std::string>());s->downloads.erase(s->downloads.begin());}s->Event("download.started",t->id,info);
            }catch(...){args->put_Cancel(TRUE);s->Event("download.failed",t->id);}return S_OK;
        }).Get(),&token);
        tab->ready=true;tab->controller->put_IsVisible(background?FALSE:TRUE);Layout();if(!background||active.empty())Activate(tab);else RefreshTabs();Event("tab.created",tab->id,{{"profile",tab->profile},{"in_private",tab->inPrivate}});
        const auto initial=tab->initialUrl;if(initial=="about:blank"){tab->loading=false;op->Finish(Ok(Info(tab)));}else Navigate(tab,initial,"load",op);
    }
    void CreateTab(const Json& p,const std::shared_ptr<Operation>& op){
        if(op->done)return;
        if(tabs.size()>=32){op->Finish(Fail("limit_exceeded","At most 32 tabs can be open"));return;}
        auto name=p.value("profile",std::string("default"));if(name=="default"&&p.value("in_private",false))name="private";if(!SafeProfile(name)){op->Finish(Fail("invalid_profile","Use 1-64 ASCII letters, digits, underscores or hyphens for a profile"));return;}
        auto found=profiles.find(name);bool privacy=p.value("in_private",found!=profiles.end()?found->second->inPrivate:false);
        std::shared_ptr<Profile> profile;if(found==profiles.end()){if(profiles.size()>=64){op->Finish(Fail("limit_exceeded","At most 64 profiles can be registered"));return;}profile=std::make_shared<Profile>();profile->name=name;profile->inPrivate=privacy;profiles[name]=profile;try{SaveProfiles();}catch(...){profiles.erase(name);throw;}ProfileList();}else{profile=found->second;if(profile->inPrivate!=privacy){op->Finish(Fail("profile_conflict","The profile already has a different private mode"));return;}}
        auto tab=std::make_shared<Tab>();tab->id="tab-"+std::to_string(nextTab++);tab->profile=name;tab->inPrivate=privacy;tab->initialUrl=p.value("url",std::string("about:blank"));tabs.push_back(tab);RefreshTabs();
        const bool background=p.value("background",false);auto self=shared_from_this();
        auto create=[self,profile,tab,op,background](HRESULT hr){
            if(FAILED(hr)||!profile->environment){op->Finish(Fail("runtime_error","Could not initialize the Chromium profile",{{"hresult",Hr(hr)}}));return;}
            if(op->done||self->closing||tab->closed)return;
            auto handler=Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([weak=self->weak_from_this(),tab,op,background](HRESULT result,ICoreWebView2Controller* controller)->HRESULT{auto s=weak.lock();if(s)s->FinishController(tab,op,result,controller,background);else{if(controller)controller->Close();op->Finish(Fail("browser_closed","The browser closed"));}return S_OK;});
            ComPtr<ICoreWebView2Environment10> modern;
            HRESULT submitted=E_NOINTERFACE;
            if(SUCCEEDED(profile->environment.As(&modern))){ComPtr<ICoreWebView2ControllerOptions> options;submitted=modern->CreateCoreWebView2ControllerOptions(&options);if(SUCCEEDED(submitted)){options->put_ProfileName(L"Default");options->put_IsInPrivateModeEnabled(profile->inPrivate?TRUE:FALSE);submitted=modern->CreateCoreWebView2ControllerWithOptions(self->hwnd,options.Get(),handler.Get());}}
            else if(!profile->inPrivate)submitted=profile->environment->CreateCoreWebView2Controller(self->hwnd,handler.Get());
            if(FAILED(submitted))op->Finish(Fail("not_supported",profile->inPrivate?"The installed runtime does not support private profiles":"Could not create the browser controller",{{"hresult",Hr(submitted)}}));
        };
        if(profile->environment){create(S_OK);return;}profile->waiting.push_back(std::move(create));if(profile->creating)return;profile->creating=true;
        fs::path folder=fs::path(root)/L"Profiles"/ProfileDirectoryName(name);try{fs::create_directories(folder);}catch(const std::exception& e){auto callbacks=std::move(profile->waiting);profile->creating=false;for(auto& f:callbacks)f(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));Note(e.what());return;}
        HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,folder.c_str(),nullptr,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([weak=weak_from_this(),profile](HRESULT result,ICoreWebView2Environment* environment)->HRESULT{auto s=weak.lock();profile->creating=false;if(SUCCEEDED(result)&&environment)profile->environment=environment;auto callbacks=std::move(profile->waiting);for(auto& f:callbacks)f(s&&!s->closing?result:E_ABORT);return S_OK;}).Get());
        if(FAILED(hr)){profile->creating=false;auto callbacks=std::move(profile->waiting);for(auto& f:callbacks)f(hr);}
    }
    void CloseTab(const std::shared_ptr<Tab>& t){
        t->closed=true;t->ready=false;if(t->navigation)t->navigation->Finish(Fail("tab_closed","The tab was closed"));if(t->controller)t->controller->Close();t->core.Reset();t->controller.Reset();
        tabs.erase(std::remove(tabs.begin(),tabs.end(),t),tabs.end());if(active==t->id){active.clear();if(!tabs.empty())Activate(tabs.front());}RefreshTabs();Event("tab.closed",t->id);
    }
    void Timer(){
        const auto now=GetTickCount64();auto operations=pending;for(auto& op:operations)if(!op->done&&now>=op->deadline)op->Finish(op->timeoutResult?op->timeoutResult():Fail("timeout","The operation did not finish before its deadline"));
        pending.erase(std::remove_if(pending.begin(),pending.end(),[](const auto& op){return op->done;}),pending.end());
        auto copy=waiting;for(auto& w:copy){if(w->operation->done||w->evaluating)continue;try{auto tab=w->tab.lock();if(!tab||tab->closed){w->operation->Finish(Fail("tab_closed","The tab closed while waiting"));continue;}w->evaluating=true;auto expression=TargetScript(w->target.dump(),"return {found:!!e,visible:shown};");
            Eval(tab,expression,w->operation,[w](Json value){w->evaluating=false;const bool found=value.value("found",false),shown=value.value("visible",false);if((w->state=="attached"&&found)||(w->state=="visible"&&shown)||(w->state=="hidden"&&!shown))w->operation->Finish(Ok({{"state",w->state},{"found",found},{"visible",shown}}));});
        }catch(const std::exception& exception){w->evaluating=false;w->operation->Finish(Fail("operation_failed",exception.what()));}
        }
        waiting.erase(std::remove_if(waiting.begin(),waiting.end(),[](const auto& w){return w->operation->done;}),waiting.end());
    }
};

Json KeyParameters(const std::string& input,const std::string& type){
    std::string key=input;int modifiers=0;size_t at=0;while((at=key.find('+'))!=std::string::npos){auto mod=key.substr(0,at);if(mod=="Control"||mod=="Ctrl")modifiers|=2;else if(mod=="Alt")modifiers|=1;else if(mod=="Shift")modifiers|=8;else if(mod=="Meta"||mod=="Win")modifiers|=4;else throw std::invalid_argument("Unknown keyboard modifier");key.erase(0,at+1);}
    int vk=0;std::string code=key;std::string text;
    const std::map<std::string,int> special{{"Enter",13},{"Tab",9},{"Escape",27},{"Backspace",8},{"Delete",46},{"ArrowLeft",37},{"ArrowUp",38},{"ArrowRight",39},{"ArrowDown",40},{"Home",36},{"End",35},{"PageUp",33},{"PageDown",34},{"Control",17},{"Shift",16},{"Alt",18},{"Meta",91},{"Space",32}};
    auto match=special.find(key);if(match!=special.end()){vk=match->second;if(key=="Space"){key=" ";text=" ";code="Space";}else if(key=="Enter")text="\r";else if(key=="Tab")text="\t";}
    else if(key.size()==1){unsigned char c=static_cast<unsigned char>(key[0]);vk=static_cast<int>((c>='a'&&c<='z')?c-'a'+'A':c);if((c>='a'&&c<='z')||(c>='A'&&c<='Z'))code="Key"+std::string(1,static_cast<char>(vk));else if(c>='0'&&c<='9')code="Digit"+key;text=key;}
    else if(key.size()<=3&&key[0]=='F'){int f=std::stoi(key.substr(1));if(f<1||f>24)throw std::invalid_argument("Function key out of range");vk=111+f;}
    else throw std::invalid_argument("Unsupported key name; use page.type for Unicode text");
    Json p{{"type",type},{"key",key},{"code",code},{"windowsVirtualKeyCode",vk},{"nativeVirtualKeyCode",vk},{"modifiers",modifiers}};
    if(type=="keyDown"&&!text.empty()&&(modifiers&7)==0){p["text"]=text;p["unmodifiedText"]=text;}return p;
}

void DispatchState(const std::shared_ptr<State>& s,Json request,Reply reply,Reply progress={}){
    std::shared_ptr<Operation> op;
    try{
        if(s->closing){reply(Fail("browser_closed","The browser is closing"));return;}
        const std::string method=request.at("method").get<std::string>();Json p=request.value("params",Json::object());if(!p.is_object())throw std::invalid_argument("params must be an object");
        if(p.contains("timeout_ms"))request["timeout_ms"]=p["timeout_ms"];
        op=s->NewOperation(std::move(reply),request);
        op->progress=std::move(progress);
        if(op->done)return;
        if(method=="browser.info"){
            bool ready=std::any_of(s->tabs.begin(),s->tabs.end(),[](const auto& t){return t->ready&&!t->closed;});op->Finish(Ok({{"version","0.2.0"},{"runtime",s->runtime},{"engine","Microsoft Edge WebView2 / Chromium"},{"ready",ready},{"startup_error",s->error},{"tabs",s->tabs.size()},{"profile_root",Utf8(s->root)},{"transport","current-user named pipe"}}));return;
        }
        if(method=="browser.capabilities"){bool checked=false,blocked=true;for(const auto& open:s->tabs)if(open->ready&&!open->closed){checked=true;blocked=blocked&&open->externalUrisBlocked;}op->Finish(Ok({{"methods",Methods()},{"coordinates","CSS viewport pixels"},{"snapshot_scope","main_frame"},{"native_profiles",true},{"private_profiles_require","WebView2 Environment10"},{"external_uri_launches_blocked",checked&&blocked},{"external_uri_blocking_requires","WebView2 ICoreWebView2_18"},{"host_objects_exposed",false},{"saved_logins",s->vault!=nullptr},{"saved_logins_protection","Windows current-user DPAPI; exact origin and profile"},{"max_batch_actions",20},{"max_tabs",32},{"max_screenshot_pixels",32000000},{"limitations",{"No existing Chrome/Edge profile import","Renderer-supported CDP commands only","Website authentication and access restrictions still apply","Snapshots are untrusted page content","No global operating-system input injection"}}}));return;}
        if(method=="browser.shutdown"){op->Finish(Ok({{"closing",true}}));PostMessageW(s->hwnd,WM_CLOSE,0,0);return;}
        if(method=="tabs.list"){Json result=Json::array();for(auto& t:s->tabs)result.push_back(s->Info(t));op->Finish(Ok(result));return;}
        if(method=="tabs.create"){s->CreateTab(p,op);return;}
        if(method=="profiles.list"){Json value=Json::array();for(const auto& entry:s->profiles)value.push_back({{"name",entry.first},{"in_private",entry.second->inPrivate}});op->Finish(Ok(value));return;}
        if(method=="profiles.create"){
            auto name=p.at("name").get<std::string>();if(!SafeProfile(name))throw std::invalid_argument("Profile names accept 1-64 ASCII letters, digits, hyphens and underscores");bool privacy=p.value("in_private",false);auto it=s->profiles.find(name);
            if(it!=s->profiles.end()&&it->second->inPrivate!=privacy){op->Finish(Fail("profile_conflict","The profile already has another private mode"));return;}
            if(it==s->profiles.end()){if(s->profiles.size()>=64){op->Finish(Fail("limit_exceeded","At most 64 profiles can be open"));return;}auto profile=std::make_shared<Profile>();profile->name=name;profile->inPrivate=privacy;s->profiles[name]=profile;try{s->SaveProfiles();}catch(...){s->profiles.erase(name);throw;}s->ProfileList();}
            op->Finish(Ok({{"name",name},{"in_private",privacy}}));return;
        }
        if(method=="saved_logins.list"||method=="saved_logins.delete"||method=="saved_logins.autofill"){
            if(!s->vault){op->Finish(Fail("vault_unavailable","Saved logins are unavailable",{{"detail",s->vaultError}}));return;}
            auto profile=p.value("profile",std::string());auto origin=p.value("origin",std::string());if(!origin.empty())origin=CredentialVault::NormalizeOrigin(origin);
            Json items=Json::array();for(const auto& entry:s->profiles)if(!entry.second->inPrivate&&(profile.empty()||profile==entry.first))for(auto value:s->vault->List(entry.first,origin))items.push_back(std::move(value));
            if(method=="saved_logins.list"){op->Finish(Ok(items));return;}
            auto id=p.at("credential_id").get<std::string>();auto found=std::find_if(items.begin(),items.end(),[&id](const Json& item){return item.at("id")==id;});if(found==items.end()){op->Finish(Fail("login_not_found","No saved credential matches this credential_id and filter"));return;}
            auto owner=found->at("profile").get<std::string>(),scope=found->at("origin").get<std::string>();
            if(method=="saved_logins.delete"){bool deleted=s->vault->Delete(owner,scope,id);op->Finish(Ok({{"deleted",deleted}}));return;}
            bool enabled=p.at("enabled").get<bool>();s->vault->SetAutoFill(owner,scope,id,enabled);op->Finish(Ok({{"credential_id",id},{"auto_fill",enabled}}));return;
        }
        if(method=="events.read"){
            auto since=p.value("since",uint64_t(0));int limit=p.value("limit",100);if(limit<1||limit>512)throw std::invalid_argument("limit must be 1..512");Json entries=Json::array();for(const auto& e:s->events)if(e.at("seq").get<uint64_t>()>since&&entries.size()<static_cast<size_t>(limit))entries.push_back(e);op->Finish(Ok({{"events",entries},{"cursor",entries.empty()?since:entries.back().at("seq").get<uint64_t>()},{"latest",s->eventSequence},{"dropped",s->dropped}}));return;
        }
        if(method=="downloads.list"){
            Json current=s->downloads;for(auto& item:current){auto found=s->downloadOperations.find(item.at("id").get<std::string>());if(found==s->downloadOperations.end())continue;auto download=found->second;COREWEBVIEW2_DOWNLOAD_STATE state=COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;INT64 received=0,total=-1;download->get_State(&state);download->get_InterruptReason(&reason);download->get_BytesReceived(&received);download->get_TotalBytesToReceive(&total);item["state"]=state==COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED?"completed":state==COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED?"interrupted":"in_progress";item["bytes_received"]=received;item["total_bytes"]=total;item["interrupt_reason"]=static_cast<int>(reason);}op->Finish(Ok(current));return;
        }
        if(method=="downloads.configure"){
            if(p.contains("directory")){auto path=fs::path(Wide(p.at("directory").get<std::string>()));if(!path.is_absolute()||path.wstring().rfind(L"\\\\",0)==0)throw std::invalid_argument("Use an absolute local download directory");fs::create_directories(path);s->downloadDirectory=path;}
            s->downloadsEnabled=p.value("enabled",s->downloadsEnabled);op->Finish(Ok({{"enabled",s->downloadsEnabled},{"directory",Utf8(s->downloadDirectory.wstring())}}));return;
        }
        if(method=="window.set"||method=="window.info"){
            if(method=="window.set"){
                RECT old{};GetWindowRect(s->hwnd,&old);int w=p.value("width",old.right-old.left),h=p.value("height",old.bottom-old.top);if(w<640||w>7680||h<400||h>4320)throw std::invalid_argument("Window dimensions must be 640..7680 by 400..4320");
                SetWindowPos(s->hwnd,p.value("always_on_top",false)?HWND_TOPMOST:HWND_NOTOPMOST,p.value("x",old.left),p.value("y",old.top),w,h,SWP_NOACTIVATE);
                auto state=p.value("state",std::string("normal"));if(state=="normal")ShowWindow(s->hwnd,SW_RESTORE);else if(state=="maximized")ShowWindow(s->hwnd,SW_MAXIMIZE);else if(state=="minimized")ShowWindow(s->hwnd,SW_MINIMIZE);else if(state=="hidden")ShowWindow(s->hwnd,SW_HIDE);else throw std::invalid_argument("Unknown window state");
            }
            RECT box{};GetWindowRect(s->hwnd,&box);op->Finish(Ok({{"x",box.left},{"y",box.top},{"width",box.right-box.left},{"height",box.bottom-box.top},{"visible",IsWindowVisible(s->hwnd)!=FALSE},{"maximized",IsZoomed(s->hwnd)!=FALSE},{"minimized",IsIconic(s->hwnd)!=FALSE}}));return;
        }
        const bool activeAllowed=method=="page.observe"||method=="page.act"||method=="page.login";
        auto id=activeAllowed?p.value("tab",s->active):p.at("tab").get<std::string>();auto tab=s->Find(id);if(!tab){op->Finish(Fail("tab_not_found","This tab ID is not open"));return;}
        if(method=="tabs.close"){s->CloseTab(tab);op->Finish(Ok({{"closed",true}}));return;}
        if(method=="tabs.activate"){s->Activate(tab);op->Finish(Ok(s->Info(tab)));return;}
        if(method=="page.info"){op->Finish(Ok(s->Info(tab)));return;}
        if(!tab->ready||!tab->core){op->Finish(Fail("not_ready","The tab's Chromium controller is not ready"));return;}
        if(method=="page.login"){s->Login(tab,p,op);return;}
        if(method=="page.act"){
            auto actions=p.at("actions");if(!actions.is_array()||actions.empty()||actions.size()>20)throw std::invalid_argument("actions must contain 1..20 action objects");
            const std::set<std::string> allowed{"click","fill","type","key","mouse","scroll","wait","navigate","observe"};
            for(const auto& action:actions){if(!action.is_object()||!action.contains("action")||!action.at("action").is_string()||!allowed.count(action.at("action").get<std::string>()))throw std::invalid_argument("Unsupported batch action");if(action.contains("tab")||action.contains("timeout_ms"))throw std::invalid_argument("Batch actions share one selected tab and one overall timeout");}
            if((p.contains("expected_generation")&&p.at("expected_generation").get<uint64_t>()!=tab->generation)||(p.contains("expected_url")&&p.at("expected_url").get<std::string>()!=tab->url)){op->Finish(Fail("stale_observation","The selected page differs from the expected observation; observe it again before acting"));return;}
            auto receipt=std::make_shared<Json>(Json{{"tab",tab->id},{"status","running"},{"completed",0},{"total",actions.size()},{"results",Json::array()},{"may_have_executed",false}});auto plan=std::make_shared<Json>(std::move(actions));auto index=std::make_shared<size_t>(0);const bool observe=p.value("observe",true);
            op->timeoutResult=[receipt](){auto details=*receipt;details["status"]="timeout";return Fail("timeout","The action batch exceeded its deadline; completed actions are not repeated",std::move(details));};op->Publish(*receipt);
            auto next=std::make_shared<std::function<void()>>();std::weak_ptr<std::function<void()>> weakNext=next;
            *next=[s,tab,op,receipt,plan,index,observe,weakNext](){
                if(op->done)return;
                auto now=GetTickCount64();if(now>=op->deadline){op->Finish(op->timeoutResult());return;}
                const bool stopped=receipt->at("status")=="failed";
                if(*index==plan->size()||stopped){
                    if(!stopped){(*receipt)["status"]="completed";(*receipt)["may_have_executed"]=false;}receipt->erase("in_flight");
                    if(!observe){op->Finish(Ok(*receipt));return;}
                    DispatchState(s,{{"method","page.observe"},{"params",{{"tab",tab->id}}},{"timeout_ms",static_cast<int>(op->deadline-now)}},[op,receipt](Json observed){if(op->done)return;if(observed.value("ok",false))(*receipt)["observation"]=std::move(observed["result"]);else (*receipt)["observation_error"]=std::move(observed["error"]);op->Finish(Ok(*receipt));});return;
                }
                const auto step=*index;Json params=plan->at((*index)++);const auto action=params.at("action").get<std::string>();params.erase("action");if(action=="key"){params["action"]=params.value("key_action",std::string("press"));params.erase("key_action");}params["tab"]=tab->id;
                (*receipt)["in_flight"]=step;(*receipt)["may_have_executed"]=action!="wait"&&action!="observe";op->Publish(*receipt);auto retained=weakNext.lock();
                DispatchState(s,{{"method","page."+action},{"params",std::move(params)},{"timeout_ms",static_cast<int>(op->deadline-now)}},[op,receipt,retained,step,action](Json result){if(op->done)return;bool ok=result.value("ok",false);Json item{{"index",step},{"action",action},{"ok",ok}};if(ok){item["result"]=std::move(result["result"]);(*receipt)["completed"]=receipt->at("completed").get<int>()+1;receipt->erase("in_flight");(*receipt)["may_have_executed"]=false;}else{item["error"]=std::move(result["error"]);(*receipt)["status"]="failed";(*receipt)["failed_index"]=step;receipt->erase("in_flight");}(*receipt)["results"].push_back(std::move(item));op->Publish(*receipt);if(retained)(*retained)();});
            };(*next)();return;
        }
        if(method=="page.navigate"){auto url=p.at("url").get<std::string>();if(url.empty()||url.size()>32768)throw std::invalid_argument("URL length must be 1..32768");s->Navigate(tab,url,p.value("wait",std::string("load")),op);return;}
        if(method=="page.back"||method=="page.forward"||method=="page.reload"){
            if(method!="page.reload"){BOOL possible=FALSE;if(method=="page.back")tab->core->get_CanGoBack(&possible);else tab->core->get_CanGoForward(&possible);if(!possible){op->Finish(Fail("history_unavailable","No history entry exists in that direction"));return;}}
            s->Navigate(tab,"",p.value("wait",std::string("load")),op,method.substr(5));return;
        }
        if(method=="page.stop"){tab->core->Stop();if(tab->navigation){tab->navigation->Finish(Fail("navigation_stopped","Navigation stopped by the client"));tab->navigation.reset();}op->Finish(Ok());return;}
        if(method=="page.cdp"){auto name=p.at("method").get<std::string>();if(name.size()>200||name.find('.')==std::string::npos)throw std::invalid_argument("A renderer CDP Domain.method is required");s->Cdp(tab,name,p.value("params",Json::object()),op,[op](Json value){op->Finish(Ok(value));});return;}
        if(method=="page.evaluate"){auto expression=p.at("expression").get<std::string>();if(expression.size()>512000)throw std::invalid_argument("Expression is too long");auto world=p.value("world",std::string("main"));if(world!="main"&&world!="isolated")throw std::invalid_argument("world must be main or isolated");s->Eval(tab,expression,op,[op](Json value){op->Finish(Ok({{"value",value}}));},p.value("await_promise",true),world=="isolated");return;}
        if(method=="page.snapshot"||method=="page.observe"){
            int maxElements=p.value("max_elements",method=="page.observe"?80:2000),maxText=p.value("max_text",method=="page.observe"?6000:120000);if(maxElements<1||maxElements>(method=="page.observe"?500:2000)||maxText<0||maxText>(method=="page.observe"?100000:120000))throw std::invalid_argument("Observation bounds exceed the supported element/text limits");
            auto role=p.value("role",std::string()),name=p.value("name",std::string());bool include=p.value("include_text",true);auto prefix=tab->id+"g"+std::to_string(tab->generation)+"s"+std::to_string(++tab->snapshot);
            s->Eval(tab,SnapshotScript(Json(prefix).dump()),op,[op,tab,maxElements,maxText,include,role,name](Json value){Json filtered=Json::array();size_t matched=0;for(auto& item:value.at("elements"))if((role.empty()||item.value("role",std::string())==role)&&(name.empty()||item.value("name",std::string())==name)){++matched;if(filtered.size()<static_cast<size_t>(maxElements))filtered.push_back(std::move(item));}value["elements"]=std::move(filtered);if(matched>static_cast<size_t>(maxElements))value["truncated"]=true;if(include){auto text=value.value("text",std::string());if(text.size()>static_cast<size_t>(maxText)){size_t size=static_cast<size_t>(maxText);while(size>0&&size<text.size()&&(static_cast<unsigned char>(text[size])&0xC0)==0x80)--size;text.resize(size);value["truncated"]=true;}value["text"]=std::move(text);}else value.erase("text");value["tab"]=tab->id;value["profile"]=tab->profile;value["in_private"]=tab->inPrivate;value["generation"]=tab->generation;op->Finish(Ok(std::move(value)));});return;
        }
        if(method=="page.wait"){
            if(!p.contains("ref")&&!p.contains("selector")&&!p.contains("name")&&!p.contains("role"))throw std::invalid_argument("A selector, snapshot ref or exact semantic name/role is required");auto state=p.value("state",std::string("visible"));if(state!="visible"&&state!="hidden"&&state!="attached")throw std::invalid_argument("Wait state must be visible, hidden or attached");auto w=std::make_shared<Waiting>();w->tab=tab;w->target=p;w->state=state;w->operation=op;s->waiting.push_back(w);return;
        }
        if(method=="page.click"){
            auto click=[s,tab,p,op](double x,double y){if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0)throw std::invalid_argument("Coordinates must be finite and nonnegative");const auto button=p.value("button",std::string("left"));if(button!="left"&&button!="right"&&button!="middle")throw std::invalid_argument("button must be left, right or middle");int count=p.value("click_count",1);if(count<1||count>3)throw std::invalid_argument("click_count must be 1..3");int mask=button=="left"?1:button=="right"?2:4;s->Sequence(tab,{{"Input.dispatchMouseEvent",{{"type","mouseMoved"},{"x",x},{"y",y}}},{"Input.dispatchMouseEvent",{{"type","mousePressed"},{"x",x},{"y",y},{"button",button},{"buttons",mask},{"clickCount",count}}},{"Input.dispatchMouseEvent",{{"type","mouseReleased"},{"x",x},{"y",y},{"button",button},{"buttons",0},{"clickCount",count}}}},op,{{"clicked",true},{"x",x},{"y",y}});};
            if(p.contains("x")&&p.contains("y"))click(p.at("x").get<double>(),p.at("y").get<double>());else s->Eval(tab,TargetScript(p.dump(),"if(e.disabled||e.matches(':disabled')||e.getAttribute('aria-disabled')==='true') throw new Error('Target is disabled'); e.scrollIntoView({block:'center',inline:'center'}); const r=e.getBoundingClientRect(); const left=Math.max(0,r.left),top=Math.max(0,r.top),right=Math.min(innerWidth,r.right),bottom=Math.min(innerHeight,r.bottom); if(right<=left||bottom<=top)throw new Error('Target is not visible');const x=(left+right)/2,y=(top+bottom)/2,hit=document.elementFromPoint(x,y);if(!hit||!(hit===e||e.contains(hit)))throw new Error('Target is covered by another element');return {found:true,x,y};"),op,[click,op](Json target){if(!target.value("found",false)){op->Finish(Fail("target_not_found","No element matches the selector"));return;}click(target.at("x").get<double>(),target.at("y").get<double>());});return;
        }
        if(method=="page.fill"){
            auto text=p.at("text").get<std::string>();if(text.size()>512000)throw std::invalid_argument("Text is too long");s->Eval(tab,TargetScript(p.dump(),"if(e.disabled||e.readOnly)throw new Error('Target is disabled or read-only'); if(!['INPUT','TEXTAREA'].includes(e.tagName)&&!e.isContentEditable)throw new Error('Target is not editable'); e.scrollIntoView({block:'center'});e.focus();if(e.select)e.select();else{const r=document.createRange();r.selectNodeContents(e);const s=getSelection();s.removeAllRanges();s.addRange(r);}return {found:true};"),op,[s,tab,op,text](Json target){if(!target.value("found",false)){op->Finish(Fail("target_not_found","No element matches the selector"));return;}s->Sequence(tab,{{"Input.insertText",{{"text",text}}}},op,{{"filled",true}});});return;
        }
        if(method=="page.type"){auto text=p.at("text").get<std::string>();if(text.size()>512000)throw std::invalid_argument("Text is too long");s->Cdp(tab,"Input.insertText",{{"text",text}},op,[op](Json){op->Finish(Ok({{"typed",true}}));});return;}
        if(method=="page.key"){
            const auto key=p.at("key").get<std::string>(),action=p.value("action",std::string("press"));std::vector<std::pair<std::string,Json>> sequence;
            if(action=="press"||action=="down")sequence.emplace_back("Input.dispatchKeyEvent",KeyParameters(key,"keyDown"));
            if(action=="press"||action=="up")sequence.emplace_back("Input.dispatchKeyEvent",KeyParameters(key,"keyUp"));
            if(sequence.empty())throw std::invalid_argument("action must be press, down or up");s->Sequence(tab,std::move(sequence),op,{{"key",key},{"action",action}});return;
        }
        if(method=="page.mouse"||method=="page.scroll"){
            auto type=method=="page.scroll"?std::string("wheel"):p.value("type",std::string("move"));std::string mapped=type=="move"?"mouseMoved":type=="down"?"mousePressed":type=="up"?"mouseReleased":type=="wheel"?"mouseWheel":"";if(mapped.empty())throw std::invalid_argument("Mouse type must be move, down, up or wheel");
            double x=p.value("x",1.0),y=p.value("y",1.0);if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0)throw std::invalid_argument("Coordinates must be finite and nonnegative");Json params{{"type",mapped},{"x",x},{"y",y}};auto button=p.value("button",std::string("left"));if(button!="left"&&button!="middle"&&button!="right")throw std::invalid_argument("Unknown mouse button");unsigned mask=button=="left"?1u:button=="right"?2u:4u;if(type=="down")tab->buttons|=mask;else if(type=="up")tab->buttons&=~mask;params["buttons"]=tab->buttons;
            if(type=="down"||type=="up"){params["button"]=button;params["clickCount"]=1;}if(type=="wheel"){params["deltaX"]=p.value("delta_x",0.0);params["deltaY"]=p.value("delta_y",0.0);}
            s->Cdp(tab,"Input.dispatchMouseEvent",params,op,[op](Json){op->Finish(Ok());});return;
        }
        if(method=="page.viewport"){
            int width=p.at("width").get<int>(),height=p.at("height").get<int>();double scale=p.value("device_scale_factor",1.0);if(width<100||width>7680||height<100||height>4320||!std::isfinite(scale)||scale<0.5||scale>4)throw std::invalid_argument("Viewport must be 100..7680 by 100..4320 with scale 0.5..4");s->Cdp(tab,"Emulation.setDeviceMetricsOverride",{{"width",width},{"height",height},{"deviceScaleFactor",scale},{"mobile",false}},op,[tab,width,height,scale,op](Json){tab->viewportWidth=width;tab->viewportHeight=height;tab->scale=scale;op->Finish(Ok({{"width",width},{"height",height},{"device_scale_factor",scale}}));});return;
        }
        if(method=="page.screenshot"){
            auto format=p.value("format",std::string("png"));if(format!="png"&&format!="jpeg")throw std::invalid_argument("format must be png or jpeg");
            if(format=="jpeg"){int quality=p.value("quality",85);if(quality<1||quality>100)throw std::invalid_argument("JPEG quality must be 1..100");}
            s->Eval(tab,"devicePixelRatio",op,[s,tab,p,format,op](Json pixelRatio){
            double dpr=pixelRatio.is_number()?pixelRatio.get<double>():1.0;if(!std::isfinite(dpr)||dpr<=0||dpr>20){op->Finish(Fail("image_too_large","Invalid screenshot pixel ratio"));return;}
            s->Cdp(tab,"Page.getLayoutMetrics",Json::object(),op,[s,tab,p,format,op,dpr](Json layout){
                Json view=layout.value("cssVisualViewport",layout.value("visualViewport",Json::object()));Json content=layout.value("cssContentSize",layout.value("contentSize",Json::object()));
                double x=view.value("pageX",0.0),y=view.value("pageY",0.0),width=view.value("clientWidth",0.0),height=view.value("clientHeight",0.0);bool full=p.value("full_page",false);
                if(full){x=content.value("x",0.0);y=content.value("y",0.0);width=content.value("width",0.0);height=content.value("height",0.0);}
                if(p.contains("clip")){const auto& clip=p.at("clip");x=view.value("pageX",0.0)+clip.value("x",0.0);y=view.value("pageY",0.0)+clip.value("y",0.0);width=clip.at("width").get<double>();height=clip.at("height").get<double>();full=true;}
                if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||x<0||y<0||width<1||height<1||width*dpr>16384||height*dpr>16384||width*height*dpr*dpr>32000000){op->Finish(Fail("image_too_large","Screenshot exceeds the 32-million-pixel/16384-pixel bounds"));return;}
                Json params{{"format",format},{"captureBeyondViewport",full},{"fromSurface",true},{"clip",{{"x",x},{"y",y},{"width",width},{"height",height},{"scale",1}}}};if(format=="jpeg"){int quality=p.value("quality",85);if(quality<1||quality>100)throw std::invalid_argument("JPEG quality must be 1..100");params["quality"]=quality;}
                s->Cdp(tab,"Page.captureScreenshot",params,op,[op,width,height,view,format,dpr](Json data){auto encoded=data.at("data").get<std::string>();if(encoded.size()>60000000){op->Finish(Fail("image_too_large","Encoded screenshot exceeds the response limit"));return;}op->Finish(Ok({{"data",std::move(encoded)},{"mime_type",format=="png"?"image/png":"image/jpeg"},{"width",static_cast<int>(std::ceil(width*dpr))},{"height",static_cast<int>(std::ceil(height*dpr))},{"css_width",width},{"css_height",height},{"device_scale_factor",dpr},{"viewport",view},{"coordinate_space","CSS pixels"}}));});
            });
            });return;
        }
        if(method=="page.settings"){
            auto options=p.value("settings",Json::object());if(!options.is_object())throw std::invalid_argument("settings must be an object");const std::set<std::string> allowed{"javascript","devtools","context_menus","statusbar","zoom_factor","user_agent"};for(auto it=options.begin();it!=options.end();++it)if(!allowed.count(it.key()))throw std::invalid_argument("Unknown setting: "+it.key());ComPtr<ICoreWebView2Settings> settings;tab->core->get_Settings(&settings);if(!settings)throw std::runtime_error("Browser settings unavailable");
            for(const auto* key:{"javascript","devtools","context_menus","statusbar"})if(options.contains(key)&&!options.at(key).is_boolean())throw std::invalid_argument("Boolean setting expected: "+std::string(key));
            if(options.contains("zoom_factor")){double zoom=options.at("zoom_factor").get<double>();if(!std::isfinite(zoom)||zoom<0.25||zoom>5)throw std::invalid_argument("zoom_factor must be 0.25..5");}
            ComPtr<ICoreWebView2Settings2> uaSettings;if(options.contains("user_agent")){auto ua=options.at("user_agent").get<std::string>();if(ua.size()>2048)throw std::invalid_argument("User agent is too long");if(FAILED(settings.As(&uaSettings))){op->Finish(Fail("not_supported","The installed runtime cannot set a user agent"));return;}}
            if(options.contains("javascript"))settings->put_IsScriptEnabled(options.at("javascript").get<bool>()?TRUE:FALSE);if(options.contains("devtools"))settings->put_AreDevToolsEnabled(options.at("devtools").get<bool>()?TRUE:FALSE);if(options.contains("context_menus"))settings->put_AreDefaultContextMenusEnabled(options.at("context_menus").get<bool>()?TRUE:FALSE);if(options.contains("statusbar"))settings->put_IsStatusBarEnabled(options.at("statusbar").get<bool>()?TRUE:FALSE);
            if(options.contains("zoom_factor")){double zoom=options.at("zoom_factor").get<double>();if(!std::isfinite(zoom)||zoom<0.25||zoom>5)throw std::invalid_argument("zoom_factor must be 0.25..5");tab->controller->put_ZoomFactor(zoom);}
            if(options.contains("user_agent"))uaSettings->put_UserAgent(Wide(options.at("user_agent").get<std::string>()).c_str());op->Finish(Ok({{"applied",options}}));return;
        }
        if(method=="page.storage"){
            const auto action=p.value("operation",std::string("get"));std::string script;
            if(action=="get")script="({origin:location.origin,local:Object.fromEntries(Object.entries(localStorage)),session:Object.fromEntries(Object.entries(sessionStorage))})";
            else if(action=="clear")script="(()=>{localStorage.clear();sessionStorage.clear();return {cleared:true,origin:location.origin}})()";
            else if(action=="set"){for(const auto* kind:{"local","session"})if(p.contains(kind)){if(!p.at(kind).is_object())throw std::invalid_argument("Storage entries must be objects");for(const auto& value:p.at(kind))if(!value.is_string())throw std::invalid_argument("Storage values must be strings");}script="(()=>{const p="+p.dump()+";for(const [k,v]of Object.entries(p.local||{}))localStorage.setItem(k,v);for(const [k,v]of Object.entries(p.session||{}))sessionStorage.setItem(k,v);return {origin:location.origin,updated:true}})()";}
            else throw std::invalid_argument("Storage operation must be get, set or clear");s->Eval(tab,script,op,[op](Json value){op->Finish(Ok(value));});return;
        }
        if(method=="page.cookies"){
            ComPtr<ICoreWebView2_2> core;HRESULT hr=tab->core.As(&core);if(FAILED(hr)){op->Finish(Fail("not_supported","Cookie manager is unavailable in this runtime"));return;}ComPtr<ICoreWebView2CookieManager> manager;core->get_CookieManager(&manager);auto action=p.value("operation",std::string("list"));auto url=p.value("url",std::string());
            if(action=="list"){
                hr=manager->GetCookies(Wide(url).c_str(),Callback<ICoreWebView2GetCookiesCompletedHandler>([op](HRESULT code,ICoreWebView2CookieList* list)->HRESULT{
                    if(op->done)return S_OK;
                    try{
                        if(FAILED(code)||!list){op->Finish(Fail("cookie_error","Could not list cookies",{{"hresult",Hr(code)}}));return S_OK;}
                        UINT count=0;if(FAILED(list->get_Count(&count)))throw std::runtime_error("Cookie count unavailable");
                        if(count>10000){op->Finish(Fail("limit_exceeded","Cookie list exceeds the bounded response limit"));return S_OK;}
                        Json items=Json::array();size_t total=0;
                        for(UINT i=0;i<count;++i){
                            ComPtr<ICoreWebView2Cookie> cookie;if(FAILED(list->GetValueAtIndex(i,&cookie))||!cookie)throw std::runtime_error("A cookie became unavailable");
                            CookieText name,value,domain,path;
                            if(FAILED(cookie->get_Name(&name.value))||FAILED(cookie->get_Value(&value.value))||FAILED(cookie->get_Domain(&domain.value))||FAILED(cookie->get_Path(&path.value)))throw std::runtime_error("A cookie property could not be read");
                            auto n=name.Read(4096),v=value.Read(65536),d=domain.Read(4096),p=path.Read(4096);total+=n.size()+v.size()+d.size()+p.size();if(total>8000000)throw std::runtime_error("Cookie data exceeds the response budget");
                            BOOL http=FALSE,secure=FALSE,session=FALSE;double expires=0;COREWEBVIEW2_COOKIE_SAME_SITE_KIND same=COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE;
                            if(FAILED(cookie->get_IsHttpOnly(&http))||FAILED(cookie->get_IsSecure(&secure))||FAILED(cookie->get_IsSession(&session))||FAILED(cookie->get_Expires(&expires))||FAILED(cookie->get_SameSite(&same)))throw std::runtime_error("A cookie attribute could not be read");
                            items.push_back({{"name",n},{"value",v},{"domain",d},{"path",p},{"http_only",http!=FALSE},{"secure",secure!=FALSE},{"session",session!=FALSE},{"expires",expires},{"same_site",same==COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX?"Lax":same==COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT?"Strict":"None"}});
                        }
                        op->Finish(Ok(items));
                    }catch(const std::exception& exception){op->Finish(Fail("cookie_error",exception.what()));}
                    return S_OK;
                }).Get());if(FAILED(hr))op->Finish(Fail("cookie_error","Cookie list could not be requested"));return;
            }
            if(action=="clear"){
                if(url.empty()){hr=manager->DeleteAllCookies();if(FAILED(hr)){op->Finish(Fail("cookie_error","Cookies could not be cleared"));return;}op->Finish(Ok({{"cleared",true}}));return;}
                hr=manager->GetCookies(Wide(url).c_str(),Callback<ICoreWebView2GetCookiesCompletedHandler>([manager,op](HRESULT code,ICoreWebView2CookieList* list)->HRESULT{
                    if(op->done)return S_OK;try{if(FAILED(code)||!list){op->Finish(Fail("cookie_error","Could not find cookies for this URL"));return S_OK;}UINT count=0;if(FAILED(list->get_Count(&count))||count>10000)throw std::runtime_error("Cookie count is unavailable or too large");for(UINT i=0;i<count;++i){ComPtr<ICoreWebView2Cookie> cookie;if(FAILED(list->GetValueAtIndex(i,&cookie))||!cookie)throw std::runtime_error("A cookie became unavailable");if(FAILED(manager->DeleteCookie(cookie.Get()))){op->Finish(Fail("cookie_error","A cookie could not be removed"));return S_OK;}}op->Finish(Ok({{"cleared",true},{"count",count}}));}catch(const std::exception& exception){op->Finish(Fail("cookie_error",exception.what()));}return S_OK;
                }).Get());if(FAILED(hr))op->Finish(Fail("cookie_error","Cookie removal could not be requested"));return;
            }
            if(action!="set")throw std::invalid_argument("Cookie operation must be list, set or clear");auto items=p.at("items");if(!items.is_array()||items.size()>1000)throw std::invalid_argument("items must be a list of at most 1000 cookies");
            for(const auto& item:items){auto name=item.at("name").get<std::string>(),value=item.at("value").get<std::string>(),domain=item.at("domain").get<std::string>(),path=item.value("path",std::string("/"));if(name.empty()||name.size()>4096||value.size()>65536||domain.empty()||domain.size()>1024)throw std::invalid_argument("Cookie field length is invalid");ComPtr<ICoreWebView2Cookie> cookie;hr=manager->CreateCookie(Wide(name).c_str(),Wide(value).c_str(),Wide(domain).c_str(),Wide(path).c_str(),&cookie);if(FAILED(hr)||!cookie)throw std::invalid_argument("Cookie is invalid for the browser");cookie->put_IsHttpOnly(item.value("http_only",false)?TRUE:FALSE);cookie->put_IsSecure(item.value("secure",false)?TRUE:FALSE);auto same=item.value("same_site",std::string("Lax"));if(same!="Lax"&&same!="Strict"&&same!="None")throw std::invalid_argument("same_site must be Lax, Strict or None");cookie->put_SameSite(same=="Lax"?COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX:same=="Strict"?COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT:COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE);if(item.contains("expires"))cookie->put_Expires(item.at("expires").get<double>());hr=manager->AddOrUpdateCookie(cookie.Get());if(FAILED(hr)){op->Finish(Fail("cookie_error","Cookie could not be saved",{{"hresult",Hr(hr)}}));return;}}
            op->Finish(Ok({{"updated",items.size()}}));return;
        }
        op->Finish(Fail("method_not_found","This browser method is not implemented",{{"method",method}}));
    }catch(const VaultError& e){if(op)op->Finish(Fail(e.code,e.what(),{{"windows_error",e.windows_error}}));else if(reply)reply(Fail(e.code,e.what(),{{"windows_error",e.windows_error}}));}
    catch(const Json::exception& e){if(op)op->Finish(Fail("invalid_argument",e.what()));else if(reply)reply(Fail("invalid_argument",e.what()));}
    catch(const std::invalid_argument& e){if(op)op->Finish(Fail("invalid_argument",e.what()));else if(reply)reply(Fail("invalid_argument",e.what()));}
    catch(const std::exception& e){if(op)op->Finish(Fail("operation_failed",e.what()));else if(reply)reply(Fail("operation_failed",e.what()));}
}

LRESULT CALLBACK AddressProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR){
    if(message==WM_KEYDOWN&&wparam==VK_RETURN){SendMessageW(GetParent(window),WM_COMMAND,GoId,0);return 0;}
    if(message==WM_KEYDOWN&&wparam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(window,EM_SETSEL,0,-1);return 0;}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,AddressProc,1);
    return DefSubclassProc(window,message,wparam,lparam);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    auto* raw=reinterpret_cast<State*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){auto* create=reinterpret_cast<CREATESTRUCTW*>(lparam);raw=static_cast<State*>(create->lpCreateParams);raw->hwnd=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));}
    if(!raw)return DefWindowProcW(window,message,wparam,lparam);
    auto s=raw->shared_from_this();
    switch(message){
    case RpcMessage:{std::unique_ptr<Envelope> envelope(reinterpret_cast<Envelope*>(lparam));if(envelope){
        unsigned queued=0;bool claimed=!envelope->phase||envelope->phase->compare_exchange_strong(queued,1);
        if(!claimed||(envelope->canceled&&envelope->canceled->load()))envelope->reply(Fail("timeout","The request expired before UI dispatch"));else DispatchState(s,std::move(envelope->request),std::move(envelope->reply),std::move(envelope->progress));}return 0;}
    case WM_SIZE:s->Layout();return 0;
    case WM_GETMINMAXINFO:{auto* bounds=reinterpret_cast<MINMAXINFO*>(lparam);bounds->ptMinTrackSize={640,400};return 0;}
    case WM_TIMER:s->Timer();return 0;
    case WM_NOTIFY:{auto* n=reinterpret_cast<NMHDR*>(lparam);if(n->idFrom==TabsId&&n->code==TCN_SELCHANGE){int index=TabCtrl_GetCurSel(s->tabsControl);if(index>=0&&static_cast<size_t>(index)<s->tabs.size())s->Activate(s->tabs[static_cast<size_t>(index)]);}return 0;}
    case WM_COMMAND:{int id=LOWORD(wparam);auto t=s->Find(s->active);if(id==NewId){int selected=static_cast<int>(SendMessageW(s->profileBox,CB_GETCURSEL,0,0));wchar_t profile[128]{};if(selected>=0)SendMessageW(s->profileBox,CB_GETLBTEXT,static_cast<WPARAM>(selected),reinterpret_cast<LPARAM>(profile));auto op=s->NewOperation([weak=s->weak_from_this()](Json result){if(auto state=weak.lock())if(!result.value("ok",false))state->Note(result["error"].value("message",std::string("Tab creation failed")));},Json::object());s->CreateTab({{"profile",selected>=0?Utf8(profile):"default"}},op);}
        else if(id==CloseId&&t)s->CloseTab(t);
        else if(id==LoginId){HMENU menu=CreatePopupMenu();const UINT flags=MF_STRING|((t&&t->ready&&!t->inPrivate&&s->vault)?MF_ENABLED:MF_GRAYED);AppendMenuW(menu,flags,SaveLoginId,L"Save current login...");AppendMenuW(menu,flags,FillLoginId,L"Fill saved login");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,flags,AutoLoginId,L"Toggle auto-fill for this origin");AppendMenuW(menu,flags,ForgetLoginId,L"Forget saved login for this origin");RECT button{};GetWindowRect(GetDlgItem(window,LoginId),&button);int chosen=static_cast<int>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,button.left,button.bottom,0,window,nullptr));DestroyMenu(menu);if(chosen)try{s->UiLogin(chosen);}catch(...){s->Note("Saved login operation could not be completed safely");}}
        else if(id==SaveLoginId||id==FillLoginId||id==ForgetLoginId||id==AutoLoginId){try{s->UiLogin(id);}catch(...){s->Note("Saved login operation could not be completed safely");}}
        else if(id==DownloadId){s->downloadsEnabled=!s->downloadsEnabled;SetWindowTextW(GetDlgItem(window,DownloadId),s->downloadsEnabled?L"Downloads: on":L"Downloads: off");s->Note(s->downloadsEnabled?"Downloads enabled for this instance; files are never executed automatically":"Downloads blocked for this instance");}
        else if((id==GoId||id==BackId||id==ForwardId||id==ReloadId)&&t&&t->ready){Json p{{"tab",t->id}};std::string method=id==BackId?"page.back":id==ForwardId?"page.forward":id==ReloadId?"page.reload":"page.navigate";if(id==GoId){int n=GetWindowTextLengthW(s->address);std::wstring text(static_cast<size_t>(n)+1,L'\0');GetWindowTextW(s->address,text.data(),n+1);text.resize(static_cast<size_t>(n));auto url=Utf8(text);if(url.find(':')==std::string::npos)url="https://"+url;p["url"]=url;}DispatchState(s,{{"method",method},{"params",p}},[weak=s->weak_from_this()](Json result){if(auto state=weak.lock())if(!result.value("ok",false))state->Note(result["error"].value("message",std::string("Navigation failed")));});}return 0;}
    case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wparam),White);SetBkColor(reinterpret_cast<HDC>(wparam),Bar);return reinterpret_cast<LRESULT>(s->barBrush);
    case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(wparam),White);SetBkColor(reinterpret_cast<HDC>(wparam),Bar);return reinterpret_cast<LRESULT>(s->barBrush);
    case WM_DRAWITEM:{auto* d=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);if(d->CtlType==ODT_BUTTON){bool pressed=(d->itemState&ODS_SELECTED)!=0;HBRUSH brush=CreateSolidBrush(pressed?Blue:Bar);FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);wchar_t text[128]{};GetWindowTextW(d->hwndItem,text,128);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,White);SelectObject(d->hDC,s->font);DrawTextW(d->hDC,text,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;}break;}
    case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);FillRect(dc,&r,s->navyBrush);RECT bottom{14,r.bottom-StatusHeight,r.right-14,r.bottom};SetBkMode(dc,TRANSPARENT);SetTextColor(dc,White);SelectObject(dc,s->font);std::wstring text=Wide(s->status);DrawTextW(dc,text.c_str(),-1,&bottom,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);EndPaint(window,&paint);return 0;}
    case WM_SETCURSOR:if(LOWORD(lparam)==HTCLIENT){HWND child=reinterpret_cast<HWND>(wparam);if(std::find(s->buttons.begin(),s->buttons.end(),child)!=s->buttons.end()){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}}break;
    case WM_CLOSE:s->closing=true;for(auto& op:s->pending)op->Finish(Fail("browser_closed","The browser is closing"));for(auto& tab:s->tabs){tab->closed=true;if(tab->controller)tab->controller->Close();tab->core.Reset();tab->controller.Reset();}DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);s->hwnd=nullptr;PostQuitMessage(0);return 0;
    default:break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
}

struct Host::Impl {std::shared_ptr<State> state=std::make_shared<State>();};
Host::Host():impl_(std::make_unique<Impl>()){}
Host::~Host(){auto s=impl_->state;if(s->hwnd)SendMessageW(s->hwnd,WM_CLOSE,0,0);}
HWND Host::Window()const noexcept{return impl_->state->hwnd;}
void Host::SetEndpoint(const std::wstring& path){impl_->state->endpoint=path;}
void Host::Dispatch(Json request,Reply reply){DispatchState(impl_->state,std::move(request),std::move(reply));}
bool Host::Create(HINSTANCE instance,const std::wstring& profileRoot,int showCommand){
    auto s=impl_->state;s->instance=instance;s->root=profileRoot;
    s->navyBrush=CreateSolidBrush(Navy);s->barBrush=CreateSolidBrush(Bar);s->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    try{fs::create_directories(s->root);s->downloadDirectory=fs::path(s->root)/L"Downloads";fs::create_directories(s->downloadDirectory);}catch(...){return false;}
    try{s->vault=std::make_unique<CredentialVault>(fs::path(s->root));}catch(const std::exception& exception){s->vaultError=exception.what();}
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"BrowserControlSdkHost";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=s->navyBrush;wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Browser Control SDK",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1240,840,nullptr,nullptr,instance,s.get());if(!window)return false;
    BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
    auto control=[s,window,instance](LPCWSTR cls,LPCWSTR text,DWORD style,int id){HWND h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(s->font),TRUE);return h;};
    s->tabsControl=control(WC_TABCONTROLW,L"",TCS_SINGLELINE|TCS_FOCUSNEVER,TabsId);
    for(auto item:std::vector<std::pair<int,const wchar_t*>>{{BackId,L"\x2190"},{ForwardId,L"\x2192"},{ReloadId,L"\x21bb"},{NewId,L"+"},{CloseId,L"\x00d7"},{GoId,L"Go"},{LoginId,L"Logins"},{DownloadId,L"Downloads: off"}})s->buttons.push_back(control(L"BUTTON",item.second,BS_OWNERDRAW|WS_TABSTOP,item.first));
    s->address=control(L"EDIT",L"about:blank",ES_AUTOHSCROLL|WS_TABSTOP|WS_BORDER,AddressId);SetWindowSubclass(s->address,AddressProc,1,0);s->profileBox=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,ProfileId);
    auto profile=std::make_shared<Profile>();profile->name="default";s->profiles["default"]=profile;
    try{
        auto file=fs::path(s->root)/L"profiles.json";
        if(fs::exists(file)){if(fs::file_size(file)>65536)throw std::runtime_error("Profile metadata exceeds its size limit");std::ifstream stream(file,std::ios::binary);Json records;stream>>records;if(!records.is_array()||records.size()>64)throw std::runtime_error("Profile metadata is malformed");for(const auto& record:records){auto name=record.at("name").get<std::string>();if(!SafeProfile(name))throw std::runtime_error("Profile metadata contains an invalid name");bool privacy=record.at("in_private").get<bool>();if(name=="default"&&privacy)throw std::runtime_error("The default profile cannot be private; use a separate private profile");auto saved=std::make_shared<Profile>();saved->name=name;saved->inPrivate=privacy;s->profiles[name]=saved;}}
        s->SaveProfiles();
    }catch(const std::exception& exception){s->error=std::string("Profile metadata could not be loaded: ")+exception.what();s->Note(s->error);}
    s->ProfileList();s->Layout();SetTimer(window,1,75,nullptr);ShowWindow(window,showCommand);UpdateWindow(window);
    if(!s->error.empty())return true;
    LPWSTR version=nullptr;HRESULT hr=GetAvailableCoreWebView2BrowserVersionString(nullptr,&version);s->runtime=Utf8(Take(version));
    if(FAILED(hr)){s->error="Microsoft Edge WebView2 Runtime is not installed. Install the Evergreen Runtime from Microsoft's official website.";s->Note(s->error);s->Event("runtime.missing","",{{"hresult",Hr(hr)}});return true;}
    auto op=s->NewOperation([weak=s->weak_from_this()](Json result){if(auto state=weak.lock()){if(!result.value("ok",false)){state->error=result["error"].value("message",std::string("Browser startup failed"));state->Note(state->error);}else state->Note("Ready - local SDK automation, isolated browser profiles");}},{{"timeout_ms",60000}});s->CreateTab(Json::object(),op);return true;
}
}
