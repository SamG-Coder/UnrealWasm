$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$sdk=Join-Path $root 'engine/tools/emsdk'
$out=Join-Path $root 'engine/artifacts/raster-probe'
& "$sdk/python/3.13.3_64bit/python.exe" "$PSScriptRoot/prepare-raster-fixture.py"
if($LASTEXITCODE){throw 'Original raster shader fixture failed'}
& "$sdk/python/3.13.3_64bit/python.exe" "$sdk/upstream/emscripten/em++.py" `
 -m64 -std=c++20 -O2 --use-port=emdawnwebgpu --js-library "$root/engine/Runtime/webgpu-wasm64.js" `
 "$root/Source/WebGPURHI/Private/WebGPUDevice.cpp" "$PSScriptRoot/raster-draw.cpp" `
 "-I$root/Source/WebGPURHI/Public" "-I$out" --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web `
 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=UTF8ToString `
 '-sEXPORTED_FUNCTIONS=_raster_init,_raster_state,_raster_error,_raster_run,_raster_done,_raster_passed' `
 -o "$out/raster.mjs"
if($LASTEXITCODE){throw 'Wasm raster draw build failed'}
