"""Join actual UE wrapper compilation and both browsers' texture-storage checks."""
import hashlib
import json
import pathlib

root=pathlib.Path(__file__).resolve().parents[2]
out=root/'engine/artifacts/webgpu-engine-resources'
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
units=json.loads((out/'results.json').read_text())
assert len(units)==2 and all(item['exitCode']==0 for item in units)
for item in units:
    assert item['sourceSHA256']==digest(root/'Source/WebGPURHI/Private'/item['source'])
headers=json.loads((out/'headers.json').read_text())
for name,sha in headers.items(): assert sha==digest(root/'Source/WebGPURHI/Public'/name)
browsers=[]
for name in ['validation.json','validation-custom.json']:
    runtime=json.loads((root/'engine/artifacts/webgpu'/name).read_text())
    assert runtime['passed']
    assert runtime['wasmSHA256']==digest(root/'engine/artifacts/webgpu/resources.wasm')
    assert runtime['backendSHA256']==digest(root/'Source/WebGPURHI/Private/WebGPUDevice.cpp')
    assert runtime['bridgeSHA256']==digest(root/'engine/Runtime/webgpu-wasm64.js')
    assert runtime['debugGroups']==dict(nestedGroups=2,depthLimitChecked=128,underflowRejected=True,invalidLabelRejected=True,gpuTimestamps=False)
    assert runtime['textureClears']==dict(cases=324,exactBytesCompared=1990656,formats=6,integerHighBitsTested=True,unrealContextExecuted=False)
    indirect=runtime['indirectDispatch']
    assert indirect==dict(cases=21,exactUintComparisons=5376,gpuProducedArguments=True,cpuArgumentReadback=False,zeroWorkgroupsTested=True,invalidArgumentRangesRejected=4)
    clear=runtime['bufferClear']
    original=root/'engine/artifacts/shader-format/original-clear-buffer-uint.wcs'
    original_report=json.loads(pathlib.Path(str(original)+'.json').read_text())
    assert original_report['originalEnginePreprocessing'] and original_report['originalShaderConstructedAndBindingsVerified']
    assert original_report['shaderType']==clear['shaderType']=='FClearReplacementCS_Buffer_Uint_Bounds'
    assert clear['shaderContainerSHA256']==digest(original)
    assert clear['preprocessedSourceSHA256']==digest(pathlib.Path(str(original)+'.preprocessed.hlsl'))
    assert clear['cases']==72 and clear['exactUintComparisons']==27648 and clear['subviewOffsetBytes']==256 and not clear['unrealContextExecuted']
    textures=runtime['textures']
    assert textures['textureFormatsTier1'] and textures['rg11b10Renderable']
    assert len(textures['cases'])==54 and textures['exactBytesCompared']==73728
    assert all(case['passed'] for case in textures['cases'])
    assert {case['format'] for case in textures['cases']}=={'r8unorm','rgba8unorm','r32uint','rgba16float','rgba32float','rg11b10ufloat'}
    browsers.append(dict(browser=runtime['browser'],cases=54,exactBytesCompared=73728,textureFormatsTier1=True))
report=dict(passed=True,wasmSHA256=digest(root/'engine/artifacts/webgpu/resources.wasm'),
            engineResourceFactoriesCompiled=True,browserBackendExecuted=True,unrealResourceFactoriesExecuted=False,
            computeParameterAdapterCompiled=True,computeParameterAdapterExecuted=False,
            concreteComputeContextCompiled=True,computeContextExecuted=False,transitionFactoryCompiled=True,transitionFactoryExecuted=False,
            debugGroupsExecuted=True,indirectDispatchCasesPerBrowser=21,
            textureClearCasesPerBrowser=324,textureClearExactBytesPerBrowser=1990656,
            originalBufferClearCasesPerBrowser=72,
            shaderTextureWritesTested=False,fullSceneRendered=False,browsers=browsers)
(out/'validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
