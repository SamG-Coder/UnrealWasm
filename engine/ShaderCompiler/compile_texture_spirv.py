"""Compile format-specialized original compute HLSL via SPIR-V and Tint.

This backend emits validated WGSL and reflection, not a WCSH runtime container.
The caller must supply the actual resource-view formats; HLSL alone cannot tell
whether a float texture is R8, half-float or full-float storage.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
from spirv_access import infer_write_only, instructions, DECORATE, NON_READABLE

FORMATS={'r8':({'float'},True,'r8unorm'),
         'r11f_g11f_b10f':({'float3'},False,'rg11b10ufloat')}

def compile_texture_shader(source,entry,formats,output,slang,tint,validator):
    if not re.fullmatch(r'[A-Za-z_]\w*',entry): raise ValueError('Invalid entry identifier')
    output.mkdir(parents=True,exist_ok=True)
    original=source.read_bytes();text=original.decode('utf-8-sig')
    for name,storage_format in formats.items():
        if not re.fullmatch(r'[A-Za-z_]\w*',name) or storage_format not in FORMATS:
            raise ValueError('Unsupported texture format specialization: '+name)
        pattern=re.compile(r'(?m)^([ \t]*)(RWTexture(?:1D|2D|3D)(?:Array)?)\s*<\s*(unorm\s+)?(float[234]?)\s*>\s+'+re.escape(name)+r'\s*;')
        matches=list(pattern.finditer(text))
        if len(matches)!=1: raise ValueError('Expected one global texture declaration for '+name)
        match=matches[0];types,normalized,_=FORMATS[storage_format]
        if match[4] not in types or (match[3] and not normalized):
            raise ValueError('Texture element type and storage format differ for '+name)
        replacement=f'{match[1]}[format("{storage_format}")] {match[2]}<{match[4]}> {name};'
        text=text[:match.start()]+replacement+text[match.end():]
    lowered=output/'format-specialized.hlsl';lowered.write_text(text,encoding='utf-8')
    def run(command,name):
        process=subprocess.run([str(x) for x in command],stdin=subprocess.DEVNULL,capture_output=True,text=True)
        (output/name).write_text(process.stdout+process.stderr,encoding='utf-8')
        if process.returncode: raise ValueError(f'{name}: '+process.stdout+process.stderr)
    # Unreal's D3D compiler uses -Zpr: uniform matrices share the CPU row layout.
    # Make this explicit instead of depending on Slang's command-line default.
    run([slang,'-lang','hlsl',lowered,'-entry',entry,'-stage','compute','-target','spirv','-profile','spirv_1_3','-matrix-layout-row-major',
         '-reflection-json',output/'reflection.json','-o',output/'original.spv'],'slang.log')
    run([validator,'--target-env','vulkan1.1',output/'original.spv'],'validate-original.log')
    binary=(output/'original.spv').read_bytes();specialized,ids=infer_write_only(binary)
    # Every original instruction must survive exactly; only access annotations
    # may be added. Re-running the pass must be idempotent.
    before_header,before=instructions(binary);after_header,after=instructions(specialized)
    added=[(DECORATE,[identifier,NON_READABLE]) for identifier in ids]
    assert before_header==after_header
    assert [op for op in after if op not in added]==before
    assert infer_write_only(specialized)==(specialized,[])
    (output/'write-only.spv').write_bytes(specialized)
    run([validator,'--target-env','vulkan1.1',output/'write-only.spv'],'validate-specialized.log')
    # Slang can retain resources in unreachable branches of included helpers.
    # Unreal's shader-parameter struct deliberately has no bindings for these.
    # Use the standard optimizer, never remove resources by name or alter HLSL.
    optimizer=validator.parent/'spirv-opt.exe'
    run([optimizer,'-O',output/'write-only.spv','-o',output/'optimized.spv'],'optimize.log')
    run([validator,'--target-env','vulkan1.1',output/'optimized.spv'],'validate-optimized.log')
    run([tint,output/'optimized.spv','--format','wgsl','-o',output/'shader.wgsl'],'tint.log')
    code=(output/'shader.wgsl').read_text()
    resources=[]
    for name,fmt in formats.items():
        wanted=FORMATS[fmt][2]
        declaration=re.search(r'@group\((\d+)u?\)\s*@binding\((\d+)u?\)\s*var '+re.escape(name)+r'\s*:\s*texture_storage_\w+<'+wanted+r',\s*(\w+)>;',code)
        if not declaration: raise ValueError('Generated WGSL did not retain the requested format for '+name)
        resources.append(dict(name=name,format=wanted,group=int(declaration[1]),binding=int(declaration[2]),access=declaration[3]))
    if source.read_bytes()!=original: raise RuntimeError('Original shader input changed')
    manifest=dict(entry=entry,wgslEntry='main',source=str(source.resolve()),sourceSHA256=hashlib.sha256(original).hexdigest(),
                  wgslSHA256=hashlib.sha256((output/'shader.wgsl').read_bytes()).hexdigest(),
                  slangSHA256=hashlib.sha256(slang.read_bytes()).hexdigest(),tintSHA256=hashlib.sha256(tint.read_bytes()).hexdigest(),
                  optimizerSHA256=hashlib.sha256(optimizer.read_bytes()).hexdigest(),optimization='spirv-opt -O',
                  resourceFormats=resources,writeOnlyVariableIds=ids,accessPassPreservedInstructions=True,originalInstructionsPreserved=False,matrixLayout='row-major',
                  originalSourceEdited=False,containerProduced=False,fullLumenExecuted=False)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    return manifest

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for name in ['source','formats','output']: parser.add_argument('--'+name,type=pathlib.Path,required=True)
    parser.add_argument('--entry',required=True)
    parser.add_argument('--slang',type=pathlib.Path,default=pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
    parser.add_argument('--validator',type=pathlib.Path,default=pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/spirv-val.exe'))
    parser.add_argument('--tint',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]/'artifacts/tint-cli/Release/tint.exe')
    args=parser.parse_args()
    try:
        print(json.dumps(compile_texture_shader(args.source,args.entry,json.loads(args.formats.read_text()),args.output,args.slang,args.tint,args.validator),indent=2))
    except Exception as error:
        args.output.mkdir(parents=True,exist_ok=True)
        (args.output/'failure.json').write_text(json.dumps(dict(error=str(error)),indent=2))
        raise SystemExit(str(error))
