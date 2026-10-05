"""Lower original UE-preprocessed vertex/pixel HLSL without changing its sources.

Vertex resources occupy bind group 0 and pixel resources group 1. This preserves
Unreal's stage-local slot numbers while avoiding cross-stage binding collisions.
The engine shader-container and graphics-pipeline integration remain separate.
"""
import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
from spirv_access import instructions, DECORATE

DESCRIPTOR_SET=34

def stage_bindings(binary,group):
    header,ops=instructions(binary);out=[];changes=[]
    for opcode,args in ops:
        args=list(args)
        if opcode==DECORATE and len(args)>=2 and args[1]==DESCRIPTOR_SET:
            if len(args)!=3 or args[2]!=0:raise ValueError('Raster input must use stage-local descriptor set zero')
            if group:
                changes.append(dict(variable=args[0],originalGroup=0,group=group));args[2]=group
        out.append((opcode,args))
    words=list(header)
    for opcode,args in out:words.extend([((len(args)+1)<<16)|opcode,*args])
    result=struct.pack('<'+str(len(words))+'I',*words)
    # The only edits are descriptor-set operands; every executable instruction
    # and every other annotation must remain byte-for-byte equivalent.
    def body(sequence):return [(op,a) for op,a in sequence if not(op==DECORATE and len(a)>1 and a[1]==DESCRIPTOR_SET)]
    assert body(instructions(result)[1])==body(ops)
    return result,changes

def compile_raster_shader(source,entry,stage,output,slang,tint,validator):
    if stage not in ('vertex','fragment') or not re.fullmatch(r'[A-Za-z_]\w*',entry):raise ValueError('Invalid raster stage or entry')
    output.mkdir(parents=True,exist_ok=True);original=source.read_bytes();commands=[]
    def run(command,name):
        command=list(map(str,command));commands.append(command)
        result=subprocess.run(command,stdin=subprocess.DEVNULL,capture_output=True,text=True)
        (output/name).write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode:raise ValueError(name+': '+result.stdout+result.stderr)
    run([slang,'-lang','hlsl',source,'-entry',entry,'-stage',stage,'-target','spirv','-profile','spirv_1_3','-matrix-layout-row-major',
         '-reflection-json',output/'reflection.json','-o',output/'original.spv'],'slang.log')
    run([validator,'--target-env','vulkan1.1',output/'original.spv'],'validate-original.log')
    optimizer=validator.parent/'spirv-opt.exe'
    run([optimizer,'-O',output/'original.spv','-o',output/'optimized.spv'],'optimize.log')
    run([validator,'--target-env','vulkan1.1',output/'optimized.spv'],'validate-optimized.log')
    group=0 if stage=='vertex' else 1
    binary,changes=stage_bindings((output/'optimized.spv').read_bytes(),group)
    (output/'stage-bindings.spv').write_bytes(binary)
    run([validator,'--target-env','vulkan1.1',output/'stage-bindings.spv'],'validate-bindings.log')
    run([tint,output/'stage-bindings.spv','--format','wgsl','-o',output/'shader.wgsl'],'tint.log')
    code=(output/'shader.wgsl').read_text()
    if '@'+stage not in code:raise ValueError('Output shader stage mismatch')
    groups=[int(x) for x in re.findall(r'@group\((\d+)u?\)',code)]
    if any(x!=group for x in groups):raise ValueError('Unexpected output bind group')
    if source.read_bytes()!=original:raise ValueError('Original shader source changed')
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report=dict(stage=stage,entry=entry,wgslEntry='main',source=str(source.resolve()),sourceSHA256=hashlib.sha256(original).hexdigest(),
        wgslSHA256=digest(output/'shader.wgsl'),resourceGroup=group,bindingRemaps=changes,bindingRemapPreservedInstructions=True,
        originalSourceEdited=False,optimization='spirv-opt -O',matrixLayout='row-major',commands=commands,
        compilerSHA256=digest(pathlib.Path(__file__)),slangSHA256=digest(slang),tintSHA256=digest(tint),optimizerSHA256=digest(optimizer),
        containerProduced=False,runtimePipelineCreated=False)
    (output/'manifest.json').write_text(json.dumps(report,indent=2))
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for name in ('source','output'):parser.add_argument('--'+name,type=pathlib.Path,required=True)
    parser.add_argument('--entry',required=True);parser.add_argument('--stage',choices=('vertex','fragment'),required=True)
    parser.add_argument('--slang',type=pathlib.Path,default=pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
    parser.add_argument('--validator',type=pathlib.Path,default=pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/spirv-val.exe'))
    parser.add_argument('--tint',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]/'artifacts/tint-cli/Release/tint.exe')
    args=parser.parse_args()
    try:
        result=compile_raster_shader(args.source,args.entry,args.stage,args.output,args.slang,args.tint,args.validator)
        print(json.dumps({key:result[key] for key in ('stage','sourceSHA256','wgslSHA256','resourceGroup','containerProduced')},indent=2))
    except Exception as error:
        args.output.mkdir(parents=True,exist_ok=True);(args.output/'failure.json').write_text(json.dumps(dict(error=str(error))))
        raise SystemExit(str(error))
