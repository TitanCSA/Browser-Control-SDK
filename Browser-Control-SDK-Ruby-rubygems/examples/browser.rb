require 'browser_control_sdk'
BrowserControlSDK::Browser.connect { |browser| puts JSON.pretty_generate(browser.info); puts JSON.pretty_generate(browser.tabs) }
