/** Windows Browser Control transport. Raw protocol responses remain unknown until narrowed. */
export type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };
export type Params = { [key: string]: JsonValue };
export interface TimeoutOptions { timeout_ms?: number }
export type Target =
  | { ref: string; selector?: never; name?: never; x?: never; y?: never; role?: never }
  | { selector: string; ref?: never; name?: never; x?: never; y?: never; role?: never }
  | { name: string; role?: string; ref?: never; selector?: never; x?: never; y?: never }
  | { x: number; y: number; ref?: never; selector?: never; name?: never; role?: never };
export interface ScreenshotOptions { path?: string; [key: string]: unknown }
export interface ScreenshotResult { data: string; [key: string]: unknown }
export interface FrameOptions extends ScreenshotOptions {
  interval_ms?: number;
}
export interface CancellationSignal {
  readonly aborted: boolean;
  addEventListener(type: 'abort', listener: () => void, options?: { once?: boolean }): void;
  removeEventListener(type: 'abort', listener: () => void): void;
}
export interface Action extends Params {
  action: 'click' | 'fill' | 'type' | 'key' | 'mouse' | 'scroll' | 'wait' | 'navigate' | 'observe';
}
export class BrowserError extends Error {
  constructor(code: string, message: string, details?: unknown);
  code: string;
  details: unknown;
}
export class Browser {
  constructor(filename: string, endpoint: { version: number; pipe: string; token: string });
  endpointPath: string;
  process?: unknown;
  static instancesDirectory(): string;
  static connect(endpointFile?: string, options?: TimeoutOptions): Promise<Browser>;
  static launch(options?: TimeoutOptions & { executable?: string; profile_root?: string; endpoint?: string }): Promise<Browser>;
  call(method: string, params?: Params, options?: TimeoutOptions): Promise<unknown>;
  info(): Promise<unknown>;
  capabilities(): Promise<unknown>;
  tabs(): Promise<unknown>;
  tab(id: string): Tab;
  newTab(options?: Params): Promise<Tab>;
  profiles(): Promise<unknown>;
  createProfile(name: string, options?: Params): Promise<unknown>;
  savedLogins(profile?: string, origin?: string): Promise<unknown>;
  deleteSavedLogin(credential_id: string): Promise<unknown>;
  setSavedLoginAutofill(credential_id: string, enabled: boolean): Promise<unknown>;
  window(options?: Params): Promise<unknown>;
  events(options?: Params): Promise<unknown>;
  downloads(options?: Params): Promise<unknown>;
  shutdown(): Promise<unknown>;
}
export class Tab {
  constructor(browser: Browser, id: string);
  browser: Browser;
  id: string;
  call(method: string, params?: Params, options?: TimeoutOptions): Promise<unknown>;
  info(): Promise<unknown>;
  activate(): Promise<unknown>;
  close(): Promise<unknown>;
  goto(url: string, options?: TimeoutOptions & { wait?: string }): Promise<unknown>;
  back(): Promise<unknown>;
  forward(): Promise<unknown>;
  reload(): Promise<unknown>;
  stop(): Promise<unknown>;
  snapshot(): Promise<unknown>;
  observe(options?: Params): Promise<unknown>;
  act(actions: Action[], options?: TimeoutOptions & { observe?: boolean; expected_generation?: number; expected_url?: string }): Promise<unknown>;
  login(operation?: 'status' | 'fill', options?: Params): Promise<unknown>;
  evaluate(expression: string, options?: TimeoutOptions & { await_promise?: boolean; world?: string }): Promise<unknown>;
  wait(options: Target & TimeoutOptions & { state?: string }): Promise<unknown>;
  click(options: Target & { button?: string; click_count?: number }): Promise<unknown>;
  fill(text: string, options: Target): Promise<unknown>;
  type(text: string): Promise<unknown>;
  press(key: string, action?: string): Promise<unknown>;
  mouse(type: string, x: number, y: number, options?: Params): Promise<unknown>;
  scroll(delta_y: number, options?: Params): Promise<unknown>;
  screenshot(options?: ScreenshotOptions): Promise<ScreenshotResult>;
  frames(options?: FrameOptions & { signal?: CancellationSignal }): AsyncGenerator<ScreenshotResult, void, unknown>;
  viewport(width: number, height: number, device_scale_factor?: number): Promise<unknown>;
  settings(settings: Params): Promise<unknown>;
  cookies(operation?: string, options?: Params): Promise<unknown>;
  storage(operation?: string, options?: Params): Promise<unknown>;
  cdp(method: string, params?: Params): Promise<unknown>;
}
