# Cook the actual map with original UE shaders using the isolated native host.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$out=Join-Path $PSScriptRoot 'artifacts\shader-format'
New-Item -ItemType Directory -Force $out | Out-Null
$previousSdk=$env:WEBCUDA_EMSDK
$previousPath=$env:PATH
try {
    $env:WEBCUDA_EMSDK=Join-Path $PSScriptRoot 'tools\emsdk'
    $env:PATH=(Join-Path $PSScriptRoot 'artifacts\cook-host\Engine\Binaries\DotNET\UnrealBuildTool\runtimes\win-x64\native')+';'+$env:PATH
    $started=[DateTime]::UtcNow.ToString('o')
    @{status='running';startedUtc=$started;map='/Game/Sample';target='WebCuda';sceneCooked=$false;sceneRendered=$false;workerMode='in-process'} | ConvertTo-Json | Set-Content "$out\cook-result.json"
    & "$PSScriptRoot\artifacts\cook-host\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\UnrealBrowserSample.uproject" -run=Cook -TargetPlatform=WebCuda -Map=/Game/Sample -unattended -nop4 -nosplash -nullrhi -noshaderworker "-abslog=$out\cook.log" *> "$out\cook-console.log"
    $code=$LASTEXITCODE
    @{status=$(if($code -eq 0){'succeeded'}else{'failed'});startedUtc=$started;completedUtc=[DateTime]::UtcNow.ToString('o');map='/Game/Sample';target='WebCuda';exitCode=$code;sceneCooked=($code -eq 0);sceneRendered=$false;workerMode='in-process'} | ConvertTo-Json | Set-Content "$out\cook-result.json"
    Get-Content "$out\cook-console.log" -Tail 25
    exit $code
} finally { $env:WEBCUDA_EMSDK=$previousSdk; $env:PATH=$previousPath }
