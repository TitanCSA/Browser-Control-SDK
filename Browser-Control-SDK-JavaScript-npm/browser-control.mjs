/** Standard-library Node.js client for the owned Windows BrowserControl instance. */
import net from 'node:net';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';

const MAX_REQUEST = 1024 * 1024, MAX_RESPONSE = 64 * 1024 * 1024;
const PIPE_PREFIX = '\\\\.\\pipe\\BrowserControl-';
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

export class BrowserError extends Error {
  constructor(code, message, details = {}) {
    super(`${code}: ${message}`);
    this.name = 'BrowserError'; this.code = code; this.details = details;
  }
}

function timeout(value) {
  if (!Number.isInteger(value) || value < 1 || value > 120000)
    throw new RangeError('timeout_ms must be an integer between 1 and 120000');
  return value;
}

async function loadEndpoint(filename) {
  try {
    const stat = await fs.stat(filename);
    if (stat.size > 16384) throw new Error();
    const endpoint = JSON.parse((await fs.readFile(filename, 'utf8')).replace(/^\uFEFF/, ''));
    if (endpoint.version !== 1 || typeof endpoint.pipe !== 'string' ||
        !endpoint.pipe.startsWith(PIPE_PREFIX) || endpoint.pipe.length > 240 ||
        typeof endpoint.token !== 'string' || endpoint.token.length < 16 || endpoint.token.length > 1024)
      throw new Error();
    return endpoint;
  } catch {
    throw new BrowserError('invalid_endpoint', 'The private browser endpoint file is missing or invalid');
  }
}

function exchange(endpoint, request, timeoutMs) {
  return new Promise((resolve, reject) => {
    let payload;
    try { payload = Buffer.from(JSON.stringify(request, (_key, value) => {
      if (typeof value === 'number' && !Number.isFinite(value)) throw new TypeError();
      return value;
    }), 'utf8'); }
    catch { reject(new TypeError('Browser parameters must be JSON values')); return; }
    if (payload.length > MAX_REQUEST) {
      reject(new BrowserError('request_too_large', 'The browser request exceeds 1 MiB')); return;
    }
    const frame = Buffer.allocUnsafe(4 + payload.length);
    frame.writeUInt32LE(payload.length, 0); payload.copy(frame, 4);
    const socket = net.createConnection(endpoint.pipe);
    let settled = false, wrote = false, headerUsed = 0, bodyUsed = 0, body;
    const header = Buffer.alloc(4);
    const fail = (code, message) => {
      if (settled) return;
      settled = true; clearTimeout(timer); socket.destroy();
      reject(wrote ? new BrowserError('result_unknown',
        'No confirmed browser reply; inspect state before retrying the action',
        { may_have_executed: true, cause_code: code, method: request.method }) : new BrowserError(code, message));
    };
    const timer = setTimeout(() => fail('transport_timeout', 'The browser transport deadline expired'), timeoutMs + 1000);
    socket.on('connect', () => { wrote = true; socket.write(frame); });
    socket.on('error', error => fail('connection_failed', `Could not communicate with the browser (${error.code ?? 'IO'})`));
    socket.on('end', () => fail('connection_closed', 'The browser closed its pipe before the response completed'));
    socket.on('data', chunk => {
      if (settled) return;
      let offset = 0;
      if (headerUsed < 4) {
        const take = Math.min(4 - headerUsed, chunk.length);
        chunk.copy(header, headerUsed, 0, take); headerUsed += take; offset += take;
        if (headerUsed < 4) return;
        const length = header.readUInt32LE(0);
        if (length < 1 || length > MAX_RESPONSE) { fail('invalid_response', 'Browser response length is outside the supported bound'); return; }
        body = Buffer.allocUnsafe(length);
      }
      const available = chunk.length - offset;
      if (available > body.length - bodyUsed) { fail('invalid_response', 'Unexpected bytes follow the browser response'); return; }
      chunk.copy(body, bodyUsed, offset); bodyUsed += available;
      if (bodyUsed !== body.length) return;
      let response;
      try {
        response = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(body));
        if (!response || typeof response !== 'object' || Array.isArray(response) || response.id !== request.id)
          throw new Error();
        if (response.ok === true && Object.hasOwn(response, 'result')) {
          settled = true; clearTimeout(timer); socket.destroy(); resolve(response.result); return;
        }
        if (response.ok === false && typeof response.error?.code === 'string' && typeof response.error.message === 'string') {
          settled = true; clearTimeout(timer); socket.destroy();
          reject(new BrowserError(response.error.code, response.error.message, response.error.details)); return;
        }
        throw new Error();
      } catch { fail('invalid_response', 'The browser returned an invalid response'); }
    });
  });
}

