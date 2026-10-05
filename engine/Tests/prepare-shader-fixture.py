"""Compile the captured original UE pass with the cooker backend for runtime QA."""
import hashlib
import json
import pathlib
import sys
import struct

root = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root/'engine/ShaderCompiler'))
from compile_webcuda import compile_shader

capture = json.loads((root/'engine/artifacts/exported/SetupAdaptiveProbeIndirectArgsCS.json').read_text())
source = pathlib.Path(capture['source'])
assert hashlib.sha256(source.read_bytes()).hexdigest() == capture['sourceSHA256']
output = root/'engine/artifacts/webgpu'
manifest = compile_shader(source, 'SetupAdaptiveProbeIndirectArgsCS', output, pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
payload = (output/'shader.wcs').read_bytes()
(output/'WebCudaShaderFixture.h').write_text('#pragma once\nstatic const unsigned char ShaderFixture[] = {\n' + ','.join(map(str,payload)) + '\n};\n')
manifest.update(source=str(source), wgslSHA256=hashlib.sha256((output/'shader.wgsl').read_bytes()).hexdigest())
(output/'shader-manifest.json').write_text(json.dumps(manifest,indent=2))
print(f'Compiled original Lumen pass into {len(payload)} bytes of WCSH shader data')

clear=root/'engine/artifacts/shader-format/original-clear-buffer-uint.wcs'
clear_report=json.loads(pathlib.Path(str(clear)+'.json').read_text())
assert clear_report['shaderType']=='FClearReplacementCS_Buffer_Uint_Bounds' and clear_report['originalShaderConstructedAndBindingsVerified']
clear_blob=clear.read_bytes()
clear_header='#pragma once\nstatic const unsigned char ClearBufferFixture[]={'+','.join(map(str,clear_blob))+'};\nnamespace ClearBufferLayout {\n'
for parameter in clear_report['allocations']:
    clear_header+=f'constexpr unsigned {parameter["name"]}={parameter["offset"]};\n'
clear_header+='}\n'
(output/'ClearBufferFixture.h').write_text(clear_header)
(output/'clear-buffer-manifest.json').write_text(json.dumps(dict(shaderType=clear_report['shaderType'],shaderContainerSHA256=hashlib.sha256(clear_blob).hexdigest(),
    preprocessedSourceSHA256=hashlib.sha256(pathlib.Path(str(clear)+'.preprocessed.hlsl').read_bytes()).hexdigest(),originalShaderBindingsVerified=True),indent=2))

integration=root/'engine/artifacts/shader-format/spirv-format-probe/compiled'
integration_report=json.loads((integration/'manifest.json').read_text())
integration_code=(integration/'shader.wgsl').read_bytes()
assert integration_report['wgslSHA256']==hashlib.sha256(integration_code).hexdigest()
assert integration_report['entry']=='ScreenProbeIntegrateCS' and integration_report['accessPassPreservedInstructions']
container=root/'engine/artifacts/shader-format/original-lumen-integrate-v2.wcs'
binding_report=json.loads(pathlib.Path(str(container)+'.json').read_text(encoding='utf-8-sig'))
assert binding_report['originalShaderConstructedAndBindingsVerified'] and binding_report['containerVersion']==2
blob=container.read_bytes();words=struct.unpack_from('<16I',blob)
start=64+words[8]*32+words[9]*12+words[13]*4+words[14]*16
assert blob[start+words[10]:]==integration_code
integration_report.update(shaderContainerSHA256=hashlib.sha256(blob).hexdigest(),unrealResourceTableAdded=True,containerProduced=True)
(output/'LumenIntegrateFixture.h').write_text('#pragma once\nstatic const unsigned char LumenIntegrateContainer[] = {\n'+','.join(map(str,blob))+'\n};\n')
(output/'integrate-manifest.json').write_text(json.dumps(integration_report,indent=2))
layout=json.loads((integration.parent/'layout-validation.json').read_text())
assert layout['passed'] and layout['wgslSHA256']==integration_report['wgslSHA256']
header='#pragma once\nnamespace IntegrateLayout {\n'
for buffer in layout['buffers']:
    name='Root' if buffer['name']=='_RootShaderParameters' else buffer['name']
    header+=f'namespace {name} {{ constexpr unsigned Bytes={buffer["bytes"]};\n'
    for field in buffer['fields']:
        header+=f'constexpr unsigned {field["name"]}={field["offset"]};\n'
    header+='}\n'
header+='}\n'
(output/'LumenIntegrateLayout.h').write_text(header)
metadata=json.loads(pathlib.Path(str(container)+'.metadata.json').read_text())
uniforms={}
def visit(metadata):
    if metadata['shaderName']: uniforms[metadata['shaderName']]=metadata
    for member in metadata['members']:
        if 'structure' in member: visit(member['structure'])
def fields(metadata,base=0):
    for member in metadata['members']:
        if member['included']: yield from fields(member['structure'],base+member['offset'])
        else: yield member['name'],base+member['offset']
visit(metadata['root'])
table_header='#pragma once\nnamespace IntegrateTables {\n'
for name in ('SceneTexturesStruct','BlueNoise'):
    uniform=uniforms[name];indices={r['offset']:i for i,r in enumerate(uniform['resources'])}
    table_header+=f'namespace {name} {{ constexpr unsigned Hash={uniform["layoutHash"]}u,Count={len(indices)};\n'
    for name_,offset in fields(uniform):
        if offset in indices: table_header+=f'constexpr unsigned {name_}={indices[offset]};\n'
    table_header+='}\n'
table_header+='}\n'
(output/'LumenIntegrateTables.h').write_text(table_header)

# Exercise an original engine copy kernel through both parameter conventions.
# The stable-root binary comes directly from Unreal; the loose-global input is
# Unreal's retained preprocessing output before its root-parameter parser.
original = root/'engine/artifacts/shader-format/original-memcpy.wcs'
report = json.loads(pathlib.Path(str(original)+'.json').read_text(encoding='utf-8-sig'))
assert report['originalShaderConstructedAndBindingsVerified'] and report['shaderType']=='FRDGMemcpyCS'
legacy = compile_shader(pathlib.Path(str(original)+'.preprocessed.hlsl'), 'MemcpyCS', output/'legacy-memcpy', pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
assert legacy['constantMode']=='globals'
assert [(p['name'],p['offset']) for p in legacy['parameters'] if p['kind']=='LooseData']==[('Size',0),('SrcOffset',4),('DstOffset',8)]
root_blob, legacy_blob = original.read_bytes(), (output/'legacy-memcpy/shader.wcs').read_bytes()
header = '#pragma once\n'
for name, blob in [('RootMemcpyFixture',root_blob),('LegacyMemcpyFixture',legacy_blob)]:
    header += 'static const unsigned char '+name+'[] = {\n'+','.join(map(str,blob))+'\n};\n'
vectors=[]
for lanes in (2,4):
    code=root/f'engine/artifacts/shader-format/original-memcpy-uint{lanes}.wcs'
    binding=json.loads(pathlib.Path(str(code)+'.json').read_text(encoding='utf-8-sig'))
    assert binding['originalShaderConstructedAndBindingsVerified'] and binding['shaderType']=='FRDGMemcpyCS'
    blob=code.read_bytes()
    header += f'static const unsigned char Uint{lanes}MemcpyFixture[] = {{\n'+','.join(map(str,blob))+'\n};\n'
    vectors.append(dict(lanes=lanes,shaderContainerSHA256=hashlib.sha256(blob).hexdigest()))
(output/'MemcpyShaderFixtures.h').write_text(header)
(output/'vector-memcpy-manifest.json').write_text(json.dumps(vectors,indent=2))
(output/'memcpy-manifest.json').write_text(json.dumps(dict(rootSHA256=hashlib.sha256(root_blob).hexdigest(),legacySHA256=hashlib.sha256(legacy_blob).hexdigest(),originalShaderType='FRDGMemcpyCS',permutation=1,rootBindingVerified=True),indent=2))
