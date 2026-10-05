param([string]$Clang='C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference='Stop'
$sampleRoot=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force "$sampleRoot\web\generated" | Out-Null
& $Clang --target=wasm32 -O3 -nostdlib '-Wl,--no-entry' '-Wl,--export-all' '-Wl,--export-memory' "$sampleRoot\shared\Scene.cpp" -o "$sampleRoot\web\generated\scene.wasm"
if($LASTEXITCODE){throw 'Wasm fixture build failed'}
