"""Verify original-shader provenance, layout and both Wasm dispatch results."""
import hashlib
import json
import pathlib
import sys
import struct
root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'engine/ShaderCompiler'))
from spirv_access import infer_write_only, instructions, DECORATE, NON_READABLE
out=root/'engine/artifacts/shader-format/spirv-format-probe'
compiled=out/'compiled'
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
manifest=json.loads((compiled/'manifest.json').read_text())
layout=json.loads((out/'layout-validation.json').read_text())
assert layout['passed'] and layout['wgslSHA256']==manifest['wgslSHA256']
assert layout['metadataSHA256']==digest(root/'engine/artifacts/shader-format/lumen-integrate-layout.wcs.metadata.json')
assert digest(pathlib.Path(manifest['source']))==manifest['sourceSHA256']
assert digest(compiled/'shader.wgsl')==manifest['wgslSHA256']
container_path=root/'engine/artifacts/shader-format/original-lumen-integrate-v2.wcs'
container=container_path.read_bytes();header=struct.unpack_from('<16I',container)
assert header[:3]==(0x48534357,2,1) and header[8]==21 and header[14]==7 and header[15]==0
original_binding=json.loads(pathlib.Path(str(container_path)+'.json').read_text(encoding='utf-8-sig'))
assert original_binding['originalShaderConstructedAndBindingsVerified'] and original_binding['containerVersion']==2 and original_binding['resourceCount']==21
assert not any(a['name'] in ['RWScreenProbeSceneDepth','RWScreenTileAdaptiveProbeHeader','RWScreenTileAdaptiveProbeIndices','RWAdaptiveScreenProbeData'] for a in original_binding['allocations'])
hash_start=64+header[8]*32+header[9]*12;table_start=hash_start+header[13]*4
hashes=struct.unpack_from('<'+str(header[13])+'I',container,hash_start)
tables=[struct.unpack_from('<4I',container,table_start+i*16) for i in range(header[14])]
metadata=json.loads(pathlib.Path(str(container_path)+'.metadata.json').read_text())
uniforms={}
def visit(node):
    if node['shaderName']: uniforms[node['shaderName']]=node
    for member in node['members']:
        if 'structure' in member:visit(member['structure'])
def offsets(node,base=0):
    result={}
    for m in node['members']:
        if m['included']:result.update(offsets(m['structure'],base+m['offset']))
        else:result[m['name']]=base+m['offset']
    return result
visit(metadata['root'])
reflection=json.loads((compiled/'reflection.json').read_text())
resource_names={p['binding']['index']:p['name'] for p in reflection['parameters']}
for slot,index,binding,kind in tables:
    name=resource_names[binding];matches=[u for u in uniforms if name.startswith(u+'_')]
    assert len(matches)==1;uniform=uniforms[matches[0]];field=name[len(matches[0])+1:]
    assert hashes[slot]==uniform['layoutHash'] and index<len(uniform['resources'])
    assert uniform['resources'][index]['offset']==offsets(uniform)[field]
    assert kind==(6 if uniform['resources'][index]['baseType']==8 else 4)
assert header[12]==sum(1<<slot for slot in {t[0] for t in tables})==original_binding['resourceTableBits']
wgsl_start=table_start+header[14]*16+header[10]
assert container[wgsl_start:]==(compiled/'shader.wgsl').read_bytes()
original=(compiled/'original.spv').read_bytes();specialized=(compiled/'write-only.spv').read_bytes()
actual,ids=infer_write_only(original)
assert actual==specialized and len(ids)==3 and ids==manifest['writeOnlyVariableIds']
assert infer_write_only(specialized)==(specialized,[])
for name in ['validate-original.log','validate-specialized.log','optimize.log','validate-optimized.log','tint.log']:
    assert (compiled/name).read_text()==''
expected=[('RWDiffuseIndirect','rg11b10ufloat',25),('RWLightIsMoving','r8unorm',26),('RWRoughSpecularIndirect','rg11b10ufloat',27)]
assert [(r['name'],r['format'],r['binding']) for r in manifest['resourceFormats']]==expected
assert all(r['access']=='write' and r['group']==0 for r in manifest['resourceFormats'])
browsers=[]
for direct,runtime in [('browser.json','validation.json'),('browser-custom.json','validation-custom.json')]:
    direct=json.loads((out/direct).read_text())
    runtime=json.loads((root/'engine/artifacts/webgpu'/runtime).read_text())
    assert direct['passed'] and direct['pipelineCreated'] and direct['sourceSHA256']==manifest['wgslSHA256']
    assert runtime['passed'] and runtime['wasmSHA256']==digest(root/'engine/artifacts/webgpu/resources.wasm')
    assert runtime['backendSHA256']==digest(root/'Source/WebGPURHI/Private/WebGPUDevice.cpp')
    assert runtime['uniformPackingSHA256']==digest(root/'Source/WebGPURHI/Public/WebGPUUniformData.h')
    assert runtime['uniformUploads']==dict(versions=2,exactBytesCompared=128,resourcePointersZeroed=True,paddingZeroed=True,invalidInputsRejected=7,unrealFactoryExecuted=False)
    assert runtime['samplers']==dict(created=3,invalidDescriptorsRejected=9,duplicateCreationRejected=True)
    pipeline=runtime['lumenIntegrationPipeline']
    assert pipeline['createdThroughWasm'] and pipeline['wgslSHA256']==manifest['wgslSHA256']
    assert pipeline['dispatched'] and pipeline['branch']=='unlit-material' and pipeline['boundResources']==21
    assert pipeline['engineProducedContainer'] and pipeline['resourceTableBindings']==7 and pipeline['layoutHashRejectionTested'] and pipeline['formatRejectionTested']
    assert pipeline['shaderContainerSHA256']==digest(container_path)
    assert pipeline['exactBytesCompared']==10752 and not pipeline['litMaterialEvaluated'] and not pipeline['fullLighting']
    browsers.append(dict(browser=runtime['browser'],nativeBrowserPipelineCreated=True,wasmPipelineCreated=True,wasmDispatch=True,exactBytesCompared=10752))
report=dict(passed=True,shader='FScreenProbeIntegrateCS',permutation=0,sourceSHA256=manifest['sourceSHA256'],
            wgslSHA256=manifest['wgslSHA256'],resourceFormats=manifest['resourceFormats'],browsers=browsers,
            accessPassPreservedInstructions=True,standardOptimization=manifest['optimization'],originalSourceEdited=False,writeOnlyAnnotations=3,dispatched=True,dispatchBranch='unlit-material',
            validatedNativeFields=sum(len(b['fields']) for b in layout['buffers']),validatedMatrices=layout['matrices'],validatedArrays=layout['arrays'],
            litMaterialEvaluated=False,integratedIntoCookedShaderContainer=True,containerVersion=2,shaderContainerSHA256=digest(container_path),
            originalShaderObjectBindingsVerified=True,unrealResourceTableBindings=len(tables),resourceLayoutHashesVerified=True,fullSceneRendered=False)
(out/'pipeline-validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
