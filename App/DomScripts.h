#pragma once
#include <string>

namespace BrowserControl {
inline std::string SemanticHelpers() {
    return R"JS(
const visible = e => { const r=e.getBoundingClientRect(), s=getComputedStyle(e); return r.width>0 && r.height>0 && s.visibility!=='hidden' && s.display!=='none'; };
const clean = s => String(s||'').replace(/\s+/g,' ').trim().slice(0,1200);
const role = e => e.getAttribute('role') || ({BUTTON:'button',A:'link',SELECT:'combobox',TEXTAREA:'textbox',SUMMARY:'button',IMG:'img'}[e.tagName]) || (e.tagName==='INPUT' ? ({checkbox:'checkbox',radio:'radio',range:'slider',button:'button',submit:'button'}[e.type] || 'textbox') : 'element');
const name = e => {
 let n=e.getAttribute('aria-label');
 if (!n && e.getAttribute('aria-labelledby')) n=e.getAttribute('aria-labelledby').split(/\s+/).map(id=>document.getElementById(id)?.innerText||'').join(' ');
 if (!n && e.labels) n=Array.from(e.labels).map(l=>l.innerText).join(' ');
 return clean(n || e.getAttribute('alt') || e.getAttribute('title') || e.getAttribute('placeholder') || e.innerText || (['button','submit'].includes(e.type)?e.value:''));
};
const candidateSelector='a[href],button,input:not([type=hidden]),textarea,select,summary,[role],[contenteditable=true],[tabindex]';
)JS";
}
inline std::string SnapshotScript(const std::string& prefix) {
    return R"JS((() => {
const prefix = )JS" + prefix + R"JS(;
)JS" + SemanticHelpers() + R"JS(
const candidates = Array.from(document.querySelectorAll(candidateSelector));
const refs = new Map(); const elements=[];
for (const e of candidates) {
 if (!visible(e)) continue;
 const ref=prefix+'-'+(elements.length+1); refs.set(ref,e);
 const r=e.getBoundingClientRect();
 const item={ref,role:role(e),name:name(e),tag:e.tagName.toLowerCase(),type:e.type||'',disabled:!!e.disabled,checked:!!e.checked,
 bounds:{x:r.x,y:r.y,width:r.width,height:r.height}};
 if (e.type==='password' || /(?:^|\s)(?:username|current-password|new-password|one-time-code)(?:\s|$)/.test(e.autocomplete||'') || (e.form?.querySelector('input[type=password]') && ['text','email','tel'].includes(e.type))) item.value='[redacted]';
 else if ('value' in e) item.value=String(e.value).slice(0,4000);
 if (e.tagName==='A') item.href=String(e.href).slice(0,4096);
 elements.push(item); if(elements.length>=2000) break;
}
globalThis.__bcsRefs=refs; globalThis.__bcsPrefix=prefix;
return {url:String(location.href).slice(0,32768),title:String(document.title).slice(0,1200),text:(document.body?.innerText||'').slice(0,120000),elements,
 viewport:{width:innerWidth,height:innerHeight,device_scale_factor:devicePixelRatio},
 frames:Array.from(document.querySelectorAll('iframe')).slice(0,64).map(f=>({title:String(f.title).slice(0,1200),src:String(f.src).slice(0,4096)})),
 scope:'main_frame',page_content_is_untrusted:true,truncated:elements.length>=2000};
})())JS";
}

inline std::string TargetScript(const std::string& targetJson, const std::string& action) {
    return R"JS((() => { const p=)JS" + targetJson + ";" + SemanticHelpers() + R"JS(
let e;
if(p.ref){ if(!globalThis.__bcsRefs || !p.ref.startsWith(globalThis.__bcsPrefix+'-')) throw new Error('STALE_REF: take a fresh snapshot'); e=globalThis.__bcsRefs.get(p.ref); if(!e || !e.isConnected) throw new Error('STALE_REF: target is detached'); }
else if(p.selector) {e=document.querySelector(p.selector);if(!e)return {found:false};}
else if(typeof p.name==='string'||typeof p.role==='string') {
 const matches=Array.from(document.querySelectorAll(candidateSelector)).filter(n=>visible(n)&&(p.name===undefined||name(n)===p.name)&&(p.role===undefined||role(n)===p.role));
 if(matches.length>1)throw new Error('AMBIGUOUS_TARGET: more than one visible element has this exact name and role');
 if(!matches.length)return {found:false};e=matches[0];
} else throw new Error('A selector, snapshot ref or exact semantic name/role is required');
const r0=e.getBoundingClientRect(),s=getComputedStyle(e);
const shown=r0.width>0&&r0.height>0&&s.display!=='none'&&s.visibility!=='hidden';
)JS" + action + R"JS(
})())JS";
}

