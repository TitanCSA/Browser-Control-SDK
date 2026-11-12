import 'package:browser_control_sdk/browser_control_sdk.dart';

Future<void> main() async {
  final browser = await Browser.connect();
  try {
    print(browser.infoJson());
  } finally {
    browser.close();
  }
}
