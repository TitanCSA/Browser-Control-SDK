# Browser Control SDK for Ruby

`gem install browser-control-sdk`

Requires Ruby 3.1+ on Windows x64 and the running BrowserControl application. The included native bridge communicates directly with its private named pipe. No Python or driver installation is required.

```ruby
require 'browser_control_sdk'
BrowserControlSDK::Browser.connect do |browser|
  p browser.info
  tab = browser.new_tab
  browser.navigate(tab.fetch('id'), 'https://example.com')
end
```

Use `connect(endpoint: 'C:/path/to/private/endpoint.json')` to select one running instance. Without a path the native SDK discovers a responding instance for the current user. The endpoint contains a private authentication token; keep it private.

Every documented protocol operation is available through `browser.call(method, params, timeout_ms: 15000)`. See [API documentation](docs/API.md) and [example](examples/browser.rb). Call `close` if you do not use the block form.
