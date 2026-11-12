Import-Module BrowserControlSDK
$browser=Connect-BrowserControl
$tab=New-BrowserControlTab -Client $browser
Set-BrowserControlTabUrl -Client $browser -Tab $tab.id -Url https://example.org
