param([string]$Engine='D:\UnrealEngine\UE_5.8',[switch]$SkipPrepare)
$ErrorActionPreference='Stop'
if(!$SkipPrepare){& "$PSScriptRoot\prepare-cook-host.ps1" -Engine $Engine}
$hostRoot=Join-Path $PSScriptRoot 'artifacts\cook-host'
$tool="$hostRoot\Engine\Binaries\DotNET\UnrealBuildTool"
$env:WEBCUDA_EMSDK=Join-Path $PSScriptRoot 'tools\emsdk'
$env:PATH="$tool\runtimes\win-x64\native;"+$env:PATH
$dotnet="$Engine\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe"
# Generate real UBT arguments but execute only this module's explicitly listed
# actions. An unrestricted build would write other binaries shared with stock.
& $dotnet "$tool\UnrealBuildTool.dll" "-rootdirectory=$hostRoot" UnrealEditor Win64 Development -Module=BuildSettings -SkipBuild -SkipPreBuildTargets -NoUBTMakefiles -NoUBA -NoPCH -DisableUnity "-Log=$PSScriptRoot\artifacts\cook-host-generate.log"
if($LASTEXITCODE){throw 'Cook-host UBT argument generation failed'}
$module="$hostRoot\Engine\Intermediate\Build\Win64\x64\UnrealEditorNU\Development\BuildSettings"
$compiler='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64'
$resourceCompiler='C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\rc.exe'
$original="$Engine\Engine\Binaries\Win64\UnrealEditor-BuildSettings.dll"
$originalHash=(Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
$destination="$hostRoot\Engine\Binaries\Win64\UnrealEditor-BuildSettings.dll"
if((Get-Item -LiteralPath (Split-Path $destination)).LinkType){throw 'Host binary output directory must be independent'}
# prepare-cook-host copies BuildSettings, never hard links it. Verify the file
# itself has a single link before allowing the linker to open it for writing.
$links=@(& fsutil hardlink list $destination)
if($LASTEXITCODE -ne 0 -or $links.Count -ne 1){throw 'BuildSettings output is not an independent file'}
Push-Location "$hostRoot\Engine\Source"
try {
 foreach($unit in @('BuildSettings.cpp','PerModuleInline.gen.cpp','RHIStaticShaderPlatformNames.gen.cpp')){
  & "$compiler\cl.exe" "@$module\$unit.obj.rsp"
  if($LASTEXITCODE){throw "BuildSettings compile failed: $unit"}
 }
 # This SDK's RC response reader retains quotes around paths. Expand with the
 # standard Windows argv parser, preserving UBT's actual resource definitions.
 & "$PSScriptRoot\tools\emsdk\python\3.13.3_64bit\python.exe" "$PSScriptRoot\BuildTool\run-resource-compiler.py" $resourceCompiler "$module\Default.rc2.res.rsp"
 if($LASTEXITCODE){throw 'BuildSettings resource compilation failed'}
 & "$compiler\link.exe" "@$module\UnrealEditor-BuildSettings.dll.rsp"
 if($LASTEXITCODE){throw 'BuildSettings module link failed'}
}finally{Pop-Location}
if((Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ne $originalHash){throw 'Installed BuildSettings binary changed'}
& "$PSScriptRoot\tools\emsdk\python\3.13.3_64bit\python.exe" "$PSScriptRoot\Tests\validate-buildsettings.py"
if($LASTEXITCODE){throw 'BuildSettings ABI validation failed'}
