"""Link actual platform code using the compile environment produced by UBT."""
import pathlib
import shlex
import subprocess
ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / 'engine/tools/emsdk'
OUT = ROOT / 'engine/artifacts/platform-services'
OUT.mkdir(parents=True, exist_ok=True)
INTERMEDIATE = ROOT / 'Intermediate/Build/WebCuda/wasm64/UnrealBrowserSample/Development'
tokens = shlex.split((INTERMEDIATE / 'UnrealBrowserSample/SampleGameMode.cpp.o.rsp').read_text(encoding='utf-8-sig'))
flags = []
index = 0
while index < len(tokens):
    token = tokens[index]
    if token in ('-o', '-MF'):
        index += 2
        continue
    if token not in ('-c', '-MD') and not token.endswith('SampleGameMode.cpp'):
        flags.append(token)
    index += 1
command = [str(SDK / 'python/3.13.3_64bit/python.exe'), str(SDK / 'upstream/emscripten/em++.py'),
           *flags, str(ROOT / 'engine/Tests/platform-services.cpp'),
           str(INTERMEDIATE / 'Core/WebCudaPlatformMemory.cpp.o'),
           # The harness links inline math only, not Core's diagnostic/logging
           # subsystem. Engine compilation retains normal Development checks.
           '-DDO_CHECK=0', '-DENABLE_NAN_DIAGNOSTIC=0',
           '--no-entry', '-sEXPORTED_FUNCTIONS=_platform_run,_platform_files_run,_platform_memory_benchmark,_platform_chaos_transform,_platform_semaphore_violation',
           '-sWASMFS=1', '-sFORCE_FILESYSTEM=1', '-sEXPORTED_RUNTIME_METHODS=FS', '-sMODULARIZE=1', '-sEXPORT_ES6=1',
           '-sENVIRONMENT=web,node', '-sALLOW_MEMORY_GROWTH=1', '-o', str(OUT / 'services.mjs')]
subprocess.run(command, check=True)
subprocess.run([str(SDK / 'upstream/bin/wasm-dis.exe'), str(OUT / 'services.wasm'), '-o', str(OUT / 'services.wat')], check=True)
text = (OUT / 'services.wat').read_text()
for instruction in ('f32x4.mul', 'f64x2.mul'):
    if instruction not in text:
        raise RuntimeError(f'Expected actual SIMD instruction: {instruction}')
