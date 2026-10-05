param([string]$Engine='D:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
& "$PSScriptRoot\prepare-ubt-overrides.ps1" -Engine $Engine
& "$PSScriptRoot\prepare-source-overlay.ps1" -Engine $Engine
$extension=Join-Path $PSScriptRoot 'Platforms\WebCuda'
$installed=Join-Path $Engine 'Engine\Platforms\WebCuda'
# Remove only our initial registration junction: stock UBT cannot parse an
# unknown platform. The extension belongs exclusively to the custom source root.
if(Test-Path -LiteralPath $installed){
 $entry=Get-Item -LiteralPath $installed
 if($entry.LinkType -eq 'Junction' -and $entry.Target -eq $extension){Remove-Item -LiteralPath $installed -Force}
}
$dotnet=Join-Path $Engine 'Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe'
& $dotnet build "$PSScriptRoot\BuildTool\UnrealBuildTool.WebCuda.csproj" "-p:EngineRoot=$Engine" -v:quiet
if($LASTEXITCODE){throw 'WebCuda UBT compilation failed'}
$nativeDependencies=Join-Path $Engine 'Engine\Binaries\DotNET\UnrealBuildTool\runtimes'
$runtimeDestination="$PSScriptRoot\artifacts\source-engine\Engine\Binaries\DotNET\UnrealBuildTool\runtimes"
New-Item -ItemType Directory -Force $runtimeDestination | Out-Null
Copy-Item -Path "$nativeDependencies\*" -Destination $runtimeDestination -Recurse -Force
