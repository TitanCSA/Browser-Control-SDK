"""Exercise the actual browser on an owned localhost fixture, not third-party accounts."""
from pathlib import Path
import argparse
import concurrent.futures
import json
import statistics
import struct
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'SDK/Python'))
from browser_control import Browser, BrowserError


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def dimensions(path):
    data = Path(path).read_bytes()
    require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Screenshot is not a PNG')
    return struct.unpack('>II', data[16:24])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--output')
    args = parser.parse_args()
    out = Path(args.output) if args.output else ROOT / 'obj/Validation' / uuid.uuid4().hex
    out.mkdir(parents=True, exist_ok=True)
    ready = out / 'fixture-ready.json'
    fixture = subprocess.Popen([sys.executable, str(ROOT/'Tests/fixture_server.py'),
                                '--ready-file', str(ready)], stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE)
    browser = None
    report = {'scope':'Owned localhost fixture only; no external accounts or sites', 'cases':[]}
    def case(name, function):
        start = time.perf_counter()
        try:
            function()
            report['cases'].append({'name':name,'passed':True,'ms':round((time.perf_counter()-start)*1000,2)})
            print('PASS ' + name, flush=True)
        except Exception as error:
            item={'name':name,'passed':False,'error':str(error)}
            if isinstance(error,BrowserError):item['details']=error.details
            report['cases'].append(item)
            print('FAIL ' + name + ': ' + str(error), flush=True)
    try:
        deadline = time.monotonic()+10
        while not ready.exists() and time.monotonic()<deadline:
            require(fixture.poll() is None,'Local fixture server exited')
            time.sleep(.05)
        base = json.loads(ready.read_text(encoding='utf-8'))['url']
        private_profiles = Browser.instances_directory().parent/'Validation'/uuid.uuid4().hex/'Profiles'
        browser = Browser.launch(executable=args.exe, profile_root=private_profiles, timeout_ms=60000)
        tab = browser.new_tab()
        tab.goto(base)
        report['browser'] = browser.info()
        report['capabilities'] = browser.capabilities()

        def snapshot():
            state = tab.snapshot()
            text = json.dumps(state)
            require('LOCAL-FIXTURE-PASSWORD-NOT-A-CREDENTIAL' not in text,'Password leaked into snapshot')
            require('LOCAL-HIDDEN-FIXTURE' not in text,'Hidden value leaked into snapshot')
            items = state['elements']
            require(any(e.get('name')=='Your name' for e in items),'Labelled input missing')
            require(any(e.get('name')=='Save example' for e in items),'Button missing')
            require(any(e.get('disabled') for e in items),'Disabled state missing')
        case('Semantic snapshot, labels, state and secret-field redaction',snapshot)

        def form():
            state = tab.snapshot()
            ref = next(e['ref'] for e in state['elements'] if e.get('name')=='Your name')
            tab.fill('SDK example',ref=ref)
            tab.click(selector='#agree')
            tab.click(selector='#save')
            result = tab.evaluate('document.querySelector("#result").textContent')
            require(result=='Saved: SDK example | linear | true','Filled form or checkbox output differs')
            events = tab.evaluate('window.fixtureEvents')
            require(any(e['type']=='input' and e['id']=='name' for e in events),'No input event was dispatched')
        case('Reference click/fill, checkbox and real input events',form)

        def keys():
            tab.click(selector='#name')
            tab.press('Control+a')
            tab.type('Keyboard text')
            tab.press('Enter')
            require(tab.evaluate('document.querySelector("#name").value')=='Keyboard text','Keyboard text mismatch')
            require(tab.evaluate('document.querySelector("#key-result").textContent')=='Key: Enter','Enter event missing')
        case('Focused typing and keyboard shortcut/events',keys)

        def pointer():
            tab.evaluate('document.querySelector("#pointer").scrollIntoView({block:"center"})')
            rect = tab.evaluate('(()=>{const r=document.querySelector("#pointer").getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2}})()')
            tab.mouse('move',rect['x'],rect['y'])
            tab.click(x=rect['x'],y=rect['y'])
            require(tab.evaluate('document.querySelector("#pointer").textContent')=='Pointer click received','Pointer event missing')
            before = tab.evaluate('scrollY')
            tab.scroll(350)
            time.sleep(.15)
            require(tab.evaluate('scrollY')>before,'Wheel did not scroll')
        case('CSS-coordinate mouse movement, click and wheel',pointer)

        def delayed():
            tab.click(selector='#delayed')
            tab.wait(selector='#async-ready',timeout_ms=5000)
            require(tab.evaluate('document.querySelector("#async-ready").textContent')=='Delayed button ready','Delayed DOM wait failed')
        case('Asynchronous DOM wait without a fixed page sleep',delayed)

        def stale():
            state = tab.snapshot()
            target = next(e['ref'] for e in state['elements'] if e.get('name')=='Original node')
            tab.click(selector='#replace')
            try:
                tab.click(ref=target)
            except BrowserError:
                return
            raise AssertionError('Detached reference silently acted on a replacement')
        case('Detached snapshot references are rejected',stale)

        def screenshots():
            tab.evaluate('scrollTo(0,0)')
            tab.screenshot(path=out/'viewport.png')
            tab.screenshot(path=out/'full-page.png',full_page=True)
            tab.screenshot(path=out/'region.png',clip={'x':10,'y':10,'width':320,'height':180})
            view = dimensions(out/'viewport.png')
            full = dimensions(out/'full-page.png')
            clip = dimensions(out/'region.png')
            require(full[1]>view[1],'Full-page image did not extend beyond viewport')
            require(clip==(320,180),'Region image dimensions differ')
        case('Viewport, full-page and selected-region screenshots',screenshots)

        def live_frames():
            stream=tab.frames(interval_ms=0,clip={'x':0,'y':0,'width':400,'height':100})
            tab.evaluate('document.querySelector("h1").textContent="Live frame one"')
            first=next(stream)
            tab.evaluate('document.querySelector("h1").textContent="Live frame two"')
            second=next(stream)
            stream.close()
            require(first['data']!=second['data'],'Continuous capture did not reflect a page change')
            tab.evaluate('document.querySelector("h1").textContent="Browser Control SDK"')
        case('Explicit live-region frame iterator reflects page changes',live_frames)

        def layout_settings():
            browser.window(width=1150,height=850,state='normal')
            tab.viewport(1000,700)
            require(tab.evaluate('innerWidth')==1000 and tab.evaluate('innerHeight')==700,'Viewport override differs')
            tab.settings(zoom_factor=1.0,user_agent='BrowserControlSDK-LocalFixture/0.1.0',context_menus=False)
            require(tab.evaluate('navigator.userAgent')=='BrowserControlSDK-LocalFixture/0.1.0','User-agent setting did not apply')
        case('Window, viewport and native browser settings',layout_settings)

        def cookie_roundtrip():
            tab.cookies('set',items=[{'name':'sdk_http_only','value':'owned-test-value','domain':'127.0.0.1',
                                    'path':'/','http_only':True,'secure':False,'same_site':'Lax'}])
            items=tab.cookies('list',url=base)
            require(any(c['name']=='sdk_http_only' and c['http_only'] for c in items),'Cookie attributes did not roundtrip')
            require('sdk_http_only' not in tab.evaluate('document.cookie'),'HttpOnly cookie leaked through page script')
            tab.cookies('clear',url=base)
        case('Explicit SDK cookie creation and HttpOnly semantics',cookie_roundtrip)

        def storage():
            tab.storage('set',local={'sdk_test':'local'},session={'sdk_test':'session'})
            state = tab.storage()
            require(state['local']['sdk_test']=='local' and state['session']['sdk_test']=='session','Storage roundtrip failed')
            tab.storage('clear')
            require(tab.evaluate('localStorage.getItem("sdk_test")') is None,'Storage clear failed')
        case('Explicit local/session storage roundtrip and clear',storage)

        def profiles():
            a = browser.new_tab(profile='isolation-a')
            b = browser.new_tab(profile='isolation-b')
            a.goto(base)
            b.goto(base)
            a.evaluate('document.cookie="sdk_profile=A; SameSite=Lax; Path=/"')
            require('sdk_profile=A' in a.evaluate('document.cookie'),'Cookie was not set')
            require('sdk_profile=A' not in b.evaluate('document.cookie'),'Cookie crossed profile boundary')
            a.cookies('list',url=base)
            a.cookies('clear',url=base)
            require('sdk_profile=' not in a.evaluate('document.cookie'),'Cookie clearing did not affect requested profile')
            a.close();b.close()
        case('Independent profile cookies and SDK cookie clearing',profiles)

        def case_sensitive_profiles():
            upper=browser.new_tab(profile='Work')
            lower=browser.new_tab(profile='work')
            upper.goto(base);lower.goto(base)
            upper.evaluate('document.cookie="case_isolation=upper; SameSite=Lax; Path=/"')
            require('case_isolation=upper' not in lower.evaluate('document.cookie'),'Case variants share a Windows data folder')
            private=browser.new_tab(profile='private-check',in_private=True)
            private.goto(base)
            require(private.info()['in_private'] is True,'Private context flag missing')
            require('case_isolation=upper' not in private.evaluate('document.cookie'),'Persistent cookie crossed into private profile')
            private.close();upper.close();lower.close();tab.activate()
        case('Case-safe profile directories and private context isolation',case_sensitive_profiles)

        def downloads():
            directory=private_profiles.parent/'Downloads'
            browser.downloads(enabled=True,directory=str(directory))
            tab.click(selector='#download')
            end=time.monotonic()+5
            files=[]
            while time.monotonic()<end:
                files=list(directory.glob('*.txt'))
                if files and files[0].read_bytes()==b'Browser Control SDK owned local download fixture.\n':break
                time.sleep(.05)
            require(bool(files) and files[0].read_bytes()==b'Browser Control SDK owned local download fixture.\n','Download bytes differ')
            require(bool(browser.downloads()),'Download list did not record the file')
            browser.downloads(enabled=False)
        case('Explicit download configuration, byte content and tracking',downloads)

        def external_uri():
            import winreg
            info=tab.info()
            require(info.get('external_uri_launches_blocked') is True,'External URI blocking is not active in this runtime')
            # WebView2 raises this event only for a Windows-registered protocol.
            # This test owns a temporary HKCU registration whose handler does not
            # exist, so even a regression cannot launch an external application.
            scheme='bcsdk-test-'+uuid.uuid4().hex[:16]
            key_name='Software\\Classes\\'+scheme
            missing_exe=(out/('INTENTIONALLY-NONEXISTENT-'+uuid.uuid4().hex+'.exe')).resolve()
            require(not missing_exe.exists(),'External URI fixture handler unexpectedly exists')
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER,key_name):pass
            except FileNotFoundError:
                pass
            else:
                raise AssertionError('Refusing to modify an existing protocol registration')
            created=False
            try:
                with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,key_name) as key:
                    created=True
                    winreg.SetValueEx(key,'',0,winreg.REG_SZ,'URL:Owned Browser Control validation')
                    winreg.SetValueEx(key,'URL Protocol',0,winreg.REG_SZ,'')
                with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,key_name+'\\shell\\open\\command') as key:
                    winreg.SetValueEx(key,'',0,winreg.REG_SZ,'"'+str(missing_exe)+'" "%1"')
                href=scheme+'://blocked?test-private-query=hidden'
                tab.evaluate('(()=>{const a=document.createElement("a");a.id="owned-external-uri-test";a.href='+json.dumps(href)+';a.textContent="Owned external URI test";document.body.prepend(a);return true})()')
                tab.click(selector='#owned-external-uri-test')
                end=time.monotonic()+3
                events=[]
                while time.monotonic()<end:
                    state=browser.events()
                    events=state.get('events',[]) if isinstance(state,dict) else state
                    if any(e.get('type')=='external_uri.blocked' and e.get('data',{}).get('scheme')==scheme for e in events):break
                    time.sleep(.05)
                require(any(e.get('type')=='external_uri.blocked' and e.get('data',{}).get('scheme')==scheme for e in events),'Registered external URI event was not blocked')
                require('test-private-query' not in json.dumps(events),'External URI query leaked into diagnostics')
            finally:
                try:tab.evaluate('document.querySelector("#owned-external-uri-test")?.remove()')
                except BrowserError:pass
                if created:
                    for suffix in ('\\shell\\open\\command','\\shell\\open','\\shell',''):
                        try:winreg.DeleteKey(winreg.HKEY_CURRENT_USER,key_name+suffix)
                        except FileNotFoundError:pass
        case('Website external-application launches blocked and redacted',external_uri)

        def navigation():
            state = tab.snapshot()
            old_ref = state['elements'][0]['ref']
            tab.goto(base+'/second')
            require(tab.evaluate('document.title')=='Second local page','Navigation title mismatch')
            try:
                tab.click(ref=old_ref)
            except BrowserError:
                pass
            else:
                raise AssertionError('A pre-navigation reference stayed active')
            tab.back()
            require(tab.evaluate('document.title')=='Browser Control SDK - Local Automation Fixture','Back history failed')
            tab.forward()
            tab.reload()
            tab.goto(base)
        case('Navigation/history and document-scoped references',navigation)

        def cdp():
            value = tab.cdp('Runtime.evaluate',{'expression':'6*7','returnByValue':True})
            require(value['result']['value']==42,'Raw CDP response differs')
        case('Explicit renderer CDP command',cdp)

        def bad_token():
            endpoint = json.loads(browser.endpoint_path.read_text(encoding='utf-8'))
            endpoint['token'] = '0'*64
            wrong = out/'wrong-token.json'
            wrong.write_text(json.dumps(endpoint),encoding='utf-8')
            try:
                Browser(wrong).info()
            except BrowserError:
                require(browser.info().get('ready',False),'Valid owner lost control after invalid token')
                return
            raise AssertionError('Invalid capability token was accepted')
        case('Invalid local capability token refused',bad_token)

        def concurrency():
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                values = list(pool.map(lambda n:tab.evaluate(str(n)+'*2'),range(12)))
            require(values==[n*2 for n in range(12)],'Concurrent replies were lost or cross-wired')
        case('Bounded concurrent clients preserve response identity',concurrency)

        def latency():
            samples=[]
            for unused in range(30):
                start=time.perf_counter_ns();require(tab.evaluate('21*2')==42,'Evaluation result differs')
                samples.append((time.perf_counter_ns()-start)/1_000_000)
            report['local_roundtrip_ms']={'samples':len(samples),'median':round(statistics.median(samples),3),
                'p95':round(sorted(samples)[int(.95*(len(samples)-1))],3),'scope':'SDK pipe + UI dispatch + trivial renderer JS; not network or website latency'}
        case('Measured local SDK/renderer roundtrip',latency)
        browser.events()
    except Exception as error:
        report['fatal_error']=str(error)
        if isinstance(error,BrowserError):report['fatal_error_details']=error.details
    finally:
        if browser is not None:
            try:
                browser.shutdown()
            except Exception as error:
                report['shutdown_error']=str(error)
        fixture.terminate()
        fixture.wait(timeout=5)
        report['passed']=bool(report['cases']) and all(c['passed'] for c in report['cases']) and not report.get('fatal_error')
        (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({'passed':report['passed'],'cases':len(report['cases']),'report':str(out/'results.json')}),flush=True)
    if not report['passed']:
        raise SystemExit(1)


if __name__=='__main__':
    main()
