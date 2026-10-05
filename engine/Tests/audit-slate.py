"""Compile the actual Slate/font/image consumers using full-game UBT arguments."""
import concurrent.futures
import json
import pathlib
import shlex
import subprocess
import sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
BUILD=ROOT/'Intermediate/Build/WebCuda/wasm64/UnrealBrowserSample/Development'
OUT=ROOT/'engine/artifacts/slate-dependencies/engine-units';OUT.mkdir(parents=True,exist_ok=True)
SDK=ROOT/'engine/tools/emsdk'
UNITS=[('SlateCore','FontCacheFreeType.cpp'),('SlateCore','FontCacheHarfBuzz.cpp'),('SlateCore','SlateFontRenderer.cpp'),
       ('SlateCore','FontCache.cpp'),('SlateCore','FontCacheCompositeFont.cpp'),('Slate','AttributeInterpolator.cpp'),
       ('ImageWrapper','PngImageWrapper.cpp'),('Core','Compression.cpp')]
def compile_unit(item):
    module,name=item
    response=BUILD/module/(name+'.o.rsp')
    args=shlex.split(response.read_text(encoding='utf-8-sig'))
    for flag,suffix in [('-o','.o'),('-MF','.d')]:args[args.index(flag)+1]=str(OUT/(name+suffix))
    log=OUT/(name+'.log')
    with log.open('w',encoding='utf-8') as stream:
        result=subprocess.run([str(SDK/'python/3.13.3_64bit/python.exe'),str(SDK/'upstream/emscripten/em++.py'),*args],stdout=stream,stderr=subprocess.STDOUT)
    return {'source':name,'module':module,'exitCode':result.returncode,'errors':[line for line in log.read_text(encoding='utf-8').splitlines() if ': error:' in line or ': fatal error:' in line],'log':str(log)}
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(compile_unit,UNITS))
(OUT/'results.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
sys.exit(1 if any(item['exitCode'] for item in results) else 0)
