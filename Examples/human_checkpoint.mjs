import { Browser } from '../SDK/JavaScript/browser-control.mjs';
import readline from 'node:readline';

// Like the Python/C++ versions, this example only observes. It submits nothing.
if (process.argv.length !== 3) throw new Error('Usage: node Examples/human_checkpoint.mjs TAB_ID');
try {
  const browser = await Browser.connect();
  const tab = browser.tab(process.argv[2]);
  console.log(await tab.observe({ max_text: 1200 }));
  const input = readline.createInterface({ input: process.stdin });
  console.log('Finish the authorized manual step in this tab, then type continue:');
  let answer;
  for await (const line of input) { answer = line; break; }
  input.close();
  if (answer?.trim() !== 'continue') throw new Error('No confirmation; nothing was resumed.');
  console.log(await tab.observe({ max_text: 1200 }));
  console.log('Review the fresh state before adding the next authorized action.');
} catch (error) {
  console.error(`${error.message}\nInspect the browser before retrying any earlier write.`);
  process.exitCode = 1;
}
