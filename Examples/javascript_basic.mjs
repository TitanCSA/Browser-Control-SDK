import { Browser } from '../SDK/JavaScript/browser-control.mjs';

console.log('Open example.org, read its visible page, and save a screenshot. Start BrowserControl.exe first.');
const browser = await Browser.connect();
const tab = await browser.newTab();
await tab.goto('https://example.org');
console.log((await tab.snapshot()).text);
await tab.screenshot({ path: 'page.png' });
