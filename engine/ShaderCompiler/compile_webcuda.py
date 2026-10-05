"""Compile UE-preprocessed HLSL into the versioned WebCuda shader container.

Initial resource ABI: compute, scalar/vector root constants and uint/uint2/uint4 buffers.
Unsupported stages/resources/layouts fail without producing successful output.
Original shader algorithms are never supplied or replaced by this compiler.
"""
import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess

MAGIC = 0x48534357  # WCSH, little endian
VERSION = 1

def compile_shader(source, entry, output, slang):
    if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', entry):
        raise ValueError('Invalid HLSL entry identifier')
    output.mkdir(parents=True, exist_ok=True)
    original = source.read_bytes()
    lowered, substitutions = re.subn(r'\bRWBuffer\s*<\s*uint\s*>', 'RWStructuredBuffer<uint>', original.decode('utf-8-sig'))
    # Non-multisampled Texture.Load(location, zeroOffset) has the same texel
    # address as Load(location). Slang's WGSL target rejects the offset overload.
    # Restrict this to declared non-MS texture receivers and literal zero; a
    # multisampled texture's second argument is a sample index and must remain.
    texture_types = {}
    for resource_type, name in re.findall(r'\b(Texture\w*)\s*<[^;{}>]+>\s+(\w+)', lowered):
        texture_types.setdefault(name, set()).add(resource_type)
    zero_offset_loads = 0
    def lower_zero_offset(match):
        nonlocal zero_offset_loads
        types = texture_types.get(match.group(1), set())
        if types and all(re.fullmatch(r'Texture(?:1D|2D|3D)(?:Array)?', t) for t in types):
            zero_offset_loads += 1
            return f'{match.group(1)}.Load({match.group(2)})'
        return match.group(0)
    lowered = re.sub(r'\b(\w+)\.Load\(\s*(\w+)\s*,\s*0\s*\)', lower_zero_offset, lowered)
    # Validate live resources from reflection, not declarations in unreachable
    # included functions. Slang may eliminate unused texture declarations.
    input_path, code_path, reflection_path = output/'input.hlsl', output/'shader.wgsl', output/'reflection.json'
    input_path.write_text(lowered, encoding='utf-8')
    command = [str(slang), '-lang', 'hlsl', str(input_path), '-entry', entry, '-stage', 'compute',
               '-target', 'wgsl', '-o', str(code_path), '-reflection-json', str(reflection_path)]
    # Unreal's detached compiler host can have an invalid inherited stdin on
    # Windows. Slang consumes a file, so give it an explicit valid null handle.
    result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True, text=True)
    (output/'slang.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode:
        raise ValueError(result.stdout+result.stderr)
    # A successful process exit can still hide a change in texture storage
    # semantics (for example r8 silently becoming rgba32float). Never cook that.
    if re.search(r'\bwarning\[E31105\]', result.stdout+result.stderr):
        raise ValueError('Slang changed a requested image format; WebCuda requires exact storage formats.\n'+result.stdout+result.stderr)
    reflection = json.loads(reflection_path.read_text())
    code = code_path.read_text(encoding='utf-8')
    entries = reflection['entryPoints']
    if len(entries) != 1 or entries[0]['name'] != entry:
        raise ValueError('Unexpected reflected entry point')
    groups = entries[0]['threadGroupSize']
    if len(groups) != 3 or any(not isinstance(v, int) or v <= 0 for v in groups) or groups[0]*groups[1]*groups[2] > 1024:
        raise ValueError('Invalid compute workgroup size')
    resources, mappings, parameters = [], [], []
    source_bytes = constant_bytes = 0
    reflected_parameters = reflection['parameters']
    globals_ = [p for p in reflected_parameters if p['binding'].get('kind') == 'uniform']
    constant_mode = 'none'
    if globals_:
        # Slang reflects loose globals individually, but emits their actual
        # uniform block as globalParams. Pair that block with the reflected
        # offsets; do not invent a layout or change shader expressions.
        declaration = re.search(r'@binding\((\d+)\)\s*@group\(0\)\s*var<uniform>\s+globalParams_\d+\s*:', code)
        if not declaration:
            raise ValueError('Missing emitted uniform block for implicit global constants')
        size = max(p['binding']['offset']+p['binding']['size'] for p in globals_)
        reflected_parameters = [dict(name='globalParams', implicitGlobals=True,
            binding=dict(kind='descriptorTableSlot', index=int(declaration.group(1))),
            type=dict(kind='constantBuffer',elementVarLayout=dict(binding=dict(size=(size+15)//16*16)),
                      elementType=dict(fields=globals_)))] + [p for p in reflected_parameters if p not in globals_]
    for parameter in reflected_parameters:
        name, binding, kind = parameter['name'], parameter['binding'], parameter['type']['kind']
        index = binding.get('index', -1)
        if binding.get('kind') != 'descriptorTableSlot' or binding.get('space', 0) != 0 or not isinstance(index, int) or not 0 <= index < 16:
            raise ValueError('Unsupported descriptor binding for '+name)
        if kind == 'constantBuffer':
            if (name != '_RootShaderParameters' and not parameter.get('implicitGlobals')) or source_bytes:
                raise ValueError('Uniform-buffer object ABI not implemented for '+name)
            constant_mode = 'globals' if parameter.get('implicitGlobals') else 'root'
            source_bytes = parameter['type']['elementVarLayout']['binding']['size']
            declaration = re.search(r'@binding\('+str(index)+r'\)\s*@group\(0\)\s*var<uniform>\s+'+re.escape(name)+r'_\d+\s*:\s*(\w+);', code)
            if not declaration:
                raise ValueError('Missing emitted root constant binding')
            block = re.search(r'struct\s+'+declaration.group(1)+r'\s*\{([^}]+)\}', code)
            fields = {f['name']: f for f in parameter['type']['elementType']['fields']}
            offset, alignment = 0, 16
            emitted = re.findall(r'@align\((\d+)\)\s+(\w+)_\d+\s*:\s*(u32|i32|f32|vec[234]<(?:u32|i32|f32)>)\s*,', block.group(1))
            if len(emitted) != len(fields):
                raise ValueError('Root constant ABI requires scalar/vector fields')
            seen = set()
            for align, field_name, scalar in emitted:
                if field_name not in fields or field_name in seen:
                    raise ValueError('Root constant field mapping is not one-to-one')
                seen.add(field_name)
                field = fields[field_name]
                align = int(align)
                if align <= 0 or align & (align-1):
                    raise ValueError('Invalid WGSL member alignment')
                size = 4*(int(scalar[3]) if scalar.startswith('vec') else 1)
                source_offset = field['binding']['offset']
                if size != field['binding']['size'] or source_offset+size > source_bytes:
                    raise ValueError('Constant field size differs between HLSL and WGSL')
                offset = (offset+align-1)//align*align
                mappings.append([source_offset, offset, size])
                parameters.append(dict(name=field_name, kind='LooseData', buffer=index, offset=source_offset, size=size))
                offset += size
                alignment = max(alignment, align)
            constant_bytes = (offset+alignment-1)//alignment*alignment
            resources.append([index, 0])
        elif kind == 'resource' and parameter['type'].get('baseShape') == 'structuredBuffer':
            element = parameter['type'].get('resultType')
            scalar = {'kind':'scalar','scalarType':'uint32'}
            if element == scalar:
                storage_type = 'u32'
            elif (isinstance(element, dict) and element.get('kind') == 'vector'
                  and element.get('elementType') == scalar and element.get('elementCount') in (2, 4)):
                storage_type = f'vec{element["elementCount"]}<u32>'
            else:
                # uint3 needs a 12-to-16-byte element-stride conversion. Structs,
                # atomics and other scalar representations need their own ABI.
                raise ValueError('Unsupported structured buffer element layout: '+name)
            access = re.search(r'@binding\('+str(index)+r'\)\s*@group\(0\)\s*var<storage,\s*(read|read_write)>\s+'+re.escape(name)+r'_\d+\s*:\s*array<'+re.escape(storage_type)+r'>', code)
            if not access:
                raise ValueError('Emitted buffer layout differs from reflection: '+name)
            writable = access.group(1) == 'read_write'
            resources.append([index, 2 if writable else 1])
            parameters.append(dict(name=name, kind='UAV' if writable else 'SRV', buffer=0, offset=index, size=1))
        else:
            raise ValueError('Unimplemented WebCuda resource ABI: '+name)
    if len({r[0] for r in resources}) != len(resources):
        raise ValueError('Duplicate descriptor binding')
    if any(p['offset']+p['size'] > 65535 for p in parameters):
        raise ValueError('Parameter exceeds Unreal parameter-map range')
    # Fail on unreflected/implicit WGSL bindings rather than silently omitting them.
    actual_bindings = re.findall(r'@binding\((\d+)\)\s*@group\((\d+)\)', code)
    if sorted(actual_bindings) != sorted((str(r[0]), '0') for r in resources):
        raise ValueError('WGSL bindings and reflection differ')
    entry_bytes, code_bytes = entry.encode('utf-8'), code.encode('utf-8')
    header = [MAGIC, VERSION, 1, *groups, source_bytes, constant_bytes,
              len(resources), len(mappings), len(entry_bytes), len(code_bytes)]
    payload = struct.pack('<12I', *header)
    payload += b''.join(struct.pack('<2I', *r) for r in resources)
    payload += b''.join(struct.pack('<3I', *m) for m in mappings)
    payload += entry_bytes+code_bytes
    manifest = dict(version=VERSION, entry=entry, stage='compute', resources=resources,
                    constantMappings=mappings, sourceConstantBytes=source_bytes, constantBytes=constant_bytes,
                    parameters=parameters, constantMode=constant_mode, threadGroupSize=groups, typedBufferMappings=substitutions, zeroOffsetTextureLoads=zero_offset_loads,
                    sourceSHA256=hashlib.sha256(original).hexdigest(), shaderSHA256=hashlib.sha256(payload).hexdigest(),
                    originalShaderEdited=False, fullLumenRenderer=False)
    if source.read_bytes() != original:
        raise RuntimeError('Input shader changed during compilation')
    (output/'shader.wcs').write_bytes(payload)
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    return manifest

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=pathlib.Path,required=True)
    parser.add_argument('--entry',required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--slang',type=pathlib.Path,default=pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
    parser.add_argument('--formats',type=pathlib.Path)
    parser.add_argument('--stage',choices=('compute','vertex','fragment'),default='compute')
    args=parser.parse_args()
    try:
        if args.stage!='compute':
            if args.formats:raise ValueError('Raster storage-image format specialization is not implemented')
            from compile_raster import compile_raster_shader
            from texture_container import build_container
            compile_raster_shader(args.source,args.entry,args.stage,args.output,args.slang,
                pathlib.Path(__file__).resolve().parents[1]/'artifacts/tint-cli/Release/tint.exe',args.slang.parent/'spirv-val.exe')
            print(json.dumps(build_container(args.output)))
        elif args.formats:
            from compile_texture_spirv import compile_texture_shader
            from texture_container import build_container
            compile_texture_shader(args.source,args.entry,json.loads(args.formats.read_text()),args.output,args.slang,
                pathlib.Path(__file__).resolve().parents[1]/'artifacts/tint-cli/Release/tint.exe',args.slang.parent/'spirv-val.exe')
            print(json.dumps(build_container(args.output)))
        else:
            print(json.dumps(compile_shader(args.source,args.entry,args.output,args.slang)))
    except Exception as error:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output/'failure.json').write_text(json.dumps(dict(entry=args.entry, source=str(args.source), error=str(error)),indent=2),encoding='utf-8')
        raise SystemExit(str(error))
