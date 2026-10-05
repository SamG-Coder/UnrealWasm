"""Compile the concrete Unreal WebGPU resources with the live scene's UBT flags."""
import concurrent.futures
import hashlib
import json
import pathlib
import shlex
import subprocess
import sys

root=pathlib.Path(__file__).resolve().parents[2]
build=root/'Intermediate/Build/WebCuda/wasm64/UnrealBrowserSample/Development/WebGPURHI'
output=root/'engine/artifacts/webgpu-engine-resources'
output.mkdir(parents=True,exist_ok=True)
sdk=root/'engine/tools/emsdk'

def compile_unit(name):
    args=shlex.split((build/(name+'.o.rsp')).read_text(encoding='utf-8-sig'))
    for flag,suffix in [('-o','.o'),('-MF','.d')]:
        args[args.index(flag)+1]=str(output/(name+suffix))
    log=output/(name+'.log')
    with log.open('w',encoding='utf-8') as stream:
        result=subprocess.run([str(sdk/'python/3.13.3_64bit/python.exe'),str(sdk/'upstream/emscripten/em++.py'),*args],stdout=stream,stderr=subprocess.STDOUT)
    return dict(source=name,sourceSHA256=hashlib.sha256((root/'Source/WebGPURHI/Private'/name).read_bytes()).hexdigest(),
                exitCode=result.returncode,log=str(log),
                errors=[line for line in log.read_text(encoding='utf-8').splitlines() if ': error:' in line or ': fatal error:' in line])

with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    results=list(pool.map(compile_unit,['WebGPUDevice.cpp','WebGPURHIResources.cpp']))
(output/'results.json').write_text(json.dumps(results,indent=2))
(output/'headers.json').write_text(json.dumps({name:hashlib.sha256((root/'Source/WebGPURHI/Public'/name).read_bytes()).hexdigest()
    for name in ['WebGPUDevice.h','WebGPURHIResources.h','WebGPUUniformData.h','WebCudaShaderCode.h']},indent=2))
print(json.dumps(results,indent=2))
sys.exit(int(any(r['exitCode'] for r in results)))
