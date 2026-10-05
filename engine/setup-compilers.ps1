$ErrorActionPreference='Stop'
$sdk=Join-Path $PSScriptRoot 'tools\emsdk'
if(!(Test-Path -LiteralPath "$sdk\emsdk.bat")){
 New-Item -ItemType Directory -Force (Split-Path $sdk -Parent) | Out-Null
 & git clone --depth 1 https://github.com/emscripten-core/emsdk.git $sdk
 if($LASTEXITCODE){throw 'Official Emscripten SDK checkout failed'}
}
& "$sdk\emsdk.bat" install 6.0.11
if($LASTEXITCODE){throw 'Emscripten 6.0.11 installation failed'}
& "$sdk\emsdk.bat" activate 6.0.11
if($LASTEXITCODE){throw 'Emscripten activation failed'}
Write-Output 'Using local Emscripten 6.0.11. No permanent system PATH changes are required.'
Write-Output 'Shader export additionally uses the installed Vulkan SDK Slang compiler and CUDA 13.3 toolkit.'
