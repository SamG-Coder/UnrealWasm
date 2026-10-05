"""Compile Unreal's *preprocessed* shader output for the browser CUDA ABI.

This initial profile supports uint constant fields and uint structured/typed
buffers only. Unsupported resources fail closed. It is not a general Lumen
compiler yet. No shader algorithm is supplied by this exporter.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess


def digest(data):
    return hashlib.sha256(data).hexdigest()


def export(source, entry, out, slang):
    out.mkdir(parents=True, exist_ok=True)
    original_bytes = source.read_bytes()
    original = original_bytes.decode('utf-8-sig')
    # A typed uint buffer is a tightly packed uint array in this backend. This
    # changes resource representation, not indexing, control flow or arithmetic.
    lowered, substitutions = re.subn(r'\bRWBuffer\s*<\s*uint\s*>', 'RWStructuredBuffer<uint>', original)
    if re.search(r'\b(?:RW)?(?:Texture\w*|Buffer)\s*<', lowered):
        raise ValueError('Resource requires an unimplemented CUDA format/texture mapping')
    lowered_path = out / (entry + '.hlsl')
    lowered_path.write_text(lowered, encoding='utf-8')
    raw_path, reflection_path = out / (entry + '.slang.cu'), out / (entry + '.reflection.json')
    command = [str(slang), '-lang', 'hlsl', str(lowered_path), '-entry', entry,
               '-stage', 'compute', '-target', 'cuda', '-o', str(raw_path),
               '-reflection-json', str(reflection_path)]
    subprocess.run(command, check=True)
    reflection = json.loads(reflection_path.read_text())
    raw = raw_path.read_text()
    marker = '#line 1 "MoveShaderParametersToRootConstantBuffer"'
    if raw.count(marker) != 1:
        raise ValueError('Expected Unreal root parameter buffer in Slang output')
    # Replace Slang's broad CUDA runtime with our explicitly supported ABI
    # subset. The emitted shader's function bodies remain compiler output.
    body = raw.split(marker, 1)[1]
    body = re.sub(r'^#line[^\n]*\n', '', body, flags=re.M)
    match = re.search(r'extern "C" __constant__ (\w+) SLANG_globalParams;\s*#define (\w+) \(&SLANG_globalParams\)', body)
    if not match:
        raise ValueError('Unsupported Slang global parameter ABI')
    global_type, global_name = match.groups()
    body = body[:match.start()] + body[match.end():]
    # Constant fields are repacked into per-dispatch local storage. A CUDA
    # global-memory-only cache intrinsic is not valid for that address space.
    body = body.replace('__ldg(', 'webcuda_load(')
    members_match = re.search(r'struct ' + global_type + r'\s*\{([^}]+)\}', body)
    if not members_match:
        raise ValueError('Missing global resource declarations')
    members = members_match.group(1)
    params, setup, abi = [], [], []
    for p in reflection['parameters']:
        name, kind = p['name'], p['type']['kind']
        field_match = re.search(r'\b(' + re.escape(name) + r'_\d+)\s*;', members)
        if not field_match:
            raise ValueError('Cannot map reflected parameter ' + name)
        field = field_match.group(1)
        if kind == 'constantBuffer':
            type_match = re.search(r'(\w+)\s*\*\s*' + field + r'\s*;', members)
            if not type_match:
                raise ValueError('Unsupported constant buffer representation')
            typename = type_match.group(1)
            declaration = re.search(r'struct ' + typename + r'\s*\{([^}]+)\}', body).group(1)
            params.append('const unsigned char* ' + name)
            setup.append(typename + ' cb_' + name + '{};')
            fields = p['type']['elementType']['fields']
            for f in fields:
                fm = re.search(r'\b(uint[234]?)\s+(' + re.escape(f['name']) + r'_\d+)\s*;', declaration)
                if not fm or f['type']['kind'] not in ('scalar', 'vector'):
                    raise ValueError('Unsupported constant field ' + f['name'])
                ctype, emitted = fm.groups()
                offset = f['binding']['offset']
                setup.append(f'cb_{name}.{emitted} = *reinterpret_cast<const {ctype}*>({name} + {offset});')
            setup.append(f'globals.{field} = &cb_{name};')
            abi.append({'name': name, 'type': 'buffer', 'kind': kind,
                        'bytes': p['type']['elementVarLayout']['binding']['size'], 'fields': fields})
        elif kind == 'resource' and p['type'].get('baseShape') == 'structuredBuffer' and p['type'].get('resultType') == {'kind': 'scalar', 'scalarType': 'uint32'}:
            params += ['uint* ' + name, 'uint ' + name + '_count']
            setup += [f'globals.{field}.data = {name};', f'globals.{field}.count = {name}_count;']
            abi += [{'name': name, 'type': 'buffer', 'kind': 'structuredBuffer', 'elementBytes': 4},
                    {'name': name + '_count', 'type': 'u32', 'kind': 'elementCount'}]
        else:
            raise ValueError('Unimplemented reflected parameter: ' + name)
    # Browser kernel launch binds resources as arguments. Thread the Slang
    # global parameter pointer through generated functions instead of relying on
    # process-global CUDA constants (which would race between dispatches).
    functions = re.findall(r'__device__\s+\w+\s+(\w+)\s*\(', body)
    declaration = f'extern "C" __global__ void {entry}()'
    if body.count(declaration) != 1:
        raise ValueError('Unsupported entry signature')
    body = body.replace(declaration, f'__device__ void {entry}_body()')
    functions.append(entry + '_body')
    for name in functions:
        body = re.sub(r'\b' + name + r'\((?!\s*\))', name + '(' + global_name + ', ', body)
        body = re.sub(r'\b' + name + r'\(\s*\)', name + '(' + global_name + ')', body)
        body = re.sub(r'(__device__\s+\w+\s+' + name + r'\()' + global_name + r'\b',
                      r'\1' + global_type + '* ' + global_name, body)
    prelude = pathlib.Path(__file__).with_name('buffer-prelude.cuh').read_text()
    wrapper = '\nextern "C" __global__ void ' + entry + '(' + ', '.join(params) + ') {\n'
    wrapper += f'    {global_type} globals{{}};\n    ' + '\n    '.join(setup)
    wrapper += f'\n    {entry}_body(&globals);\n}}\n'
    code = prelude + '\n' + body + wrapper
    if any(token in code for token in ('#', '%:', '??', '\\')) or len(code.encode()) > 256 * 1024:
        raise ValueError('Generated CUDA exceeds browser source policy')
    cuda_path = out / (entry + '.cu')
    cuda_path.write_text(code, encoding='utf-8')
    manifest = {'schema': 1, 'profile': 'uint-buffer-compute', 'entry': entry,
                'source': str(source.resolve()), 'sourceSHA256': digest(original_bytes),
                'loweredSHA256': digest(lowered_path.read_bytes()),
                'slangOutputSHA256': digest(raw_path.read_bytes()),
                'cudaSHA256': digest(cuda_path.read_bytes()),
                'compiler': subprocess.check_output([str(slang), '-version'], text=True, stderr=subprocess.STDOUT).strip(),
                'typedBufferMappings': substitutions, 'parameters': abi,
                'workgroupSize': reflection['entryPoints'][0]['threadGroupSize'],
                'limitations': ['Buffer-only compute profile; no textures, rasterization or ray tracing',
                                'Exporting this pass is not exporting the full Lumen renderer']}
    (out / (entry + '.json')).write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    if source.read_bytes() != original_bytes:
        raise RuntimeError('Source changed during export')
    print(json.dumps({'entry': entry, 'cudaBytes': len(code.encode()), 'sourceUnchanged': True}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('--entry', required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--slang', type=pathlib.Path, default=pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
    args = parser.parse_args()
    export(args.source, args.entry, args.out, args.slang)
