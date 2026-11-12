Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'BrowserControl requires Windows.' }
Add-Type -Path (Join-Path $PSScriptRoot 'lib/BrowserControl.PowerShell.dll')
function Connect-BrowserControl {
<#
.SYNOPSIS
Connects to an explicitly selected or newest responding local BrowserControl instance.
#>
    [CmdletBinding()] param([string]$EndpointPath, [ValidateRange(1,120000)][int]$TimeoutMs=1500)
    $path = if ($EndpointPath) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($EndpointPath) } else { $null }
    [TitanCSA.BrowserControl.PowerShell.BrowserClient]::Connect($path,$TimeoutMs)
}
function Invoke-BrowserControl {
<#
.SYNOPSIS
Invokes any documented BrowserControl protocol method with JSON-compatible parameters.
#>
    [CmdletBinding()] param([Parameter(Mandatory)]$Client, [Parameter(Mandatory)][string]$Method,
        [System.Collections.IDictionary]$Parameters=@{}, [ValidateRange(1,120000)][int]$TimeoutMs=15000)
    $json = ConvertTo-Json -InputObject $Parameters -Depth 64 -Compress
    $reply = $Client.Call($Method,$json,$TimeoutMs)
    ConvertFrom-Json -InputObject $reply -AsHashtable -Depth 128
}
function Get-BrowserControlInfo {
<#
.SYNOPSIS
Gets the selected BrowserControl application's status.
#>
    [CmdletBinding()] param([Parameter(Mandatory)]$Client)
    Invoke-BrowserControl -Client $Client -Method browser.info
}
function Get-BrowserControlTab {
<#
.SYNOPSIS
Lists tabs in the selected BrowserControl application.
#>
    [CmdletBinding()] param([Parameter(Mandatory)]$Client)
    Invoke-BrowserControl -Client $Client -Method tabs.list
}
function New-BrowserControlTab {
<#
.SYNOPSIS
Creates a browser tab using the supplied protocol options.
#>
    [CmdletBinding()] param([Parameter(Mandatory)]$Client, [System.Collections.IDictionary]$Options=@{})
    Invoke-BrowserControl -Client $Client -Method tabs.create -Parameters $Options
}
function Set-BrowserControlTabUrl {
<#
.SYNOPSIS
Navigates a selected tab and waits for the requested loading state.
#>
    [CmdletBinding()] param([Parameter(Mandatory)]$Client, [Parameter(Mandatory)][string]$Tab,
        [Parameter(Mandatory)][string]$Url, [string]$Wait='load', [ValidateRange(1,120000)][int]$TimeoutMs=15000)
    Invoke-BrowserControl -Client $Client -Method page.navigate -Parameters @{tab=$Tab;url=$Url;wait=$Wait;timeout_ms=$TimeoutMs} -TimeoutMs $TimeoutMs
}
