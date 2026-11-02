"""Check AI actions, explicit human login saving and local session reuse.

Only the supplied localhost fixture and its fictitious credentials are used.
The Save login confirmation is accepted on this test browser's own window.
"""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import json
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'SDK/Python'))
from browser_control import Browser, BrowserError

USER = 'demo@example.invalid'
PASSWORD = 'owned-local-fake-password'


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def until(check, message, seconds=8):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            if check():
                return
        except BrowserError as error:
            # Reinspect read-only page state after an expected sign-in redirect.
            # Mutation calls remain outside this poll and are never replayed.
            if error.code != 'stale_document':
                raise
        time.sleep(.05)
    raise AssertionError(message)


def windows(pid):
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    found = []
    def visit(hwnd, unused):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            name = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(hwnd, name, len(name))
            found.append((hwnd, name.value))
        return True
    user32.EnumWindows(callback_type(visit), 0)
    return found


def save_with_human_confirmation(browser):
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
    user32.GetDlgItem.restype = wintypes.HWND
    pid = browser._process.pid
    hwnd = next(h for h, cls in windows(pid) if cls == 'BrowserControlSdkHost')
    require(user32.PostMessageW(hwnd, 0x0111, 130, 0), 'Could not request owned UI Save login')
    dialogs = []
    def prompt_ready():
        dialogs[:] = [h for h, cls in windows(pid) if cls == '#32770']
        return bool(dialogs)
    until(prompt_ready, 'Owned Save login consent prompt did not open')
    yes = user32.GetDlgItem(dialogs[0], 6)
    require(bool(yes), 'Save login prompt does not have explicit Yes confirmation')
    require(user32.PostMessageW(yes, 0x00F5, 0, 0), 'Could not confirm owned fixture login save')
    until(lambda: not any(cls == '#32770' for h, cls in windows(pid)), 'Save login prompt did not close')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--output')
    args = parser.parse_args()
    out = Path(args.output) if args.output else ROOT/'obj/AuthValidation'/uuid.uuid4().hex
    out.mkdir(parents=True, exist_ok=True)
    ready = out/'fixture.json'
    ready.unlink(missing_ok=True)
    fixture = subprocess.Popen([sys.executable, str(ROOT/'Tests/fixture_server.py'), '--ready-file', str(ready)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    browser = None
    report = {'scope':'Owned localhost fixture, fictitious account and own UI consent only', 'cases':[]}
    def case(name, function):
        start = time.perf_counter()
        function()
        report['cases'].append({'name':name, 'passed':True, 'ms':round((time.perf_counter()-start)*1000, 2)})
        print('PASS '+name, flush=True)
    try:
        until(ready.exists, 'Local fixture did not start')
        base = json.loads(ready.read_text())['url']
        data_root = Browser.instances_directory().parent/'AuthValidation'/uuid.uuid4().hex/'Profiles'
        browser = Browser.launch(executable=args.exe, profile_root=data_root)
        tab = browser.new_tab(base)
        def batch():
            result = tab.act([{'action':'fill', 'name':'Your name', 'role':'textbox', 'text':'AI batch'},
                              {'action':'click', 'name':'Save example', 'role':'button'}])
            require(result['completed'] == 2 and result['status'] == 'completed', 'Batch receipt differs')
            require('AI batch' in tab.evaluate('document.querySelector("#result").textContent'), 'Semantic batch did not update the real page')
            require('observation' in result, 'Batch did not return its resulting observation')
            compact = browser.call('page.observe', {'max_elements':2, 'max_text':80})
            require(len(compact['elements']) <= 2 and len(compact.get('text','')) <= 80, 'Observation limits were ignored')
        case('Compact active-tab observation and one-request semantic action batch', batch)
        def stopped_batch():
            tab.evaluate('window.batchClicks=0;document.querySelector("#save").addEventListener("click",()=>window.batchClicks++)')
            result = tab.act([{'action':'click', 'selector':'#save'}, {'action':'click', 'selector':'#missing'},
                              {'action':'click', 'selector':'#save'}])
            require(result['status'] == 'failed' and result['completed'] == 1, 'Failure did not preserve the completed prefix')
            require(tab.evaluate('window.batchClicks') == 1, 'Failed batch replayed or continued mutations')
            try:
                tab.act([{'action':'click', 'selector':'#save'}], expected_generation=tab.info()['generation']+1)
            except BrowserError:
                pass
            else:
                raise AssertionError('Stale generation guard accepted')
            require(tab.evaluate('window.batchClicks') == 1, 'Generation guard mutated the page')
        case('Failed batches stop, preserve results and reject stale guards', stopped_batch)
        def timed_batch():
            try:
                tab.act([{'action':'click', 'selector':'#save'}, {'action':'wait', 'selector':'#never-created'},
                         {'action':'click', 'selector':'#save'}], timeout_ms=150)
            except BrowserError as error:
                require(error.details.get('completed') == 1, 'Timeout lost the completed prefix')
            else:
                raise AssertionError('Never-created target did not time out')
            require(tab.evaluate('window.batchClicks') == 2, 'Timed-out batch continued or replayed a mutation')
        case('Batch deadline keeps the partial receipt without replay', timed_batch)
        tab.goto(base+'/login')
        tab.fill(USER, selector='#login-user')
        tab.fill(PASSWORD, selector='#login-password')
        def save():
            save_with_human_confirmation(browser)
            until(lambda: len(browser.saved_logins()) == 1, 'Confirmed fixture login was not saved')
            records = browser.saved_logins()
            require(PASSWORD not in json.dumps(records), 'Metadata exposed a password')
            # The filename is implementation-defined; scan only this private test root.
            files = list(data_root.glob('*.dpapi'))
            require(bool(files), 'Encrypted vault file missing')
            require(all(PASSWORD.encode() not in f.read_bytes() and USER.encode() not in f.read_bytes() for f in files), 'Credential storage contains plaintext')
        case('Human-confirmed save creates encrypted metadata-only credential storage', save)
        credential_id = browser.saved_logins()[0]['id']
        def fallback():
            tab.goto(base+'/login')
            status = tab.login()
            require(PASSWORD not in json.dumps(status), 'Login status exposed a password')
            result = tab.login('fill', credential_id=credential_id)
            require(result.get('filled') is True and not result.get('submitted'), 'Fill unexpectedly submitted or failed')
            require(tab.evaluate('document.querySelector("#login-user").value') == USER, 'Username fallback differs')
            require(tab.evaluate('document.querySelector("#login-password").value') == PASSWORD, 'Password fallback differs')
            require(PASSWORD not in json.dumps(tab.observe()), 'Observation exposed the filled password')
        case('Saved login fills the matching origin without returning or submitting the password', fallback)
        def isolation():
            other = browser.new_tab(base.replace('127.0.0.1','localhost')+'/login')
            try:
                try:
                    other.login('fill', credential_id=credential_id)
                except BrowserError:
                    pass
                else:
                    raise AssertionError('Credential filled a different origin')
                require(other.evaluate('document.querySelector("#login-password").value') == '', 'Foreign-origin password field was modified')
            finally:
                other.close()
            other = browser.new_tab(base+'/login', profile='Other')
            try:
                try:
                    other.login('fill', credential_id=credential_id)
                except BrowserError:
                    pass
                else:
                    raise AssertionError('Credential crossed browser profiles')
            finally:
                other.close()
            tab.activate()
        case('Saved login cannot cross an origin or profile boundary', isolation)
        def automatic():
            browser.set_saved_login_autofill(credential_id, True)
            tab.goto(base+'/login')
            until(lambda: tab.evaluate('document.querySelector("#login-password").value') == PASSWORD, 'Opt-in automatic filling did not run')
            require(tab.evaluate('document.title') == 'Local sign-in fixture', 'Autofill unexpectedly submitted')
            browser.set_saved_login_autofill(credential_id, False)
        case('Optional exact-origin autofill runs without automatic submission', automatic)
        def ambiguous():
            tab.goto(base+'/login?ambiguous=1')
            try:
                tab.login('fill', credential_id=credential_id)
            except BrowserError:
                pass
            else:
                raise AssertionError('Ambiguous login forms were filled')
            require(tab.evaluate('Array.from(document.querySelectorAll("input[type=password]")).every(e=>e.value==="")'), 'Ambiguous form contains filled password')
        case('Ambiguous forms are refused without filling another account', ambiguous)
        tab.goto(base+'/login')
        def submit():
            result = tab.login('fill', credential_id=credential_id, submit=True)
            require(result.get('submitted'), 'Explicit submission was not requested')
            until(lambda: tab.evaluate('document.title') == 'Local account signed in', 'Fictitious form did not sign in')
            require(any(c['name']=='browser_fixture_login' and c['http_only'] for c in tab.cookies()), 'Persistent HttpOnly fixture cookie missing')
        case('Explicit SDK sign-in submits once and establishes a local cookie session', submit)
        browser.shutdown()
        browser._process.wait(timeout=10)
        browser = Browser.launch(executable=args.exe, profile_root=data_root)
        tab = browser.new_tab(base+'/login')
        def persisted():
            require(tab.evaluate('document.title') == 'Local account signed in', 'Persistent cookie session was lost after browser restart')
            require(browser.saved_logins()[0]['id'] == credential_id, 'Encrypted fallback login was lost after restart')
            tab.cookies('clear', url=base)
            tab.goto(base+'/login')
            result = tab.login('fill', credential_id=credential_id)
            require(result.get('filled') is True, 'Saved fallback failed after session expiry/clearing')
        case('Restart reuses a persistent session and saved login remains available after clearing it', persisted)
        def deletion():
            browser.delete_saved_login(credential_id)
            require(browser.saved_logins() == [], 'Deleted login metadata remains available')
            tab.goto(base+'/login')
            try:
                tab.login('fill', credential_id=credential_id)
            except BrowserError:
                return
            raise AssertionError('Deleted login still fills a password')
        case('Forget login removes its future autofill capability', deletion)
        report['passed'] = True
    except Exception as error:
        report['passed'] = False
        report['error'] = str(error)
        if isinstance(error, BrowserError):
            report['details'] = error.details
        print('FAIL '+str(error), flush=True)
    finally:
        if browser:
            try:
                browser.shutdown()
                browser._process.wait(timeout=10)
            except Exception:
                pass
        fixture.terminate()
        fixture.wait(timeout=5)
        (out/'results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({'passed':report.get('passed',False), 'cases':len(report['cases']), 'report':str(out/'results.json')}), flush=True)
    if not report.get('passed'):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
