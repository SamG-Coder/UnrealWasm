"""Embed original, Unreal-validated screen shader containers for C++ runtime QA."""
import hashlib
import json
import pathlib
import struct

root=pathlib.Path(__file__).resolve().parents[2]
out=root/'engine/artifacts/raster-probe'
header='#pragma once\n'
manifest={}
for suffix,kind,stage,group in [('vs','FScreenVS',2,0),('ps','FScreenPS',3,1)]:
    path=root/f'engine/artifacts/shader-format/original-screen-{suffix}.wcs'
    report=json.loads(pathlib.Path(str(path)+'.json').read_text())
    assert report['shaderType']==kind and report['originalEnginePreprocessing'] and report['originalShaderConstructedAndBindingsVerified']
    assert report['containerVersion']==3 and report['stage']==stage and report['bindGroup']==group
    data=path.read_bytes();words=struct.unpack_from('<16I',data)
    assert words[:6]==(0x48534357,3,stage,0,0,0)
    offset=64+32*words[8]+12*words[9]+4*words[13]+16*words[14]
    wgsl=data[offset+words[10]:]
    assert len(wgsl)==words[11]
    assert report['allocations']==([dict(name='DrawRectangleParameters',buffer=0,offset=0,size=0)] if stage==2 else
        [dict(name='InTexture',buffer=0,offset=0,size=1),dict(name='InTextureSampler',buffer=0,offset=1,size=1)])
    header+=f'static const unsigned char Screen_{suffix}[]={{'+','.join(map(str,data))+'};\n'
    manifest[suffix]=dict(shaderType=kind,stage=stage,bindGroup=group,containerSHA256=hashlib.sha256(data).hexdigest(),
        sourceSHA256=hashlib.sha256(pathlib.Path(str(path)+'.preprocessed.hlsl').read_bytes()).hexdigest(),wgslSHA256=hashlib.sha256(wgsl).hexdigest())
(out/'ScreenFixtures.h').write_text(header)
(out/'draw-shaders.json').write_text(json.dumps(manifest,indent=2))
print('Original UE vertex/pixel containers verified and embedded')
