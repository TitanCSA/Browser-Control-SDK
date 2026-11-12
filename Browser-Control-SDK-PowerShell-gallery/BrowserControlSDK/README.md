# Browser Control SDK for PowerShell

Requires Windows and PowerShell 7.4+. Install and start BrowserControl.exe separately.
Uses .NET named pipes directly, with no Python, browser extension or native DLL.

```powershell
Install-Module BrowserControlSDK -Scope CurrentUser
Import-Module BrowserControlSDK
$browser = Connect-BrowserControl
$tab = New-BrowserControlTab -Client $browser
Set-BrowserControlTabUrl -Client $browser -Tab $tab.id -Url https://example.org
Invoke-BrowserControl -Client $browser -Method page.snapshot -Parameters @{tab=$tab.id}
```

The Install-Module command is available after Gallery publication. For local use,
import the included BrowserControlSDK.psd1. Use `-EndpointPath` to select one
instance explicitly. Otherwise, the newest responding private discovery endpoint
under LOCALAPPDATA/TitanCSA/BrowserControl/instances is selected.

`Invoke-BrowserControl` exposes all methods in the application's protocol, including
profiles, snapshots, screenshots, actions, downloads and window control. Parameters
and replies are PowerShell hashtables/arrays. Run Get-Help on any exported command.

Every call has one transport deadline and bounded request/response frames. If the
reply is lost after a request may have executed, the error code is `result_unknown`:
inspect browser state before explicitly retrying. The SDK does not automatically
repeat actions. Private discovery tokens stay inside the client object.

The client does not own the browser process. Disconnecting or leaving the script
does not close the application. An explicit `browser.shutdown` request closes it.

[Protocol reference](https://github.com/TitanCSA/Browser-Control-SDK/blob/main/Protocol/API.md)
and a [general SDK guide](docs/SDK.md) are provided. License: MIT, bundled in LICENSE.
