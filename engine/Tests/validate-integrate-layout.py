"""Compare UE's original parameter metadata, Slang reflection and emitted WGSL.

This validates static layout, not resource binding, uploads or Lumen execution.
Reject unknown layouts instead of inferring compatibility from total sizes.
"""
import hashlib
import json
import pathlib
import re
import struct
import sys

ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'engine/artifacts/shader-format'
COMPILED=OUT/'spirv-format-probe/compiled'
sys.path.insert(0,str(ROOT/'engine/ShaderCompiler'))
from spirv_access import instructions
from wgsl_layout import WGSLLayout

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def align(value,boundary): return (value+boundary-1)//boundary*boundary

def flatten(metadata,prefix='',base=0):
    fields={}
    for member in metadata['members']:
        name=prefix+member['name'];offset=base+member['offset']
        if member['included'] or member['nested']:
            assert not member['elements'], 'Struct arrays need explicit expansion'
            fields.update(flatten(member['structure'],prefix if member['included'] else name+'_',offset))
        else:
            assert name not in fields
            fields[name]=dict(member,offset=offset)
    return fields


def main():
    metadata_path=OUT/'lumen-integrate-layout.wcs.metadata.json'
    metadata=json.loads(metadata_path.read_text())
    reflection=json.loads((COMPILED/'reflection.json').read_text())
    manifest=json.loads((COMPILED/'manifest.json').read_text())
    assert digest(pathlib.Path(manifest['source']))==manifest['sourceSHA256']
    code=(COMPILED/'shader.wgsl').read_text();layouts=WGSLLayout(code)
    assert metadata['shaderType']=='FScreenProbeIntegrateCS' and metadata['permutation']==0
    assert digest(COMPILED/'shader.wgsl')==manifest['wgslSHA256']
    # Match the metadata's original preprocessing to the root-parser job used
    # for this compiler input, so metadata from another permutation cannot pass.
    assert digest(OUT/'lumen-integrate-layout.wcs.preprocessed.hlsl')==digest(OUT/'original-lumen-integrate.wcs.preprocessed.hlsl')
    declarations={name:(int(binding),typ) for binding,name,typ in re.findall(r'@group\(0u?\)\s*@binding\((\d+)u?\)\s*var<uniform>\s+(\w+)\s*:\s*(\w+);',code)}
    ue={'_RootShaderParameters':metadata['root'],**{m['shaderName']:m for m in metadata['uniformBuffers']}}
    reports=[];matrix_count=0;array_count=0
    for parameter in reflection['parameters']:
        if parameter['type']['kind']!='constantBuffer': continue
        name=parameter['name'];binding,typ=declarations[name]
        layout=layouts.type(typ);original=flatten(ue[name],'' if name=='_RootShaderParameters' else name+'_')
        assert binding==parameter['binding']['index']
        assert layout['size']==parameter['type']['elementVarLayout']['binding']['size']
        if name!='_RootShaderParameters': assert layout['size']==ue[name]['size']
        checked=[];placeholders=[];padding=[]
        for field in parameter['type']['elementType']['fields']:
            key=field['name'];emitted=layout['fields'][key];reflected=field['binding']
            assert emitted['offset']==reflected['offset'], (key,'WGSL offset')
            assert emitted['size']==reflected['size'], (key,'WGSL size',emitted['size'],reflected['size'])
            if re.fullmatch(re.escape(name)+r'_Padding\d+',key):
                # Padding may occupy the high half of an unused resource pointer.
                padding.append(key);continue
            if key.startswith(('BindlessSRV_','BindlessUAV_','BindlessSampler_')):
                resource_name=key.split('_',1)[1]
                assert resource_name in original and not original[resource_name]['native']
                assert original[resource_name]['offset']==reflected['offset']
                # The generated struct may contain slots for a disabled bindless
                # path. Their use would require descriptor-index lowering.
                assert not re.search(r'\.'+re.escape(key)+r'\b',code), 'Live bindless slot: '+key
                placeholders.append(key);continue
            member=original[key]
            assert member['native'] and member['offset']==reflected['offset'], (key,'UE offset')
            assert member['size']==reflected['size'], (key,'UE size',member['size'],reflected['size'])
            if field['type']['kind']=='array':
                array_count+=1
                # Tint can wrap a fixed array in a one-member struct.
                actual=emitted
                if 'fields' in actual:
                    assert list(actual['fields'])==['data'];actual=actual['fields']['data']
                assert actual['elements']==member['elements']
                assert actual['stride']==reflected['elementStride']
            if field['type']['kind']=='matrix':
                matrix_count+=1
                assert member['rows']==emitted['rows']==field['type']['rowCount']
                assert member['columns']==emitted['columns']==field['type']['columnCount']
                assert emitted['matrixStride']==16
            checked.append(dict(name=key,offset=member['offset'],bytes=member['size']))
        reports.append(dict(name=name,binding=binding,bytes=layout['size'],fields=checked,unusedBindlessSlots=placeholders,paddingFields=len(padding)))
    # Unreal's D3D backend selects -Zpr. Slang transposes its HLSL matrix
    # representation for SPIR-V: HLSL row-major becomes SPIR-V ColMajor, with
    # corresponding multiplication lowering. Decoration names alone therefore
    # cannot select the HLSL layout. Require the explicit compiler setting too.
    _,ops=instructions((COMPILED/'optimized.spv').read_bytes())
    names={args[0]:struct.pack('<'+'I'*len(args[1:]),*args[1:]).split(b'\0')[0].decode() for op,args in ops if op==5}
    view_ids={id_ for id_,name in names.items() if name.startswith('SLANG_ParameterGroup_View')}
    row_major={(a[0],a[1]) for op,a in ops if op==72 and a[0] in view_ids and a[2]==4}
    column_major={(a[0],a[1]) for op,a in ops if op==72 and a[0] in view_ids and a[2]==5}
    assert manifest['matrixLayout']=='row-major'
    assert len(column_major)==matrix_count and not row_major
    assert 'transpose(View.View_TranslatedWorldToClip)' not in code
    return dict(passed=True,shaderType=metadata['shaderType'],permutation=0,metadataSHA256=digest(metadata_path),wgslSHA256=manifest['wgslSHA256'],
                sourceSHA256=manifest['sourceSHA256'],buffers=reports,matrices=matrix_count,arrays=array_count,
                matrixStorage='row-major',uploadsExecuted=False,resourcesBound=False,fullSceneRendered=False)

if __name__=='__main__':
    result=main()
    (OUT/'spirv-format-probe/layout-validation.json').write_text(json.dumps(result,indent=2))
    print(json.dumps({k:v for k,v in result.items() if k!='buffers'},indent=2))
    for buffer in result['buffers']: print(buffer['name'],buffer['bytes'],'bytes,',len(buffer['fields']),'native fields,',len(buffer['unusedBindlessSlots']),'unused bindless slots')
