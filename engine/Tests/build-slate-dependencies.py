import pathlib
import subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
SDK=ROOT/'engine/tools/emsdk';OUT=ROOT/'engine/artifacts/slate-dependencies'
INCLUDE=OUT/'include';ICU=ROOT/'engine/artifacts/icu64'
CONTENT=pathlib.Path('D:/UnrealEngine/UE_5.8/Engine/Content')
args=[str(SDK/'python/3.13.3_64bit/python.exe'),str(SDK/'upstream/emscripten/em++.py'),'-m64','-O2','-std=c++17','-DU_STATIC_IMPLEMENTATION','-DAH_EASING_USE_DBL_PRECIS']
for inc in (INCLUDE,INCLUDE/'freetype2',INCLUDE/'harfbuzz',INCLUDE/'png',INCLUDE/'zlib',ICU/'include'):args+=['-I'+str(inc)]
args+=[str(ROOT/'engine/Tests/slate-dependencies.cpp')]
for lib in ('harfbuzz','freetype','png','z','aheasing'):args+=[str(OUT/('lib'+lib+'-wasm64.a'))]
args+=[str(ICU/'libicu64-wasm64.a'),'--no-entry','-sWASMFS=1','-sFORCE_FILESYSTEM=1','-sALLOW_MEMORY_GROWTH=1','-sMODULARIZE=1','-sEXPORT_ES6=1','-sENVIRONMENT=web,node']
for font in ('Roboto-Regular.ttf','NotoNaskhArabicUI-Regular.ttf'):args+=['--embed-file',str(CONTENT/'Slate/Fonts'/font)+'@/'+font]
args+=['--embed-file',str(CONTENT/'Internationalization/English/icudt64l')+'@/Content/Internationalization/icudt64l','-o',str(OUT/'check.mjs')]
subprocess.run(args,check=True)
