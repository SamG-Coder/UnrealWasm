$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$out=Join-Path $root 'engine\artifacts\shader-format'
$capture=Get-Content "$root\engine\artifacts\exported\SetupAdaptiveProbeIndirectArgsCS.json" -Raw | ConvertFrom-Json
$previousSdk=$env:WEBCUDA_EMSDK
$previousPath=$env:PATH
try {
    $env:WEBCUDA_EMSDK=Join-Path $root 'engine\tools\emsdk'
    $env:PATH=(Join-Path $root 'engine\artifacts\cook-host\Engine\Binaries\DotNET\UnrealBuildTool\runtimes\win-x64\native')+';'+$env:PATH
    if((Get-FileHash -LiteralPath $capture.source -Algorithm SHA256).Hash -ne $capture.sourceSHA256){throw 'Captured original shader hash changed'}
    & "$root\engine\artifacts\cook-host\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\UnrealBrowserSample.uproject" -run=WebCudaShaderProbe "-Source=$($capture.source)" -Entry=SetupAdaptiveProbeIndirectArgsCS "-Output=$out\probe.wcs" -unattended -nop4 -nosplash -nullrhi -NoShaderCompile "-abslog=$out\probe.log" *> "$out\probe-console.log"
    $code=$LASTEXITCODE
    if($code){throw "IShaderFormat probe failed with exit $code"}
    $actual=(Get-FileHash -LiteralPath "$out\probe.wcs" -Algorithm SHA256).Hash
    $expected=(Get-FileHash -LiteralPath "$root\engine\artifacts\webgpu\shader.wcs" -Algorithm SHA256).Hash
    if($actual -ne $expected){throw 'Unreal compiler output differs from browser-tested container'}
    $runtime=Get-Content "$root\engine\artifacts\webgpu\validation.json" -Raw | ConvertFrom-Json
    if(!$runtime.passed -or $runtime.lumen.shaderContainerSHA256 -ne $actual){throw 'Runtime validation is missing or stale'}
    @{passed=$true;completedUtc=[DateTime]::UtcNow.ToString('o');entry='SetupAdaptiveProbeIndirectArgsCS';engineCompilerExitCode=$code;shaderContainerSHA256=$actual;identicalToBrowserTestedContainer=$true;runtimeCases=$runtime.lumen.cases;exactUintComparisons=$runtime.lumen.exactUintComparisons;malformedContainerRejections=$runtime.shaderContainerRejections;fullSceneCooked=$false;fullSceneRendered=$false} | ConvertTo-Json | Set-Content "$out\integration-validation.json"
    Write-Output "PASS: Unreal IShaderFormat output is identical to the Wasm/WebGPU-tested container ($actual)"
} finally { $env:WEBCUDA_EMSDK=$previousSdk; $env:PATH=$previousPath }
