"""Build and execute dependency portability checks, separate from Unreal ABI integration."""
import hashlib
import json
import pathlib
import subprocess
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
SDK = ROOT / 'tools/emsdk'
PYTHON = SDK / 'python/3.13.3_64bit/python.exe'
EM = SDK / 'upstream/emscripten'
OUT = ROOT / 'artifacts/open-source-ports'
AH_COMMIT = '82d85f7183e207bf4405d427a723a8a85406ddf4'  # upstream tag 1.3.2

def main():
    ah = OUT / 'AHEasing'
    ah.mkdir(parents=True, exist_ok=True)
    for filename in ('easing.h', 'easing.c', 'UNLICENSE'):
        source = filename if filename == 'UNLICENSE' else 'AHEasing/' + filename
        url = f'https://raw.githubusercontent.com/warrenm/AHEasing/{AH_COMMIT}/{source}'
        (ah / filename).write_bytes(urllib.request.urlopen(url).read())
    def run(*args):
        subprocess.run([str(x) for x in args], check=True)
    run(PYTHON, EM / 'embuilder.py', 'build', 'freetype', 'icu', '--wasm64')
    run(PYTHON, EM / 'emcc.py', '-m64', '-O2', '-c', ah / 'easing.c', '-o', OUT / 'easing.o')
    run(PYTHON, EM / 'em++.py', '-m64', '-O2', ROOT / 'Tests/open-source-ports.cpp',
        '-I' + str(ah), OUT / 'easing.o', '--use-port=freetype', '--use-port=icu',
        '--embed-file', 'D:/UnrealEngine/UE_5.8/Engine/Content/Slate/Fonts/Roboto-Regular.ttf@/Roboto-Regular.ttf',
        '-sENVIRONMENT=node', '-sALLOW_MEMORY_GROWTH=1', '-o', OUT / 'check.cjs')
    result = subprocess.run(['node', str(OUT / 'check.cjs')], check=True, capture_output=True, text=True)
    report = json.loads(result.stdout)
    report['runtime'] = 'Node.js WebAssembly'
    report['ahEasingCommit'] = AH_COMMIT
    report['wasmSHA256'] = hashlib.sha256((OUT / 'check.wasm').read_bytes()).hexdigest()
    report['limits'] = ['Not linked into Unreal yet', 'ICU locale data not staged or tested',
                        'Port versions differ from native UE headers', 'AHFloat is double on Wasm64 LP64']
    (OUT / 'validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
