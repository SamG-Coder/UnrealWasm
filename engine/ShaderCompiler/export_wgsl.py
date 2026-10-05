"""Export the existing bounded Lumen buffer pass to WGSL and its constant-layout adapter."""
import hashlib
import json
import pathlib
import re
import subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/webgpu'
ENTRY = 'SetupAdaptiveProbeIndirectArgsCS'
SLANG = pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe')

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    prior = json.loads((ROOT / 'artifacts/exported' / (ENTRY + '.json')).read_text())
    source = pathlib.Path(prior['source'])
    original = source.read_bytes()
    if hashlib.sha256(original).hexdigest() != prior['sourceSHA256']:
        raise ValueError('Original captured UE shader changed; recapture/revalidate first')
    lowered, count = re.subn(r'\bRWBuffer\s*<\s*uint\s*>', 'RWStructuredBuffer<uint>', original.decode('utf-8-sig'))
    hlsl = OUT / (ENTRY + '.hlsl'); hlsl.write_text(lowered, encoding='utf-8')
    wgsl = OUT / (ENTRY + '.wgsl'); reflection = OUT / (ENTRY + '.reflection.json')
    subprocess.run([str(SLANG), '-lang', 'hlsl', str(hlsl), '-entry', ENTRY, '-stage', 'compute', '-target', 'wgsl',
                    '-o', str(wgsl), '-reflection-json', str(reflection)], check=True)
    code = wgsl.read_text()
    reflected = json.loads(reflection.read_text())
    parameters = reflected['parameters']
    if [(p['name'], p['binding']['index']) for p in parameters] != [('_RootShaderParameters',0),('NumAdaptiveScreenProbes',1),('RWScreenProbeIndirectArgs',2)]:
        raise ValueError('Shader bindings changed; review the supported compute profile')
    if reflected['entryPoints'][0]['threadGroupSize'] != [1,1,1]: raise ValueError('Unexpected workgroup size')
    fields = parameters[0]['type']['elementType']['fields']
    original_fields = {f['name']: f['binding'] for f in fields}
    block = re.search(r'struct SLANG_ParameterGroup_RootShaderParameters_std140_\d+\s*\{([^}]+)\}', code)
    if not block: raise ValueError('Unexpected WGSL constant layout')
    mappings = []; offset = 0; max_alignment = 16
    for align, name, kind in re.findall(r'@align\((\d+)\)\s+(\w+)_\d+\s*:\s*(u32|vec2<u32>)\s*,', block.group(1)):
        align = int(align); offset = (offset + align - 1) // align * align
        field = original_fields[name]; size = 8 if kind == 'vec2<u32>' else 4
        if size != field['size']: raise ValueError('Constant field size mismatch')
        mappings.append({'name': name, 'sourceOffset': field['offset'], 'destinationOffset': offset, 'size': size})
        offset += size; max_alignment = max(max_alignment, align)
    if len(mappings) != len(fields): raise ValueError('Unsupported WGSL field representation')
    size = (offset + max_alignment - 1) // max_alignment * max_alignment
    header = '#pragma once\n#include <cstdint>\nnamespace LumenExport {\n'
    header += 'inline constexpr char Source[] = R"WGSL(' + code + ')WGSL";\n'
    header += f'inline constexpr uint32_t ConstantBytes = {size};\n'
    header += 'struct Mapping { uint32_t Source, Destination, Bytes; };\ninline constexpr Mapping Fields[] = {\n'
    header += ''.join('{' + f"{m['sourceOffset']},{m['destinationOffset']},{m['size']}" + '},\n' for m in mappings)
    header += '};\n}\n'
    (OUT / 'LumenExport.h').write_text(header, encoding='utf-8')
    report = {'entry': ENTRY, 'profile': 'uint-buffer-compute', 'source': str(source), 'sourceSHA256': prior['sourceSHA256'],
              'wgslSHA256': hashlib.sha256(wgsl.read_bytes()).hexdigest(), 'typedBufferMappings': count,
              'constantBytes': size, 'constantMappings': mappings, 'originalShaderEdited': False, 'fullLumenRenderer': False}
    (OUT / 'shader-manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if source.read_bytes() != original: raise RuntimeError('Original shader changed')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()
