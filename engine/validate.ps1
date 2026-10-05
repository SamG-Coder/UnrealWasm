param([switch]$CaptureShader,[switch]$SkipBuild,[string]$Engine='D:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
$sampleRoot=Split-Path $PSScriptRoot -Parent
Push-Location $sampleRoot
try {
 if(!$SkipBuild){
  & "$PSScriptRoot\build-tool.ps1" -Engine $Engine
  if($LASTEXITCODE){throw 'Custom UnrealBuildTool build failed'}
  & "$PSScriptRoot\ubt.ps1" WebCudaBridge WebCuda Development "-Project=$sampleRoot\UnrealBrowserSample.uproject" -WaitMutex -NoUBA -NoBuildUHT
  if($LASTEXITCODE){throw 'WebCudaBridge Wasm target failed'}
 }
 if($CaptureShader){
  $output=Join-Path $PSScriptRoot 'artifacts\shader-export'
  $process=Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList @("`"$sampleRoot\UnrealBrowserSample.uproject`"",'/Game/Sample','-game','-windowed','-ResX=1280','-ResY=720','-unattended','-nosplash','-SampleBenchmark',"-SampleOutput=`"$output`"",'-ExecCmds="r.DumpShaderDebugInfo 1,RecompileShaders LumenScreenProbeGather.usf CFLAG_Debug"') -WindowStyle Hidden -PassThru -Wait
  if($process.ExitCode){throw 'Original Unreal shader capture failed'}
 }
 $source=Join-Path $sampleRoot 'Saved\ShaderDebugInfo\PCD3D_SM6\Global\FSetupAdaptiveProbeIndirectArgsCS\0\LumenScreenProbeGather.usf'
 if(!(Test-Path -LiteralPath $source)){throw 'Unreal preprocessed shader is missing. Run engine/validate.ps1 -CaptureShader after building the native sample.'}
 & python "$PSScriptRoot\ShaderCompiler\export_shader.py" $source --entry SetupAdaptiveProbeIndirectArgsCS --out "$PSScriptRoot\artifacts\exported"
 if($LASTEXITCODE){throw 'Original shader CUDA export failed'}
 & "$PSScriptRoot\Tests\build-native-reference.cmd"
 if($LASTEXITCODE){throw 'Native shader reference harness build failed'}
 & node "$PSScriptRoot\Tests\browser-lumen.mjs"
 if($LASTEXITCODE){throw 'Browser CUDA shader parity failed'}
 $integrationSource=Join-Path $sampleRoot 'Saved\ShaderDebugInfo\PCD3D_SM6\Global\FScreenProbeIntegrateCS\0\LumenScreenProbeGather.usf'
 & python "$PSScriptRoot\ShaderCompiler\compile_texture_shader.py" $integrationSource --entry ScreenProbeIntegrateCS --out "$PSScriptRoot\artifacts\texture-compile"
 if($LASTEXITCODE){throw 'Offline texture shader compilation failed'}
} finally { Pop-Location }