// Main-frame login selection deliberately rejects ambiguous forms. Internal
// callers never forward secret-bearing results or JavaScript error text to RPC.
inline std::string LoginFormHelpers() {
    return R"JS(
const shown=e=>{const r=e.getBoundingClientRect(),s=getComputedStyle(e);return r.width>0&&r.height>0&&s.visibility!=='hidden'&&s.display!=='none'&&!e.disabled&&!e.readOnly;};
let passwords=Array.from(document.querySelectorAll('input[type=password]')).filter(e=>shown(e)&&!/(?:^|\s)new-password(?:\s|$)/.test(e.autocomplete||''));
const focused=document.activeElement, focusedForm=focused?.form;
if(focusedForm){const own=passwords.filter(e=>e.form===focusedForm);if(own.length)passwords=own;}
if(passwords.length!==1)return {status:passwords.length?'ambiguous_form':'no_login_form',origin:location.origin};
const password=passwords[0],form=password.form;
const inputs=Array.from((form||document).querySelectorAll('input')).filter(e=>shown(e)&&['text','email','tel'].includes(e.type));
let usernames=inputs.filter(e=>/(?:^|\s)username(?:\s|$)/.test(e.autocomplete||''));
if(usernames.length>1)return {status:'ambiguous_form',origin:location.origin};
const before=inputs.filter(e=>!!(e.compareDocumentPosition(password)&Node.DOCUMENT_POSITION_FOLLOWING));
const username=usernames[0]||before[before.length-1]||null;
const hinted=/(?:^|\s)current-password(?:\s|$)/.test(password.autocomplete||'')||usernames.length===1;
const ordinary=!!form&&!!username&&form.querySelectorAll('input:not([type=hidden]):not([type=submit]):not([type=button])').length<=3&&!!form.querySelector('button[type=submit],input[type=submit],button:not([type])');
if(!hinted&&!ordinary)return {status:'no_login_form',origin:location.origin};
)JS";
}
inline std::string LoginInspectScript(bool includeSecret) {
    return std::string("(()=>{") + LoginFormHelpers() + (includeSecret
        ? "return {status:'ready',origin:location.origin,username:username?.value||'',password:password.value};})()"
        : "return {status:'ready',origin:location.origin,has_username:!!username,password_present:!!password.value};})()");
}
inline std::string LoginFillScript(const std::string& credentialJson) {
    return std::string("(()=>{const credential=") + credentialJson + ";if(location.origin!==credential.origin)return {status:'origin_changed'};" + LoginFormHelpers() + R"JS(
if(location.origin!==credential.origin)return {status:'origin_changed'};
const set=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')?.set;
if(!set)return {status:'unsupported_form'};
if(username){set.call(username,credential.username);username.dispatchEvent(new Event('input',{bubbles:true}));username.dispatchEvent(new Event('change',{bubbles:true}));}
if(location.origin!==credential.origin||!password.isConnected)return {status:'origin_changed'};
set.call(password,credential.password);password.dispatchEvent(new Event('input',{bubbles:true}));password.dispatchEvent(new Event('change',{bubbles:true}));
let submitted=false;
if(credential.submit){if(!form||typeof HTMLFormElement.prototype.requestSubmit!=='function')return {status:'filled',filled:true,submitted:false,submit_unavailable:true};HTMLFormElement.prototype.requestSubmit.call(form);submitted=true;}
return {status:'filled',filled:true,submitted};})())JS";
}
}
