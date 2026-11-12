@{
RootModule='BrowserControlSDK.psm1'
ModuleVersion='0.2.1'
GUID='a0b598da-7907-4b92-b28c-dc28856f310e'
Author='TitanCSA'
Description='Standard .NET named-pipe SDK for the local Windows BrowserControl app. No Python or native DLL required.'
PowerShellVersion='7.4'
CompatiblePSEditions=@('Core')
FunctionsToExport=@('Connect-BrowserControl','Invoke-BrowserControl','Get-BrowserControlInfo','Get-BrowserControlTab','New-BrowserControlTab','Set-BrowserControlTabUrl')
CmdletsToExport=@()
AliasesToExport=@()
VariablesToExport=@()
PrivateData=@{ PSData=@{Tags=@('Windows','Browser','SDK');ProjectUri='https://github.com/TitanCSA/Browser-Control-SDK';LicenseUri='https://github.com/TitanCSA/Browser-Control-SDK/blob/main/LICENSE'} }
}
