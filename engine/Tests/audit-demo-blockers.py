"""Compile essential units with the actual full game's UBT response files.

Outputs are isolated from the running build. No headers, flags or algorithms
are substituted to force success. This supplements the full build's diagnostics.
"""
import json
import pathlib
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / 'engine/tools/emsdk'
BUILD = ROOT / 'Intermediate/Build/WebCuda/wasm64/UnrealBrowserSample/Development'
OUT = ROOT / 'engine/artifacts/demo-attempt/essential-units'
OUT.mkdir(parents=True, exist_ok=True)
UNITS = [('ApplicationCore', 'ApplicationCore.cpp'), ('Launch', 'Launch.cpp'),
         ('ApplicationCore', 'WebCudaApplication.cpp'), ('ApplicationCore', 'WebCudaBrowserHost.cpp'),
         ('Launch', 'WebCudaMain.cpp'),
         ('InputCore', 'InputCoreTypes.cpp'),
         ('Launch', 'LaunchEngineLoop.cpp'), ('RHI', 'DynamicRHI.cpp'),
         ('Core', 'UnrealMemory.cpp'), ('Core', 'ICUInternationalization.cpp')]
results = []
for module, unit in UNITS:
    response = BUILD / module / f'{unit}.o.rsp'
    args = shlex.split(response.read_text(encoding='utf-8-sig'))
    # Redirect outputs and dependencies, preserving the source and compile flags.
    for option, suffix in [('-o', '.o'), ('-MF', '.d')]:
        args[args.index(option) + 1] = str(OUT / f'{unit}{suffix}')
    log = OUT / f'{unit}.log'
    with log.open('w', encoding='utf-8') as stream:
        completed = subprocess.run([str(SDK / 'python/3.13.3_64bit/python.exe'),
                                   str(SDK / 'upstream/emscripten/em++.py'), *args],
                                  stdout=stream, stderr=subprocess.STDOUT)
    errors = [line for line in log.read_text(encoding='utf-8').splitlines()
              if ': error:' in line or ': fatal error:' in line]
    result = {'module': module, 'source': unit, 'exitCode': completed.returncode,
              'responseFile': str(response), 'log': str(log), 'errors': errors}
    results.append(result)
    print(json.dumps(result), flush=True)
(OUT / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
sys.exit(1 if any(result['exitCode'] for result in results) else 0)
