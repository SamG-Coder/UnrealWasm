"""Join original UE shader captures, raster lowering, and browser pipeline checks."""
import hashlib
import json
import pathlib
import struct
import sys

root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'engine/ShaderCompiler'))
from compile_raster import stage_bindings
out=root/'engine/artifacts/raster-probe'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
stages={}
for stage,name,shader,group in [('vertex','vs','FScreenVS',0),('fragment','ps','FScreenPS',1)]:
    folder=out/stage;manifest=json.loads((folder/'manifest.json').read_text())
    capture=root/f'engine/artifacts/shader-format/original-screen-{name}.wcs'
    metadata=json.loads(pathlib.Path(str(capture)+'.metadata.json').read_text())
    native=json.loads(pathlib.Path(str(capture)+'.json').read_text())
    stage_id=2 if stage=='vertex' else 3
    assert native['shaderType']==shader and native['originalEnginePreprocessing'] and native['originalShaderConstructedAndBindingsVerified']
    assert native['containerVersion']==3 and native['stage']==stage_id and native['bindGroup']==group
    container=capture.read_bytes();words=struct.unpack_from('<16I',container)
    assert words[:6]==(0x48534357,3,stage_id,0,0,0)
    code_offset=64+32*words[8]+12*words[9]+4*words[13]+16*words[14]+words[10]
    assert container[code_offset:]==(folder/'shader.wgsl').read_bytes()
    assert metadata['shaderType']==shader and metadata['entry']==manifest['entry']=='Main'
    assert manifest['sourceSHA256']==digest(pathlib.Path(str(capture)+'.preprocessed.hlsl'))
    assert manifest['wgslSHA256']==digest(folder/'shader.wgsl')
    assert manifest['compilerSHA256']==digest(root/'engine/ShaderCompiler/compile_raster.py')
    assert manifest['stage']==stage and manifest['resourceGroup']==group and not manifest['originalSourceEdited']
    expected,changes=stage_bindings((folder/'optimized.spv').read_bytes(),group)
    assert expected==(folder/'stage-bindings.spv').read_bytes() and changes==manifest['bindingRemaps']
    for name in ('slang.log','validate-original.log','optimize.log','validate-optimized.log','validate-bindings.log','tint.log'):
        assert (folder/name).read_text()==''
    stages[stage]=dict(shaderType=shader,sourceSHA256=manifest['sourceSHA256'],wgslSHA256=manifest['wgslSHA256'],resourceGroup=group,containerSHA256=digest(capture))
browsers=[]
for name in ('browser.json','browser-custom.json'):
    report=json.loads((out/name).read_text())
    assert report['passed'] and report['pipelineCreated'] and not report['pipelineError'] and not report['validationError']
    assert report['vertexSHA256']==stages['vertex']['wgslSHA256'] and report['fragmentSHA256']==stages['fragment']['wgslSHA256']
    assert not report['wasmExecuted'] and not report['drawSubmitted'] and not report['fullSceneRendered']
    browsers.append(dict(browser=report['browser'],pipelineCreated=True))
draws=[]
for name in ('draw.json','draw-custom.json'):
    report=json.loads((out/name).read_text())
    assert report['passed'] and report['wasmExecuted'] and report['drawSubmitted']
    assert not report['fullSceneRendered'] and not report['unrealRHIExecuted']
    assert report['invalidStageHeadersRejected']==10 and report['wrongPipelineStagesRejected']
    assert len(report['cases'])==45 and report['exactBytesCompared']==138240
    expected_cases={(m,r,f,0) for m in range(3) for r in range(2) for f in range(2)}|{(m,0,0,s) for m in range(3) for s in range(1,12)}
    assert {(c['mode'],c['rectangle'],c['flip'],c['state']) for c in report['cases']}==expected_cases
    assert all(c['passed'] and c['exactBytesCompared']==3072 for c in report['cases'])
    for key,file in dict(wasmSHA256='engine/artifacts/raster-probe/raster.wasm',backendSHA256='Source/WebGPURHI/Private/WebGPUDevice.cpp',
        headerSHA256='Source/WebGPURHI/Public/WebGPUDevice.h',containerHeaderSHA256='Source/WebGPURHI/Public/WebCudaShaderCode.h',
        bridgeSHA256='engine/Runtime/webgpu-wasm64.js',testSHA256='engine/Tests/raster-draw.cpp').items():
        assert report[key]==digest(root/file)
    for suffix,stage in [('vs','vertex'),('ps','fragment')]:
        for field in ('containerSHA256','wgslSHA256','sourceSHA256'):assert report['shaders'][suffix][field]==stages[stage][field]
    draws.append(dict(browser=report['browser'],cases=45,exactBytesCompared=138240))
audit=root/'engine/artifacts/webgpu-engine-resources'
for unit in json.loads((audit/'results.json').read_text()):
    assert unit['exitCode']==0 and unit['sourceSHA256']==digest(root/'Source/WebGPURHI/Private'/unit['source'])
for name,sha in json.loads((audit/'headers.json').read_text()).items():assert sha==digest(root/'Source/WebGPURHI/Public'/name)
report=dict(passed=True,stages=stages,browsers=browsers,originalSourceEdited=False,bindingRemapPreservedInstructions=True,
    unrealStageContainerProduced=True,originalShaderObjectBindingsVerified=True,wasmExecuted=True,drawSubmitted=True,
    unrealRasterFactoriesCompiled=True,unrealPipelineStateCompiled=True,unrealPipelineStateExecuted=False,
    depthStencilViewportScissorBlendCullExecuted=True,unrealRHIExecuted=False,draws=draws,fullSceneRendered=False)
(out/'validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
