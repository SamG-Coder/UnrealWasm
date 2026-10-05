"""Compare the rebuilt UE module against the installed module in one process."""
import ctypes
import hashlib
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'engine/artifacts/cook-host'
STOCK = pathlib.Path('D:/UnrealEngine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-BuildSettings.dll')
REBUILT = OUT / 'Engine/Binaries/Win64/UnrealEditor-BuildSettings.dll'
DUMPBIN = pathlib.Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/dumpbin.exe')

def exports(path):
    result = subprocess.run([str(DUMPBIN), '/exports', str(path)], check=True, capture_output=True, text=True)
    return set(re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\?\S+)', result.stdout, re.M))

class StaticName(ctypes.Structure):
    _fields_ = [('name', ctypes.c_wchar_p), ('platform', ctypes.c_wchar_p), ('index', ctypes.c_int)]

def static_names(dll):
    entries = (StaticName * 17).in_dll(dll, '?GStaticShaderNames@@3PAUFStaticNameMapEntry@@A')
    result = []
    for entry in entries:
        if entry.name is None:
            return result
        result.append(dict(name=entry.name, platform=entry.platform, index=entry.index))
    raise AssertionError('Shader table missing terminator')

stock_exports, rebuilt_exports = exports(STOCK), exports(REBUILT)
assert stock_exports == rebuilt_exports, (stock_exports - rebuilt_exports, rebuilt_exports - stock_exports)
stock, rebuilt = ctypes.WinDLL(str(STOCK)), ctypes.WinDLL(str(REBUILT))
checks = []
for name in sorted(stock_exports):
    if '@BuildSettings@@' not in name or 'GetVariable@' in name:
        continue
    if 'YAPEB_WXZ' in name:
        result_type = ctypes.c_wchar_p
    elif 'YAPEBDXZ' in name:
        result_type = ctypes.c_char_p
    elif 'YAHXZ' in name:
        result_type = ctypes.c_int
    elif 'YA_KXZ' in name:
        result_type = ctypes.c_uint64
    elif 'YA_NXZ' in name:
        result_type = ctypes.c_bool
    else:
        raise AssertionError(f'Unknown BuildSettings export ABI: {name}')
    a, b = getattr(stock, name), getattr(rebuilt, name)
    a.argtypes = b.argtypes = []
    a.restype = b.restype = result_type
    equal = a() == b()
    function_name = name.split('@')[0][1:]
    # A source rebuild has its own timestamps, private machine information and
    # debug source paths. Engine version and allocator/ABI settings must match.
    expected_difference = function_name in {
        'GetBuildDate', 'GetBuildTime', 'GetBuildUser', 'GetBuildUserDomain',
        'GetBuildMachine', 'GetBuildURL', 'GetVfsPaths', 'GetVfsPathsWide',
    }
    assert equal or expected_difference, f'Incompatible {function_name}'
    checks.append(dict(function=function_name, equal=equal, differenceAllowed=expected_difference))
old, new = static_names(stock), static_names(rebuilt)
assert all(entry in new for entry in old), 'Lost an installed static shader platform'
assert new == old + [dict(name='WEBCUDA', platform='WebCuda', index=32)]
report = dict(
    passed=True, stockSha256=hashlib.sha256(STOCK.read_bytes()).hexdigest(),
    rebuiltSha256=hashlib.sha256(REBUILT.read_bytes()).hexdigest(),
    exportCount=len(stock_exports), metadataChecks=checks,
    stockShaderPlatforms=old, rebuiltShaderPlatforms=new,
    engineBootTested=False, sceneCooked=False, sceneRendered=False,
)
(OUT / 'buildsettings-validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(dict(passed=True, exportCount=len(stock_exports), metadataChecks=len(checks), shaderPlatforms=new)))
