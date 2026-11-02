#include "BrowserControl.h"
#include <iostream>

int main() try {
    std::cout << "Open example.org and read its visible page. Start BrowserControl.exe first.\n";
    auto browser = BrowserControlSDK::Browser::Connect();
    auto tab = browser.NewTab();
    tab.Goto("https://example.org");
    std::cout << tab.Snapshot().at("text").get<std::string>() << '\n';
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
