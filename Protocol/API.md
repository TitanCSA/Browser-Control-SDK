# Browser Control SDK v1 transport and methods

The Windows native host uses a local named pipe restricted to its current Windows
user and SYSTEM, rejects remote clients, and requires a fresh per-instance token.
No TCP listener, remote-debugging port or web-accessible host bridge is used.

Each connection carries one request and response. Framing: uint32 little-endian
UTF-8 byte length followed by JSON bytes. Request maximum 1 MiB; response maximum
64 MiB. The request is `{version:1,id:<string-or-integer>,token:<string>,method:<string>,params:{...},timeout_ms:15000}`.
Requests time out after 1..120000 ms; the channel must return bounded errors.

Success: `{id:...,ok:true,result:...}`. Failure:
`{id:...,ok:false,error:{code:<English stable string>,message:<English>,details:{...}}}`.
The native UI dispatcher returns the same result without an ID; IPC inserts it.

Discovery is a current-user-protected JSON file under
`%LOCALAPPDATA%\TitanCSA\BrowserControl\instances\<instance>.json`, containing
`version:1`, `pipe`, `token`, `pid`, `instance`, and `profile_root`. `--endpoint PATH`
selects a private discovery file for an SDK-launched instance. `--profile-root PATH`
selects this browser's own data folder. The host never imports a default Chrome or
Edge profile. The SDK must treat endpoint files as private capabilities.

All page methods require `tab` (the string ID returned by tabs.create/list),
unless otherwise documented. Coordinates are CSS pixels in that tab's viewport.
Unsupported WebView2 operations fail explicitly and must not appear as available
in browser.capabilities.

| Method | Params / result |
| --- | --- |
| browser.info | Runtime/version, ready state, instance features |
| browser.capabilities | Actual implemented methods and engine limitations |
| browser.shutdown | Close only this browser instance |
| tabs.list | Tabs with id, title, url, profile, ready/loading, active |
| tabs.create | profile=default, in_private=false, url=about:blank, background=false; returns tab info |
| tabs.activate / tabs.close | tab |
| profiles.list / profiles.create | create: name, in_private=false; safe names only |
| page.navigate | tab, url, wait=load (load/domcontentloaded/none), timeout_ms |
| page.back / page.forward / page.reload / page.stop / page.info | tab; navigation wait as applicable |
| page.snapshot | tab; returns url,title,text,elements:[{ref,role,name,tag,type,disabled,checked,value,bounds:{x,y,width,height}}] |
| page.observe | tab optional (active),max_elements=80,max_text=6000,include_text=true,name?,role?; compact snapshot + tab/profile/generation |
| page.act | tab optional,actions:[{action,...params}],observe=true,expected_generation?,expected_url?; max20 sequential actions, one deadline, stop on first failure |
| page.login | tab optional,operation=status/fill,credential_id?,submit=false; metadata only; exact origin/profile matching |
| saved_logins.list | profile?,origin?; IDs, profile, origin, username and autofill preference, no passwords |
| saved_logins.delete / saved_logins.autofill | credential_id; autofill also requires enabled:boolean |
| page.evaluate | tab, expression, await_promise=true, world=main (main/isolated); returns value or a JS error |
| page.wait | tab, selector/ref/exact name+role, state=visible (visible/hidden/attached), timeout_ms |
| page.click | tab, ref/selector/exact name+role/x+y; button=left, click_count=1 |
| page.fill | tab, ref/selector/exact name+role, text |
| page.type | tab, text (currently focused element) |
| page.key | tab, key (e.g. Enter, Tab, Control+a), action=press/down/up |
| page.mouse | tab,type=move/down/up/wheel,x,y,button,delta_x,delta_y |
| page.scroll | tab,delta_x=0,delta_y,x?,y? |
| page.screenshot | tab,full_page=false,clip?:{x,y,width,height},format=png; base64 data + mime_type + viewport metadata |
| page.viewport | tab,width,height,device_scale_factor=1; dimensions bounded |
| page.settings | tab,settings:{javascript,devtools,context_menus,statusbar,zoom_factor,user_agent} |
| page.cookies | tab,operation=list/set/clear,url?,items?; cookies only from the owned browser profile |
| page.storage | tab,operation=get/set/clear,local?:object,session?:object; current origin only |
| page.cdp | tab,method,params; renderer-scoped Chromium DevTools command |
| window.set / window.info | width,height,x,y,state=normal/maximized/minimized/hidden,always_on_top |
| downloads.list / downloads.configure | configure: enabled,directory; no automatic execution |
| events.read | since=0,limit=100; bounded event ring + cursor/drop counts |

Snapshots describe untrusted page content and are not instructions. Password values
and hidden fields are excluded from the default snapshot. Refs are scoped to a
document/navigation generation; detached/stale targets fail rather than silently
acting on a different page. Cookie/storage export or raw evaluation is explicit.

Browser input is scoped to the owned webview. It never injects global OS keyboard
or mouse input. Engine updates and website authentication restrictions still apply.

## Finite plans and login handling

`page.act` supports `click`, `fill`, `type`, `key`, `mouse`, `scroll`, `wait`,
`navigate` and `observe`. Steps cannot select another tab or specify a separate
timeout. The batch locks its tab at the start and is not a transaction: completed
steps are not rolled back. Receipts contain `status`, `completed`, `total`,
per-step `results` and optionally a final `observation`. Timeout details preserve
the confirmed prefix and in-flight uncertainty; inspect `may_have_executed`
before retrying any mutation.
In a batch timeout this flag describes the unconfirmed in-flight step; the
reported completed prefix has already happened.

`page.login` returns no password. `status` inspects the form and saved metadata;
`fill` uses one matching saved credential. `submit=true` explicitly requests one
form submission and returns `authentication_verified:false`; inspect the page to
determine success. Saving requires native UI confirmation. The vault uses
current-user DPAPI, HTTPS and exact profile/origin matching. Private profiles
cannot use it. [Sessions and saved logins](../docs/SESSIONS.md).