export class Browser {
  #endpoint;
  constructor(filename, endpoint) { this.endpointPath = filename; this.#endpoint = endpoint; }

  static instancesDirectory() {
    if (!process.env.LOCALAPPDATA) throw new BrowserError('unsupported_platform', 'LOCALAPPDATA is unavailable; supply an endpoint file');
    return path.join(process.env.LOCALAPPDATA, 'TitanCSA', 'BrowserControl', 'instances');
  }

  static async connect(endpointFile, { timeout_ms = 1500 } = {}) {
    timeout(timeout_ms);
    if (endpointFile) {
      const browser = new Browser(path.resolve(endpointFile), await loadEndpoint(endpointFile));
      await browser.call('browser.info', {}, { timeout_ms }); return browser;
    }
    let candidates = [];
    try {
      const folder = Browser.instancesDirectory();
      for (const name of await fs.readdir(folder)) if (name.endsWith('.json')) {
        const filename = path.join(folder, name);
        candidates.push({ filename, mtime: (await fs.stat(filename)).mtimeMs });
      }
    } catch { /* No active instance may have been created yet. */ }
    candidates.sort((a, b) => b.mtime - a.mtime);
    const deadline = Date.now() + timeout_ms;
    for (const candidate of candidates.slice(0, 128)) {
      const remaining = deadline - Date.now();
      if (remaining <= 0) break;
      try { return await Browser.connect(candidate.filename, { timeout_ms: Math.min(remaining, 500) }); }
      catch (error) { if (!(error instanceof BrowserError)) throw error; }
    }
    throw new BrowserError('browser_not_running', 'Start BrowserControl.exe or call Browser.launch({ executable: ... })');
  }

  static async launch({ executable, profile_root, endpoint, timeout_ms = 30000 } = {}) {
    timeout(timeout_ms);
    if (process.platform !== 'win32') throw new BrowserError('unsupported_platform', 'BrowserControl.exe requires Windows');
    if (!executable) {
      const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
      for (const candidate of [path.resolve('BrowserControl.exe'), path.join(root, 'BrowserControl.exe'),
        path.join(root, 'obj/Build/bin/Release/BrowserControl.exe'), path.join(root, 'bin/Release/BrowserControl.exe')]) {
        try { if ((await fs.stat(candidate)).isFile()) { executable = candidate; break; } } catch {}
      }
    }
    if (!executable) throw new BrowserError('executable_missing', 'Supply the path to BrowserControl.exe');
    const filename = path.resolve(endpoint ?? path.join(Browser.instancesDirectory(), `sdk-${randomUUID()}.json`));
    try { await fs.access(filename); throw new BrowserError('endpoint_exists', 'A launched instance needs an unused endpoint path'); }
    catch (error) { if (error instanceof BrowserError) throw error; if (error.code !== 'ENOENT') throw error; }
    await fs.mkdir(path.dirname(filename), { recursive: true });
    const args = ['--endpoint', filename];
    if (profile_root) args.push('--profile-root', path.resolve(profile_root));
    const child = spawn(path.resolve(executable), args, { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
    let launchError;
    let stderrTail = Buffer.alloc(0);
    child.stderr.on('data', chunk => { stderrTail = Buffer.concat([stderrTail, chunk]).subarray(-8192); });
    child.once('error', error => { launchError = error; });
    const end = Date.now() + timeout_ms;
    while (Date.now() < end) {
      if (launchError || child.exitCode !== null) {
        const detail = stderrTail.toString('utf8').trim();
        throw new BrowserError('launch_failed', 'BrowserControl.exe exited during startup' + (detail ? ': ' + detail : ''),
          { exit_code: child.exitCode });
      }
      try {
        const browser = await Browser.connect(filename, { timeout_ms: Math.min(1000, Math.max(1, end - Date.now())) });
        const info = await browser.info();
        if (info?.ready !== false) { browser.process = child; child.unref(); child.stderr.unref?.(); return browser; }
      } catch (error) { if (!(error instanceof BrowserError)) throw error; }
      await pause(50);
    }
    throw new BrowserError('launch_timeout', 'The browser did not become ready; its window may still be open');
  }

  async call(method, params = {}, { timeout_ms = 15000 } = {}) {
    timeout(timeout_ms);
    if (process.platform !== 'win32') throw new BrowserError('unsupported_platform', 'The named-pipe browser client requires Windows');
    if (typeof method !== 'string' || !method || method.length > 128 || !params || Array.isArray(params) || typeof params !== 'object')
      throw new TypeError('Supply a method string and a params object');
    return exchange(this.#endpoint, { version: 1, id: randomUUID(), token: this.#endpoint.token,
      method, params, timeout_ms }, timeout_ms);
  }

  info() { return this.call('browser.info'); }
  capabilities() { return this.call('browser.capabilities'); }
  tabs() { return this.call('tabs.list'); }
  tab(id) { return new Tab(this, id); }
  async newTab(options = {}) { const value = await this.call('tabs.create', options); return new Tab(this, value.id); }
  profiles() { return this.call('profiles.list'); }
  createProfile(name, options = {}) { return this.call('profiles.create', { ...options, name }); }
  savedLogins(profile, origin) {
    const options = {};
    if (profile !== undefined) options.profile = profile;
    if (origin !== undefined) options.origin = origin;
    return this.call('saved_logins.list', options);
  }
  deleteSavedLogin(credential_id) { return this.call('saved_logins.delete', { credential_id }); }
  setSavedLoginAutofill(credential_id, enabled) {
    if (typeof enabled !== 'boolean') throw new TypeError('enabled must be a boolean');
    return this.call('saved_logins.autofill', { credential_id, enabled });
  }
  window(options) { return this.call(options ? 'window.set' : 'window.info', options ?? {}); }
  events(options = {}) { return this.call('events.read', options); }
  downloads(options) { return this.call(options ? 'downloads.configure' : 'downloads.list', options ?? {}); }
  shutdown() { return this.call('browser.shutdown'); }
}

const ACTION_NAMES = new Set(['click', 'fill', 'type', 'key', 'mouse', 'scroll', 'wait', 'navigate', 'observe']);
function actionsList(actions) {
  if (!Array.isArray(actions) || actions.length < 1 || actions.length > 20 ||
      actions.some(action => !action || Array.isArray(action) || typeof action !== 'object' || !ACTION_NAMES.has(action.action)))
    throw new TypeError('actions must contain between 1 and 20 objects with supported action names');
}
function target({ ref, selector, x, y, name, role }) {
  if (Number(ref !== undefined) + Number(selector !== undefined) + Number(x !== undefined || y !== undefined) + Number(name !== undefined) !== 1 ||
      (x === undefined) !== (y === undefined) || (role !== undefined && name === undefined))
    throw new TypeError('Specify one target: ref, selector, name with optional role, or both x and y');
  if (name !== undefined) {
    if (typeof name !== 'string' || !name || (role !== undefined && (typeof role !== 'string' || !role)))
      throw new TypeError('A semantic target needs a nonempty name and, when supplied, role');
    return role === undefined ? { name } : { name, role };
  }
  return ref !== undefined ? { ref } : selector !== undefined ? { selector } : { x, y };
}

export class Tab {
  constructor(browser, id) {
    if (typeof id !== 'string' || !id) throw new TypeError('tab id must be a nonempty string');
    this.browser = browser; this.id = id;
  }
  call(method, params = {}, options = {}) { return this.browser.call(method, { ...params, tab: this.id }, options); }
  info() { return this.call('page.info'); }
  activate() { return this.call('tabs.activate'); }
  close() { return this.call('tabs.close'); }
  goto(url, { wait = 'load', timeout_ms = 15000 } = {}) {
    return this.call('page.navigate', { url, wait, timeout_ms }, { timeout_ms });
  }
  back() { return this.call('page.back'); }
  forward() { return this.call('page.forward'); }
  reload() { return this.call('page.reload'); }
  stop() { return this.call('page.stop'); }
  snapshot() { return this.call('page.snapshot'); }
  observe(options = {}) { return this.call('page.observe', options); }
  act(actions, { observe = true, timeout_ms = 15000, expected_generation, expected_url } = {}) {
    actionsList(actions);
    if (typeof observe !== 'boolean') throw new TypeError('observe must be a boolean');
    const guards = {};
    if (expected_generation !== undefined) {
      if (!Number.isSafeInteger(expected_generation) || expected_generation < 0)
        throw new TypeError('expected_generation must be a nonnegative safe integer');
      guards.expected_generation = expected_generation;
    }
    if (expected_url !== undefined) {
      if (typeof expected_url !== 'string') throw new TypeError('expected_url must be a string');
      guards.expected_url = expected_url;
    }
    return this.call('page.act', { actions, observe, ...guards }, { timeout_ms });
  }
  login(operation = 'status', options = {}) {
    if (!['status', 'fill'].includes(operation)) throw new TypeError('operation must be status or fill');
    return this.call('page.login', { ...options, operation });
  }
  async evaluate(expression, { await_promise = true, world = 'main', timeout_ms = 15000 } = {}) {
    const value = await this.call('page.evaluate', { expression, await_promise, world }, { timeout_ms });
    return value && typeof value === 'object' && ('value' in value || value.undefined) ? value.value : value;
  }
  wait(options) { const { timeout_ms = 15000, state = 'visible' } = options; return this.call('page.wait', { ...target(options), state, timeout_ms }, { timeout_ms }); }
  click(options) { return this.call('page.click', { ...target(options), button: options.button ?? 'left', click_count: options.click_count ?? 1 }); }
  fill(text, options) { return this.call('page.fill', { ...target(options), text }); }
  type(text) { return this.call('page.type', { text }); }
  press(key, action = 'press') { return this.call('page.key', { key, action }); }
  mouse(type, x, y, options = {}) { return this.call('page.mouse', { ...options, type, x, y }); }
  scroll(delta_y, options = {}) { return this.call('page.scroll', { ...options, delta_y }); }
  async screenshot({ path: filename, ...options } = {}) {
    const result = await this.call('page.screenshot', options);
    if (filename) await fs.writeFile(filename, Buffer.from(result.data, 'base64'));
    return result;
  }
  async *frames({ interval_ms = 100, signal, ...options } = {}) {
    if (typeof interval_ms !== 'number' || !Number.isFinite(interval_ms) || interval_ms < 0 || interval_ms > 120000)
      throw new RangeError('interval_ms must be a finite number between 0 and 120000');
    if (signal && (typeof signal.aborted !== 'boolean' || typeof signal.addEventListener !== 'function'))
      throw new TypeError('signal must be an AbortSignal');
    while (!signal?.aborted) {
      const started = performance.now();
      const frame = await this.screenshot(options);
      if (signal?.aborted) return;
      yield frame;
      const remaining = Math.max(0, interval_ms - (performance.now() - started));
      if (remaining > 0) await new Promise(resolve => {
        const complete = () => { clearTimeout(timer); signal?.removeEventListener('abort', complete); resolve(); };
        const timer = setTimeout(complete, remaining);
        signal?.addEventListener('abort', complete, { once: true });
        if (signal?.aborted) complete();
      });
    }
  }
  viewport(width, height, device_scale_factor = 1) { return this.call('page.viewport', { width, height, device_scale_factor }); }
  settings(settings) { return this.call('page.settings', { settings }); }
  cookies(operation = 'list', options = {}) { return this.call('page.cookies', { ...options, operation }); }
  storage(operation = 'get', options = {}) { return this.call('page.storage', { ...options, operation }); }
  cdp(method, params = {}) { return this.call('page.cdp', { method, params }); }
}
