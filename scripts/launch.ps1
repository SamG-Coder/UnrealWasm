param([ValidateSet('Native','WebAssembly','Custom')][string]$Profile='Custom',[int]$Port=8088)
$ErrorActionPreference='Stop'
$sampleRoot=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force (Join-Path $sampleRoot 'artifacts') | Out-Null
if($Profile -eq 'Native'){& "$PSScriptRoot\native.ps1" Run;exit}
$url="http://127.0.0.1:$Port"
$running=$false
try{$running=(Invoke-WebRequest $url -TimeoutSec 2).Content.Contains('<title>Unreal Browser Sample</title>')}catch{}
if(!$running){
 $env:PORT=$Port
 Start-Process -FilePath (Get-Command node).Source -ArgumentList @("`"$PSScriptRoot\serve.mjs`"") -WorkingDirectory $sampleRoot -WindowStyle Hidden -RedirectStandardOutput "$sampleRoot\artifacts\server.log" -RedirectStandardError "$sampleRoot\artifacts\server-error.log" | Out-Null
 for($i=0;$i -lt 40;$i++){try{if((Invoke-WebRequest $url -TimeoutSec 1).StatusCode -eq 200){break}}catch{};Start-Sleep -Milliseconds 250}
}
if($Profile -eq 'Custom'){$browser='D:\ChromiumRTXCuda\src\out\RTXCuda\chrome.exe'}else{$browser='C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'}
if(!(Test-Path -LiteralPath $browser)){throw "Browser missing: $browser"}
# This is the interactive sample the user requested; keep its browser visible.
Start-Process -FilePath $browser -ArgumentList @("--user-data-dir=`"$sampleRoot\artifacts\profiles\interactive-$Profile`"",'--no-first-run','--no-default-browser-check',$url)
Write-Output "$Profile sample: $url"
if($Profile -eq 'Custom'){Write-Output 'Select CUDA or RTX / OptiX and allow native GPU permission.'}
