"""Compile the same browser host implementation linked into ApplicationCore."""
import pathlib
import subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=ROOT/'engine/tools/emsdk'
SOURCE=ROOT/'engine/Platforms/WebCuda/Source/Runtime/ApplicationCore'
OUT=ROOT/'engine/artifacts/browser-host'
OUT.mkdir(parents=True,exist_ok=True)
subprocess.run([str(SDK/'python/3.13.3_64bit/python.exe'),str(SDK/'upstream/emscripten/em++.py'),
    '-m64','-O2','-std=c++20','-I'+str(SOURCE/'Public'),str(SOURCE/'Private/WebCudaBrowserHost.cpp'),
    str(ROOT/'engine/Tests/browser-host.cpp'),'--no-entry','-sMODULARIZE=1','-sEXPORT_ES6=1',
    '-sENVIRONMENT=web','-sALLOW_MEMORY_GROWTH=1','-o',str(OUT/'host.mjs')],check=True)
