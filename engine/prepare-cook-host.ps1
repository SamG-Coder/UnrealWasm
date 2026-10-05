param([string]$Engine='D:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
& "$PSScriptRoot\prepare-ubt-overrides.ps1" -Engine $Engine
$hostRoot=Join-Path $PSScriptRoot 'artifacts\cook-host'
$engineDestination=Join-Path $hostRoot 'Engine'
New-Item -ItemType Directory -Force $engineDestination | Out-Null
function Link-Directory([string]$Destination,[string]$Source){
 if(!(Test-Path -LiteralPath $Destination)){
  New-Item -ItemType Directory -Force (Split-Path $Destination) | Out-Null
  New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null
 }
}
foreach($name in @('Source','Config','Content','Shaders','Documentation','Extras','FeaturePacks')){
 $source=Join-Path $Engine "Engine\$name"
 if(Test-Path -LiteralPath $source){Link-Directory "$engineDestination\$name" $source}
}
$pluginDestination=Join-Path $engineDestination 'Plugins'
if(Test-Path -LiteralPath $pluginDestination){
 $entry=Get-Item -LiteralPath $pluginDestination
 if($entry.LinkType -eq 'Junction'){
  if($entry.Target -ne "$Engine\Engine\Plugins"){throw 'Unexpected cook-host Plugins junction'}
  # Remove only the junction itself. Plugin intermediates must be local.
  Remove-Item -LiteralPath $pluginDestination -Force
 }
}
New-Item -ItemType Directory -Force $pluginDestination | Out-Null
$pluginSource=Join-Path $Engine 'Engine\Plugins'
$descriptors=@(& rg --files -g '*.uplugin' $pluginSource)
if($LASTEXITCODE -ne 0){throw 'Failed to enumerate native engine plugins'}
foreach($descriptor in $descriptors){
 $source=Split-Path $descriptor
 $relative=[System.IO.Path]::GetRelativePath($pluginSource,$source)
 $destination=Join-Path $pluginDestination $relative
 New-Item -ItemType Directory -Force $destination | Out-Null
 foreach($entry in Get-ChildItem -LiteralPath $source){
  if($entry.Name -eq 'Intermediate'){continue}
  $target=Join-Path $destination $entry.Name
  if($entry.PSIsContainer){Link-Directory $target $entry.FullName}
  elseif(!(Test-Path -LiteralPath $target)){Copy-Item -LiteralPath $entry.FullName -Destination $target}
 }
}
New-Item -ItemType Directory -Force "$engineDestination\Build","$engineDestination\Platforms","$engineDestination\Binaries\Win64","$engineDestination\Binaries\DotNET" | Out-Null
foreach($entry in Get-ChildItem -LiteralPath "$Engine\Engine\Build"){
 if($entry.PSIsContainer){Link-Directory "$engineDestination\Build\$($entry.Name)" $entry.FullName}
 elseif($entry.Name -ne 'InstalledBuild.txt'){Copy-Item -LiteralPath $entry.FullName -Destination "$engineDestination\Build\$($entry.Name)" -Force}
}
foreach($entry in Get-ChildItem -LiteralPath "$Engine\Engine\Platforms" -Directory){
 if($entry.Name -ne 'WebCuda'){Link-Directory "$engineDestination\Platforms\$($entry.Name)" $entry.FullName}
}
$hostPlatform="$engineDestination\Platforms\WebCuda"
if(Test-Path -LiteralPath $hostPlatform){
 $entry=Get-Item -LiteralPath $hostPlatform
 if($entry.LinkType -eq 'Junction'){
  if($entry.Target -ne "$PSScriptRoot\Platforms\WebCuda"){throw 'Unexpected host WebCuda platform junction'}
  Remove-Item -LiteralPath $hostPlatform -Force
 }
}
New-Item -ItemType Directory -Force "$hostPlatform\Config" | Out-Null
Link-Directory "$hostPlatform\Source" "$PSScriptRoot\Platforms\WebCuda\Source"
# The host has the custom UBT below and must receive WEBCUDA_EMSDK when launched.
# Standard platform discovery also registers the separate settings module.
$metadata=Get-Content -LiteralPath "$PSScriptRoot\Platforms\WebCuda\Config\DataDrivenPlatformInfo.ini" -Raw
[IO.File]::WriteAllText("$hostPlatform\Config\DataDrivenPlatformInfo.ini",$metadata)
Copy-Item -LiteralPath "$PSScriptRoot\Platforms\WebCuda\Config\WebCuda_SDK.json" -Destination "$hostPlatform\Config\WebCuda_SDK.json" -Force
Link-Directory "$engineDestination\Binaries\ThirdParty" "$Engine\Engine\Binaries\ThirdParty"
Link-Directory "$engineDestination\Programs\UnrealHeaderTool\Config" "$Engine\Engine\Programs\UnrealHeaderTool\Config"
foreach($entry in Get-ChildItem -LiteralPath "$Engine\Engine\Binaries\DotNET" -Directory){
 if($entry.Name -ne 'UnrealBuildTool'){Link-Directory "$engineDestination\Binaries\DotNET\$($entry.Name)" $entry.FullName}
}
foreach($entry in Get-ChildItem -LiteralPath "$Engine\Engine\Binaries\DotNET" -File){
 $destination=Join-Path "$engineDestination\Binaries\DotNET" $entry.Name
 if(!(Test-Path -LiteralPath $destination)){Copy-Item -LiteralPath $entry.FullName -Destination $destination}
}
# Only BuildSettings is rebuilt here. Other binary files are immutable inputs;
# never run an unrestricted native build in this overlay. Mutable manifests get
# real copies, and the replaced module must never share a hard link with stock.
foreach($entry in Get-ChildItem -LiteralPath "$Engine\Engine\Binaries\Win64"){
 $destination=Join-Path "$engineDestination\Binaries\Win64" $entry.Name
 if(Test-Path -LiteralPath $destination){continue}
 if($entry.PSIsContainer){Link-Directory $destination $entry.FullName}
 elseif($entry.Name -like '*BuildSettings*' -or $entry.Extension -notin @('.dll','.exe','.pdb')){
  Copy-Item -LiteralPath $entry.FullName -Destination $destination
 }else{New-Item -ItemType HardLink -Path $destination -Target $entry.FullName | Out-Null}
}
$dotnet=Join-Path $Engine 'Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe'
$output="$engineDestination\Binaries\DotNET\UnrealBuildTool\"
& $dotnet build "$PSScriptRoot\BuildTool\UnrealBuildTool.WebCuda.csproj" "-p:EngineRoot=$Engine" "-p:OutputPath=$output" -v:quiet
if($LASTEXITCODE){throw 'Isolated host UBT compilation failed'}
Copy-Item -LiteralPath "$Engine\Engine\Binaries\DotNET\UnrealBuildTool\runtimes" -Destination $output -Recurse -Force
Write-Output "Isolated cook host: $hostRoot"
