#pragma once
#include <string>

namespace BrowserControl {
inline std::string SnapshotScript(const std::string& prefix) {
    return R"JS((() => {
const prefix = )JS" + prefix + R"JS(;
const visible = e => { const r=e.getBoundingClientRect(), s=getComputedStyle(e); return r.width>0 && r.height>0 && s.visibility!=='hidden' && s.display!=='none'; };
const clean = s => String(s||'').replace(/\s+/g,' ').trim().slice(0,1200);
const role = e => e.getAttribute('role') || ({BUTTON:'button',A:'link',SELECT:'combobox',TEXTAREA:'textbox',SUMMARY:'button',IMG:'img'}[e.tagName]) || (e.tagName==='INPUT' ? ({checkbox:'checkbox',radio:'radio',range:'slider',button:'button',submit:'button'}[e.type] || 'textbox') : 'element');
const name = e => {
 let n=e.getAttribute('aria-label');
 if (!n && e.getAttribute('aria-labelledby')) n=e.getAttribute('aria-labelledby').split(/\s+/).map(id=>document.getElementById(id)?.innerText||'').join(' ');
 if (!n && e.labels) n=Array.from(e.labels).map(l=>l.innerText).join(' ');
 return clean(n || e.getAttribute('alt') || e.getAttribute('title') || e.getAttribute('placeholder') || e.innerText || (['button','submit'].includes(e.type)?e.value:''));
};
const candidates = Array.from(document.querySelectorAll('a[href],button,input:not([type=hidden]),textarea,select,summary,[role],[contenteditable=true],[tabindex]'));
const refs = new Map(); const elements=[];
for (const e of candidates) {
 if (!visible(e)) continue;
 const ref=prefix+'-'+(elements.length+1); refs.set(ref,e);
 const r=e.getBoundingClientRect();
 const item={ref,role:role(e),name:name(e),tag:e.tagName.toLowerCase(),type:e.type||'',disabled:!!e.disabled,checked:!!e.checked,
 bounds:{x:r.x,y:r.y,width:r.width,height:r.height}};
 if (e.type==='password') item.value='[redacted]';
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
    return R"JS((() => { const p=)JS" + targetJson + R"JS(;
let e;
if(p.ref){ if(!globalThis.__bcsRefs || !p.ref.startsWith(globalThis.__bcsPrefix+'-')) throw new Error('STALE_REF: take a fresh snapshot'); e=globalThis.__bcsRefs.get(p.ref); if(!e || !e.isConnected) throw new Error('STALE_REF: target is detached'); }
else { if(typeof p.selector!=='string'||!p.selector) throw new Error('A selector or ref is required'); e=document.querySelector(p.selector); if(!e) return {found:false}; }
const r0=e.getBoundingClientRect(),s=getComputedStyle(e);
const shown=r0.width>0&&r0.height>0&&s.display!=='none'&&s.visibility!=='hidden';
)JS" + action + R"JS(
})())JS";
}
}
