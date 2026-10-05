$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$sdk=Join-Path $root 'engine\tools\emsdk'
$out=Join-Path $root 'engine\artifacts\webgpu'
New-Item -ItemType Directory -Force $out | Out-Null
& "$sdk\python\3.13.3_64bit\python.exe" "$PSScriptRoot\prepare-shader-fixture.py"
if($LASTEXITCODE){throw 'Original Lumen shader-container compilation failed'}
& "$sdk\python\3.13.3_64bit\python.exe" "$sdk\upstream\emscripten\em++.py" `
 -m64 -std=c++20 -O2 --use-port=emdawnwebgpu --js-library "$root\engine\Runtime\webgpu-wasm64.js" `
 "$root\Source\WebGPURHI\Private\WebGPUDevice.cpp" "$PSScriptRoot\webgpu-resources.cpp" `
 "-I$root\Source\WebGPURHI\Public" "-I$out" --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web `
 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=UTF8ToString `
 '-sEXPORTED_FUNCTIONS=_gpu_texture_clear_run,_gpu_texture_clear_done,_gpu_texture_clear_passed,_gpu_debug_begin,_gpu_debug_end,_gpu_clear_run,_gpu_clear_done,_gpu_clear_passed,_gpu_uniform_run,_gpu_uniform_done,_gpu_uniform_passed,_gpu_sampler_checks,_gpu_init,_gpu_state,_gpu_error,_gpu_run,_gpu_done,_gpu_passed,_gpu_lumen_input,_gpu_lumen_run,_gpu_lumen_done,_gpu_lumen_output,_gpu_shader_rejections,_gpu_memcpy_run,_gpu_memcpy_done,_gpu_memcpy_passed,_gpu_texture_caps,_gpu_texture_run,_gpu_texture_done,_gpu_texture_passed,_gpu_texture_rejections,_gpu_integrate_compile,_gpu_integrate_run,_gpu_integrate_done,_gpu_integrate_passed' `
 -o "$out\resources.mjs"
if($LASTEXITCODE){throw 'WebGPU backend build failed'}
