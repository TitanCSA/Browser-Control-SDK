#include "BrowserControl.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "Usage: BrowserCheckpointExample TAB_ID\n";
        return 1;
    }
    auto browser = BrowserControlSDK::Browser::Connect();
    auto tab = browser.GetTab(argv[1]);
    std::cout << tab.Observe({{"max_text", 1200}}).dump(2) << '\n';
    std::cout << "Finish the authorized manual step in this tab, then type continue: " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer) || answer != "continue") {
        std::cerr << "No confirmation; nothing was resumed.\n";
        return 1;
    }
    std::cout << tab.Observe({{"max_text", 1200}}).dump(2) << '\n';
    std::cout << "Review the fresh state before adding the next authorized action.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << "\nInspect the browser before retrying any earlier write.\n";
    return 1;
}
