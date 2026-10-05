"""Compile new HTTP units with the full game's actual HTTP module arguments."""
import json
import pathlib
import shlex
import subprocess
import sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=ROOT/'engine/tools/emsdk';OUT=ROOT/'engine/artifacts/browser-http/engine-units'
OUT.mkdir(parents=True,exist_ok=True)
BUILD=ROOT/'Intermediate/Build/WebCuda/wasm64/UnrealBrowserSample/Development'
results=[]
for module,name in [('HTTP','WebCudaHttpTransport.cpp'),('HTTP','WebCudaPlatformHttp.cpp'),('Launch','WebCudaMain.cpp')]:
    response=BUILD/module/(name+'.o.rsp')
    args=shlex.split(response.read_text(encoding='utf-8-sig'))
    for flag,suffix in [('-o','.o'),('-MF','.d')]:args[args.index(flag)+1]=str(OUT/(name+suffix))
    log=OUT/(name+'.log')
    with log.open('w',encoding='utf-8') as stream:
        completed=subprocess.run([str(SDK/'python/3.13.3_64bit/python.exe'),str(SDK/'upstream/emscripten/em++.py'),*args],stdout=stream,stderr=subprocess.STDOUT)
    results.append(dict(source=name,responseFile=str(response),exitCode=completed.returncode,errors=[line for line in log.read_text(encoding='utf-8').splitlines() if ': error:' in line or ': fatal error:' in line]))
(OUT/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results,indent=2));sys.exit(int(any(r['exitCode'] for r in results)))
