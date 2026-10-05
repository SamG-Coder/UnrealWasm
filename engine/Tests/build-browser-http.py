"""Build the same Fetch transport used by the Unreal HTTP platform module."""
import pathlib
import subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=ROOT/'engine/tools/emsdk'
SOURCE=ROOT/'engine/Platforms/WebCuda/Source/Runtime/Online/HTTP'
OUT=ROOT/'engine/artifacts/browser-http';OUT.mkdir(parents=True,exist_ok=True)
subprocess.run([str(SDK/'python/3.13.3_64bit/python.exe'),str(SDK/'upstream/emscripten/em++.py'),
    '-m64','-O2','-std=c++20','-I'+str(SOURCE/'Public'),str(SOURCE/'Private/WebCudaHttpTransport.cpp'),
    str(ROOT/'engine/Tests/browser-http.cpp'),'--no-entry','-sMODULARIZE=1','-sEXPORT_ES6=1',
    '-sENVIRONMENT=web','-sALLOW_MEMORY_GROWTH=1','-sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8',
    '-o',str(OUT/'http.mjs')],check=True)
