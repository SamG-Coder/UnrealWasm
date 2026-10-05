import pathlib
import subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=ROOT/'engine/tools/emsdk'
OUT=ROOT/'engine/artifacts/icu64'
DATA=pathlib.Path('D:/UnrealEngine/UE_5.8/Engine/Content/Internationalization/English/icudt64l')
subprocess.run([str(SDK/'python/3.13.3_64bit/python.exe'),str(SDK/'upstream/emscripten/em++.py'),
    '-m64','-O2','-DU_STATIC_IMPLEMENTATION','-I'+str(OUT/'include'),str(ROOT/'engine/Tests/icu-unreal.cpp'),
    str(OUT/'libicu64-wasm64.a'),'--no-entry','-sWASMFS=1','-sFORCE_FILESYSTEM=1',
    '-sALLOW_MEMORY_GROWTH=1','-sMODULARIZE=1','-sEXPORT_ES6=1','-sENVIRONMENT=web,node',
    '--embed-file',str(DATA)+'@/Content/Internationalization/icudt64l','-o',str(OUT/'check.mjs')],check=True)
