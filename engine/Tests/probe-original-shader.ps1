param([string]$ShaderType='FSetupAdaptiveProbeIndirectArgsCS',[int]$Permutation=0,[string]$Name='original-lumen',[string]$LegacyOutputName='',[switch]$MetadataOnly)
$ErrorActionPreference='Stop'
if($Name -notmatch '^[a-zA-Z0-9_-]+$'){throw 'Invalid artifact name'}
if($LegacyOutputName -and $LegacyOutputName -notmatch '^[a-zA-Z0-9_-]+$'){throw 'Invalid legacy artifact name'}
$root=Split-Path (Split-Path $PSScriptRoot)
$out=Join-Path $root 'engine\artifacts\shader-format'
$previousSdk=$env:WEBCUDA_EMSDK
$previousPath=$env:PATH
try {
    $env:WEBCUDA_EMSDK=Join-Path $root 'engine\tools\emsdk'
    $env:PATH=(Join-Path $root 'engine\artifacts\cook-host\Engine\Binaries\DotNET\UnrealBuildTool\runtimes\win-x64\native')+';'+$env:PATH
    $extraArgs=@()
    if($MetadataOnly){$extraArgs+= '-MetadataOnly'}
    & "$root\engine\artifacts\cook-host\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\UnrealBrowserSample.uproject" -run=WebCudaShaderProbe "-ShaderType=$ShaderType" "-Permutation=$Permutation" "-Output=$out\$Name.wcs" -unattended -nop4 -nosplash -nullrhi -noshaderworker @extraArgs "-abslog=$out\$Name.log" *> "$out\$Name-console.log"
    if($LASTEXITCODE){Get-Content "$out\$Name-console.log" -Tail 22; throw "Original shader probe failed: $LASTEXITCODE"}
    if($MetadataOnly) {
        $metadata=Get-Content "$out\$Name.wcs.metadata.json" -Raw | ConvertFrom-Json
        if($metadata.shaderType -ne $ShaderType -or $metadata.permutation -ne $Permutation){throw 'Shader metadata does not match requested type/permutation'}
        Write-Output "PASS: original $ShaderType permutation $Permutation parameter metadata captured"
        return
    }
    $report=Get-Content "$out\$Name.wcs.json" -Raw | ConvertFrom-Json
    if(!$report.originalEnginePreprocessing -or !$report.originalShaderConstructedAndBindingsVerified){throw 'Original shader preparation/binding was not verified'}
    Write-Output "PASS: $ShaderType permutation $Permutation, original preprocessing, parameter parser and shader-object binding"
    if($LegacyOutputName) {
        & "$root\engine\artifacts\cook-host\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\UnrealBrowserSample.uproject" -run=WebCudaShaderProbe "-Source=$out\$Name.wcs.preprocessed.hlsl" "-Entry=$($report.entry)" "-Output=$out\$LegacyOutputName.wcs" -unattended -nop4 -nosplash -nullrhi -NoShaderCompile "-abslog=$out\$LegacyOutputName.log" *> "$out\$LegacyOutputName-console.log"
        if($LASTEXITCODE){throw "Legacy parameter probe failed: $LASTEXITCODE"}
        $legacy=Get-Content "$out\$LegacyOutputName.wcs.json" -Raw | ConvertFrom-Json
        if($legacy.stableRootParameters){throw 'Expected loose-global parameter preparation'}
        Write-Output "PASS: retained original $ShaderType preprocessing compiled through legacy global parameters"
    }
} finally { $env:WEBCUDA_EMSDK=$previousSdk; $env:PATH=$previousPath }
