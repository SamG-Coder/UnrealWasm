"""Build the Oodle source supplied by the local Unreal installation for Wasm64.

The SDK archive, extracted source and resulting binaries stay in ignored local
artifacts. This is recompilation of the actual SDK, not DLL binary translation.
"""
import concurrent.futures
import hashlib
import json
import pathlib
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = pathlib.Path('D:/UnrealEngine/UE_5.8')
SDK = ENGINE / 'Engine/Source/Runtime/OodleDataCompression/Sdks/2.9.16'
OUT = ROOT / 'artifacts/oodle'
EMSDK = ROOT / 'tools/emsdk'
PYTHON = EMSDK / 'python/3.13.3_64bit/python.exe'
EMXX = EMSDK / 'upstream/emscripten/em++.py'
FLAGS = ['-m64', '-msimd128', '-std=c++17', '-O2', '-DNDEBUG',
         '-DOODLE_BUILDING_LIB', '-DOODLE_BUILDING_DATA']


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    archive = SDK / 'src/oodle2_src_for_unreal_data_2.9.16.zip'
    extracted = OUT / 'source'
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if not (extracted / entry.filename).resolve().is_relative_to(extracted.resolve()):
                raise ValueError('Unsafe SDK archive path')
        z.extractall(extracted)
    core, base = extracted / 'oodle2/core', extracted / 'oodle2/base'
    includes = ['-I' + str(p) for p in (core, core / 'public', base)]
    sources = sorted(core.glob('*.cpp')) + sorted(base.glob('*.cpp'))
    objdir = OUT / 'objects'
    objdir.mkdir(exist_ok=True)
    def compile_one(source):
        obj = objdir / (source.stem + '.o')
        command = [str(PYTHON), str(EMXX), *FLAGS, *includes, '-c', str(source), '-o', str(obj)]
        result = subprocess.run(command, capture_output=True, text=True)
        (objdir / (source.stem + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return source.name, obj, result.returncode
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results = list(pool.map(compile_one, sources))
    failures = [name for name, _, code in results if code]
    if failures:
        raise RuntimeError('Oodle source build failed; see per-file logs: ' + ', '.join(failures))
    library = OUT / 'liboo2corewasm64.a'
    subprocess.run([str(EMSDK / 'upstream/bin/llvm-ar.exe'), 'rcs', str(library), *[str(obj) for _, obj, _ in results]], check=True)
    # Export the C API used by the validation harness. The static archive above
    # retains all SDK functions for the Unreal linker.
    command = [str(PYTHON), str(EMXX), '-m64', '-msimd128', '-O2',
               str(ROOT / 'Tests/oodle-wrapper.cpp'), '-I' + str(SDK / 'include'), str(library),
               '--no-entry', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,node',
               '-sALLOW_MEMORY_GROWTH=1', '-sEXPORTED_FUNCTIONS=_malloc,_free,_oodle_init,_oodle_decompress,_oodle_compress,_oodle_bound',
               '-o', str(OUT / 'oodle.mjs')]
    subprocess.run(command, check=True)
    manifest = {'sdk': 'Oodle Data 2.9.16', 'kind': 'source-recompiled-to-wasm64',
                'sourceArchive': str(archive), 'sourceSHA256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                'sourceFiles': len(sources), 'sourceEdits': 0, 'flags': FLAGS,
                'librarySHA256': hashlib.sha256(library.read_bytes()).hexdigest(),
                'wasmSHA256': hashlib.sha256((OUT / 'oodle.wasm').read_bytes()).hexdigest(),
                'libraryBytes': library.stat().st_size, 'wasmBytes': (OUT / 'oodle.wasm').stat().st_size}
    (OUT / 'build.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
