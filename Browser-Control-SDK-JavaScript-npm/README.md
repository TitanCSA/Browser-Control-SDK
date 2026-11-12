# Browser Control SDK for Node.js

Control tabs and profiles in your local Windows BrowserControl application.
Requires Node.js 18+ and a separately installed, running BrowserControl application.

```sh
npm install browser-control-sdk
```

Save this example as `example.mjs` and run `node example.mjs`:

```javascript
import { Browser } from 'browser-control-sdk';
const browser = await Browser.connect();
const tab = await browser.newTab();
await tab.goto('https://example.org');
console.log((await tab.snapshot()).text);
await tab.screenshot({ path: 'page.png', full_page: true });
```

`Browser.launch({ executable: 'C:/Tools/BrowserControl.exe' })` can start an instance.
This client controls BrowserControl's own browser; it requires no browser extension.

- [SDK usage and API guide](docs/SDK.md)
- [Runnable example](examples/browser.mjs)
- [Application downloads](https://github.com/TitanCSA/Browser-Control-SDK/releases)

The documentation and example are included in the installed npm package.
License: MIT; see [LICENSE](LICENSE).
