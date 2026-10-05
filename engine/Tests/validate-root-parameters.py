"""Join original-engine binding evidence with the actual Wasm execution hashes."""
import hashlib
import json
import pathlib

root=pathlib.Path(__file__).resolve().parents[2]
out=root/'engine/artifacts/shader-format'
runtime=json.loads((root/'engine/artifacts/webgpu/validation.json').read_text())
assert runtime['passed']
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def original(name,kind,root_size,expected_hash):
    code=out/(name+'.wcs')
    report=json.loads(pathlib.Path(str(code)+'.json').read_text(encoding='utf-8-sig'))
    assert report['shaderType']==kind
    assert report['originalEnginePreprocessing'] and report['stableRootParameters'] and report['originalShaderConstructedAndBindingsVerified']
    roots=[p for p in report['allocations'] if p['name']=='_RootShaderParameters']
    assert roots==[dict(name='_RootShaderParameters',buffer=0,offset=0,size=root_size)]
    assert digest(code)==expected_hash
    return dict(shaderType=kind,containerSHA256=expected_hash,rootUploadBytes=root_size,originalShaderBindingsVerified=True)

lumen=original('original-lumen','FSetupAdaptiveProbeIndirectArgsCS',124,runtime['lumen']['shaderContainerSHA256'])
copy=original('original-memcpy','FRDGMemcpyCS',16,runtime['rootAndGlobalConstants']['rootSHA256'])
vectors=[original(f'original-memcpy-uint{shader["lanes"]}','FRDGMemcpyCS',16,shader['shaderContainerSHA256']) for shader in runtime['vectorBuffers']['shaders']]
legacy=json.loads((out/'legacy-native.wcs.json').read_text(encoding='utf-8-sig'))
assert not legacy['stableRootParameters']
for name,offset in [('Size',0),('SrcOffset',4),('DstOffset',8)]:
    assert dict(name=name,buffer=0,offset=offset,size=4) in legacy['allocations']
assert digest(out/'legacy-native.wcs')==runtime['rootAndGlobalConstants']['legacySHA256']
report=dict(passed=True,lumen=lumen,copy=copy,vectors=vectors,legacyGlobalBindingsVerified=True,
            lumenCases=runtime['lumen']['cases'],lumenExactUintComparisons=runtime['lumen']['exactUintComparisons'],
            rootAndLegacyCases=runtime['rootAndGlobalConstants']['cases'],copyExactUintComparisons=runtime['rootAndGlobalConstants']['exactUintComparisons'],
            vectorCases=runtime['vectorBuffers']['cases'],vectorExactUintComparisons=runtime['vectorBuffers']['exactUintComparisons'],
            wasmSHA256=runtime['wasmSHA256'],fullSceneCooked=False,fullSceneRendered=False)
(out/'root-parameter-validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
