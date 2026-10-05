param([Parameter(ValueFromRemainingArguments=$true)][string[]]$BuildArguments)
$ErrorActionPreference='Stop'
$installedEngine='D:\UnrealEngine\UE_5.8'
$engineRoot=Join-Path $PSScriptRoot 'artifacts\source-engine'
if(!(Test-Path -LiteralPath "$engineRoot\Engine\Build\Build.version")){throw 'Run engine/prepare-source-overlay.ps1 first'}
$env:WEBCUDA_EMSDK=Join-Path $PSScriptRoot 'tools\emsdk'
$env:WEBCUDA_OODLE_LIBRARY=Join-Path $PSScriptRoot 'artifacts\oodle\liboo2corewasm64.a'
$env:WEBCUDA_ICU_ROOT=Join-Path $PSScriptRoot 'artifacts\icu64'
$env:WEBCUDA_SLATE_ROOT=Join-Path $PSScriptRoot 'artifacts\slate-dependencies'
$env:WEBCUDA_GENERATED_HEADERS=Join-Path $PSScriptRoot 'artifacts\platform-headers'
$buildTool="$engineRoot\Engine\Binaries\DotNET\UnrealBuildTool"
$env:PATH="$buildTool\runtimes\win-x64\native;"+$env:PATH
& "$installedEngine\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe" "$buildTool\UnrealBuildTool.dll" "-rootdirectory=$engineRoot" @BuildArguments
exit $LASTEXITCODE
