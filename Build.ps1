param([switch]$Test,[string]$BuildDirectory)
$ErrorActionPreference='Stop'
if(![Environment]::Is64BitProcess){throw 'Use Windows PowerShell x64.'}
if(!$BuildDirectory){$BuildDirectory=Join-Path $PSScriptRoot 'obj\Build'}
& cmake -S $PSScriptRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64
if($LASTEXITCODE){throw 'CMake configuration failed.'}
& cmake --build $BuildDirectory --config Release --parallel 3
if($LASTEXITCODE){throw 'C++ compilation failed.'}
if($Test){
    & ctest --test-dir $BuildDirectory -C Release --output-on-failure
    if($LASTEXITCODE){throw 'Native validation failed.'}
    $pythonTests=Join-Path $PSScriptRoot 'Tests\test_sdk_protocol.py'
    if(Test-Path -LiteralPath $pythonTests){
        & python $pythonTests
        if($LASTEXITCODE){throw 'SDK validation failed.'}
    }
}
Write-Output ('Built '+(Join-Path $BuildDirectory 'bin\Release\BrowserControl.exe'))
Write-Output 'No browser was launched, no account was connected, and no Runtime was installed.'
