"""Reject a real original Lumen compile that silently changes R8 storage.

The generated diagnostic input specializes only a resource declaration using
the PF_R8 allocation in LumenScreenProbeGather.cpp. Shader expressions and the
installed engine sources remain unchanged. This is not a playable shader export.
"""
import hashlib
import json
import pathlib
import sys

root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'engine/ShaderCompiler'))
from compile_webcuda import compile_shader

source=root/'engine/artifacts/shader-format/original-lumen-integrate.wcs.preprocessed.hlsl'
original=source.read_bytes()
text=original.decode('utf-8-sig')
declaration='RWTexture2DArray<unorm float> RWLightIsMoving;'
assert text.count(declaration)==1
lowered=text.replace(declaration,'[format("r8")] RWTexture2DArray<float> RWLightIsMoving;')
output=root/'engine/artifacts/shader-format/storage-format-audit'
output.mkdir(parents=True,exist_ok=True)
diagnostic=output/'format-specialized-original.hlsl'
diagnostic.write_text(lowered,encoding='utf-8')
try:
    compile_shader(diagnostic,'ScreenProbeIntegrateCS',output/'compile',pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
except ValueError as error:
    assert 'Slang changed a requested image format' in str(error),str(error)
    rejection=str(error)
else:
    raise AssertionError('Expected the current Slang backend to reject its format substitution')
assert not (output/'compile/shader.wcs').exists()
assert source.read_bytes()==original
report=dict(passed=True,source=str(source),sourceSHA256=hashlib.sha256(original).hexdigest(),
            compilerSHA256=hashlib.sha256((root/'engine/ShaderCompiler/compile_webcuda.py').read_bytes()).hexdigest(),
            requestedFormat='r8',rejectedFallback='rgba32float',shaderBodyChanged=False,
            successfulContainerProduced=False,fullLumenExported=False,diagnostic=rejection)
(output/'validation.json').write_text(json.dumps(report,indent=2))
print('PASS: original Lumen R8 format substitution rejected; no successful shader container produced')
