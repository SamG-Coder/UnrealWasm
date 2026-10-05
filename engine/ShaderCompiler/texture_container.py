"""Build WCSH v2 from validated original-shader WGSL and compiler reflection.

Unreal's native compiler adds its resource-table entries after parameter mapping.
All buffer layouts are checked against actual WGSL. Unsupported types fail.
"""
import hashlib
import json
import re
import struct
from wgsl_layout import WGSLLayout

def build_container(output):
    manifest=json.loads((output/'manifest.json').read_text())
    reflection=json.loads((output/'reflection.json').read_text())
    stage=manifest.get('stage','compute')
    if stage not in ('compute','vertex','fragment'):raise ValueError('Unsupported stage')
    group=1 if stage=='fragment' else 0
    # Preserve the exact compiler bytes, including Windows line endings, so
    # container provenance and the retained WGSL artifact have the same hash.
    code=(output/'shader.wgsl').read_bytes().decode('utf-8');layout_reader=WGSLLayout(code)
    declarations={}
    for declared_group,binding,storage,name,typ in re.findall(r'@group\((\d+)u?\)\s*@binding\((\d+)u?\)\s*var(?:<([^>]+)>)?\s+(\w+)\s*:\s*([^;]+);',code):
        if int(declared_group)!=group:raise ValueError('Resource in wrong stage bind group')
        if name in declarations: raise ValueError('Duplicate shader resource')
        declarations[name]=(int(binding),storage,typ.strip())
    if len(declarations)!=len(re.findall(r'@binding\(',code)): raise ValueError('Unparsed shader bindings')
    resources=[];parameters=[];mappings=[];root_bytes=0;source_bytes=0;seen=set()
    for p in reflection['parameters']:
        name=p['name']
        if name not in declarations: continue # Resources removed by compiler DCE.
        seen.add(name);binding,storage,typ=declarations[name];t=p['type'];kind=t['kind']
        if p['binding'].get('space',0)!=0 or p['binding']['kind']!='descriptorTableSlot' or binding!=p['binding']['index'] or not 0<=binding<64:
            raise ValueError('Unsupported descriptor '+name)
        record=[binding,0,0,0,0,0,0,0]
        if kind=='constantBuffer':
            actual=layout_reader.type(typ)
            if storage!='uniform' or actual['size']!=t['elementVarLayout']['binding']['size']: raise ValueError('Uniform layout mismatch '+name)
            for field in t['elementType']['fields']:
                f=actual['fields'][field['name']];b=field['binding']
                if f['offset']!=b['offset'] or f['size']!=b['size']: raise ValueError('Uniform field layout mismatch '+field['name'])
                if field['name'].startswith(('BindlessSRV_','BindlessUAV_','BindlessSampler_')) and re.search(r'\.'+re.escape(field['name'])+r'\b',code):
                    raise ValueError('Live bindless descriptor slot is not supported: '+field['name'])
            record[6]=actual['size']
            if name=='_RootShaderParameters':
                if root_bytes or binding!=0: raise ValueError('Invalid root buffer')
                root_bytes=actual['size']
                for field in t['elementType']['fields']:
                    b=field['binding'];size=b['size'];offset=b['offset']
                    # Root mappings retain holes for resource pointers and
                    # unreferenced fields, but never copy them to the GPU.
                    if field['type']['kind'] not in ('scalar','vector') or size not in (4,8,12,16): raise ValueError('Unsupported root constant '+field['name'])
                    mappings.append([offset,offset,size]);source_bytes=max(source_bytes,offset+size)
                    parameters.append(dict(name=field['name'],kind='LooseData',buffer=binding,offset=offset,size=size))
            else:
                record[1]=3
                parameters.append(dict(name=name,kind='UniformBuffer',buffer=binding,offset=0,size=0))
        elif kind=='samplerState':
            if typ!='sampler' or storage: raise ValueError('Unsupported sampler '+name)
            record[1]=6;parameters.append(dict(name=name,kind='Sampler',buffer=0,offset=binding,size=1))
        elif kind=='resource' and t.get('baseShape')=='structuredBuffer':
            actual=layout_reader.blocks.get(typ,'')
            element=t['resultType'];lanes=1
            if element.get('kind')=='vector': lanes=element['elementCount'];element=element['elementType']
            if element!={'kind':'scalar','scalarType':'uint32'} or lanes not in (1,2,4): raise ValueError('Unsupported buffer element '+name)
            expected='u32' if lanes==1 else f'vec{lanes}<u32>'
            if not re.fullmatch(r'\w+:array<'+re.escape(expected)+r'>,',re.sub(r'\s+','',actual)) or storage not in ('storage, read','storage, read_write'):
                raise ValueError('Unsupported storage buffer layout '+name)
            writable=storage.endswith('read_write');record[1]=2 if writable else 1;record[6]=record[7]=lanes*4
            parameters.append(dict(name=name,kind='UAV' if writable else 'SRV',buffer=0,offset=binding,size=1))
        elif kind=='resource':
            sampled=re.fullmatch(r'texture_(2d|2d_array)<(f32|u32|i32)>',typ)
            image=re.fullmatch(r'texture_storage_(2d|2d_array)<(r8unorm|r32uint|rg11b10ufloat),\s*(read|write|read_write)>',typ)
            if sampled:
                record[1]=4;record[2]=1 if sampled[1]=='2d' else 2;record[5]={'f32':1,'u32':2,'i32':3}[sampled[2]]
                parameters.append(dict(name=name,kind='Texture',buffer=0,offset=binding,size=1))
            elif image:
                record[1]=5;record[2]=1 if image[1]=='2d' else 2;record[3]={'r8unorm':1,'r32uint':2,'rg11b10ufloat':3}[image[2]]
                record[4]={'read':1,'write':2,'read_write':3}[image[3]]
                parameters.append(dict(name=name,kind='UAV',buffer=0,offset=binding,size=1))
            else: raise ValueError('Unsupported texture '+name)
        else: raise ValueError('Unsupported parameter '+name)
        resources.append(record)
    if seen!=set(declarations) or len({r[0] for r in resources})!=len(resources): raise ValueError('Unreflected shader resource')
    entries=reflection['entryPoints']
    if len(entries)!=1 or entries[0]['name']!=manifest['entry']: raise ValueError('Unexpected entry point')
    groups=entries[0]['threadGroupSize'] if stage=='compute' else [0,0,0]
    version=2 if stage=='compute' else 3;stage_id={'compute':1,'vertex':2,'fragment':3}[stage]
    entry=manifest['wgslEntry'].encode();wgsl=code.encode()
    payload=struct.pack('<16I',0x48534357,version,stage_id,*groups,source_bytes,root_bytes,len(resources),len(mappings),len(entry),len(wgsl),0,0,0,0)
    payload+=b''.join(struct.pack('<8I',*r) for r in resources)+b''.join(struct.pack('<3I',*m) for m in mappings)+entry+wgsl
    manifest.update(version=version,stage=stage,resources=resources,parameters=parameters,constantMode='root' if root_bytes else 'none',
                    constantMappings=mappings,sourceConstantBytes=source_bytes,constantBytes=root_bytes,threadGroupSize=groups,
                    shaderSHA256=hashlib.sha256(payload).hexdigest(),containerProduced=True,unrealResourceTableAdded=False)
    (output/'shader.wcs').write_bytes(payload)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    return manifest
