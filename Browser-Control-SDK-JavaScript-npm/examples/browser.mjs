import { Browser } from 'browser-control-sdk';
const browser = await Browser.connect();
const tab = await browser.newTab();
await tab.goto('https://example.org');
console.log((await tab.snapshot()).text);
