param([string]$Engine='D:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
& "$PSScriptRoot\prepare-platform-headers.ps1" -Engine $Engine
$overlay=Join-Path $PSScriptRoot 'artifacts\source-engine'
New-Item -ItemType Directory -Force "$overlay\Engine\Build","$overlay\Engine\Binaries" | Out-Null
# UHT reads its own project configuration for delegate suffixes and exporters.
# Keep generated host-tool output local; expose only the installed configuration.
$uhtConfig=Join-Path $overlay 'Engine\Programs\UnrealHeaderTool\Config'
New-Item -ItemType Directory -Force (Split-Path $uhtConfig) | Out-Null
if(!(Test-Path -LiteralPath $uhtConfig)){
 New-Item -ItemType Junction -Path $uhtConfig -Target "$Engine\Engine\Programs\UnrealHeaderTool\Config" | Out-Null
}
# The installed distribution only accepts its precompiled platforms. Use a
# separate source-build root; do not remove InstalledBuild.txt from the real engine.
foreach($folder in @('Source','Config','Content','Plugins','Shaders')){
 $target=Join-Path $Engine "Engine\$folder"
 $destination=Join-Path $overlay "Engine\$folder"
 if(!(Test-Path -LiteralPath $destination)){New-Item -ItemType Junction -Path $destination -Target $target | Out-Null}
}
$platformsDestination=Join-Path $overlay 'Engine\Platforms'
if(Test-Path -LiteralPath $platformsDestination){
 $entry=Get-Item -LiteralPath $platformsDestination
 if($entry.LinkType -eq 'Junction'){
  if($entry.Target -ne (Join-Path $Engine 'Engine\Platforms')){throw 'Unexpected Platforms junction target'}
  Remove-Item -LiteralPath $platformsDestination -Force
 }
}
New-Item -ItemType Directory -Force $platformsDestination | Out-Null
foreach($entry in Get-ChildItem -LiteralPath (Join-Path $Engine 'Engine\Platforms') -Directory){
 if($entry.Name -eq 'WebCuda'){continue}
 $destination=Join-Path $platformsDestination $entry.Name
 if(!(Test-Path -LiteralPath $destination)){New-Item -ItemType Junction -Path $destination -Target $entry.FullName | Out-Null}
}
$webCudaDestination=Join-Path $platformsDestination 'WebCuda'
if(!(Test-Path -LiteralPath $webCudaDestination)){New-Item -ItemType Junction -Path $webCudaDestination -Target "$PSScriptRoot\Platforms\WebCuda" | Out-Null}
foreach($folder in @('ThirdParty','Win64')){
 $destination=Join-Path $overlay "Engine\Binaries\$folder"
 if(!(Test-Path -LiteralPath $destination)){New-Item -ItemType Junction -Path $destination -Target (Join-Path $Engine "Engine\Binaries\$folder") | Out-Null}
}
$dotnetDestination=Join-Path $overlay 'Engine\Binaries\DotNET'
# Migrate the original broad junction to individual host-tool links. The
# custom UBT must be inside this source root, including its child invocations.
if(Test-Path -LiteralPath $dotnetDestination){
 $entry=Get-Item -LiteralPath $dotnetDestination
 if($entry.LinkType -eq 'Junction'){
  if($entry.Target -ne (Join-Path $Engine 'Engine\Binaries\DotNET')){throw 'Unexpected DotNET junction target'}
  Remove-Item -LiteralPath $dotnetDestination -Force
 }
}
New-Item -ItemType Directory -Force $dotnetDestination | Out-Null
foreach($entry in Get-ChildItem -LiteralPath (Join-Path $Engine 'Engine\Binaries\DotNET') -Directory){
 if($entry.Name -eq 'UnrealBuildTool'){continue}
 $destination=Join-Path $dotnetDestination $entry.Name
 if(!(Test-Path -LiteralPath $destination)){New-Item -ItemType Junction -Path $destination -Target $entry.FullName | Out-Null}
}
foreach($file in @('Build.version','BuildSettings.cs')){
 $source=Join-Path $Engine "Engine\Build\$file"
 if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination "$overlay\Engine\Build\$file" -Force}
}
foreach($folder in @('BatchFiles','Windows')){
 $target=Join-Path $Engine "Engine\Build\$folder"
 $destination=Join-Path $overlay "Engine\Build\$folder"
 if((Test-Path -LiteralPath $target) -and !(Test-Path -LiteralPath $destination)){New-Item -ItemType Junction -Path $destination -Target $target | Out-Null}
}
Write-Output "Source-build overlay: $overlay"
