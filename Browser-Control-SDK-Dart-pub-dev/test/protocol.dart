import 'dart:convert';
import 'dart:io';
import 'package:browser_control_sdk/browser_control_sdk.dart';

void check(bool value, String message) { if (!value) throw StateError(message); }
Future<void> main(List<String> args) async {
 final browser = await Browser.connect(endpoint: args.single);
 try {
  check(browser.infoJson().contains('café'), 'Unicode or fragmented transport failed');
  check(browser.callJson('fixture.uint64') == '18446744073709551615', 'uint64 response lost');
  final tab = jsonDecode(browser.callJson('tabs.create', parameters: {'url':'about:blank'}));
  check(tab['id'] == 'test-tab', 'Tab creation failed');
  check(browser.callJson('page.navigate', parameters: {'tab':tab['id'], 'url':'https://example.com/é'}).contains('example.com/é'), 'Unicode request failed');
  try { browser.callJson('fixture.error'); throw StateError('Remote error missing'); }
  on BrowserControlError catch (e) { check(e.code == 'test_error' && (e.details as Map)['sample'] == 1, 'Remote error details lost'); }
  for (final method in ['fixture.drop','fixture.bad_id','fixture.hang','fixture.oversize']) {
   try { browser.callJson(method, timeoutMs: 10); throw StateError('Transport error missing'); }
   on BrowserControlError catch (e) { check(e.code == 'result_unknown', 'Uncertain result misclassified'); }
  }
  check(browser.callJson('fixture.count') == '1', 'Unsafe request was retried');
  check(browser.callRaw('fixture.raw_uint64', '{"counter":18446744073709551615}') == 'true', 'Raw uint64 request changed');
  try { browser.callJson('fixture.echo', parameters: {'large':'x' * (1024*1024)}); throw StateError('Oversized request accepted'); }
  on BrowserControlError catch (e) { check(e.code == 'request_too_large', 'Request size error misclassified'); }
  browser.close(); browser.close();
  try { browser.infoJson(); throw StateError('Closed client accepted'); }
  on BrowserControlError catch (e) { check(e.code == 'closed', 'Closed client error lost'); }
 } finally { browser.close(); }
 stdout.writeln('Dart native Browser SDK: protocol, UTF-8, uint64, timeouts, errors, no retries and close passed');
}
