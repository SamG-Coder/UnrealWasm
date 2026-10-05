"""Conservatively infer write-only storage images without changing shader code.

Only direct variable -> OpLoad -> OpImageWrite use chains qualify. Any unknown
use leaves access unchanged. This pass adds NonReadable decorations; it never
changes instructions, formats, calculations, bindings or texture coordinates.
"""
import struct

# SPIR-V core opcodes and decoration values, from spirv.core.grammar.json.
NAME, MEMBER_NAME, ENTRY_POINT, DECORATE, MEMBER_DECORATE = 5, 6, 15, 71, 72
TYPE_IMAGE, TYPE_POINTER, VARIABLE, LOAD, IMAGE_WRITE = 25, 32, 59, 61, 99
NON_READABLE = 25

def instructions(binary):
    if len(binary)<20 or len(binary)%4:
        raise ValueError('Invalid SPIR-V byte length')
    words=list(struct.unpack('<'+str(len(binary)//4)+'I',binary))
    if words[0]!=0x07230203:
        raise ValueError('Invalid SPIR-V magic')
    result=[];index=5
    while index<len(words):
        count=words[index]>>16;opcode=words[index]&65535
        if not count or index+count>len(words):
            raise ValueError('Truncated SPIR-V instruction')
        result.append((opcode,words[index+1:index+count]))
        index+=count
    return words[:5],result

def infer_write_only(binary):
    header,ops=instructions(binary)
    image_types={a[0] for op,a in ops if op==TYPE_IMAGE and len(a)>=8 and a[6]==2}
    pointers={a[0] for op,a in ops if op==TYPE_POINTER and len(a)==3 and a[1]==0 and a[2] in image_types}
    variables={a[1] for op,a in ops if op==VARIABLE and len(a)>=3 and a[0] in pointers and a[2]==0}
    decorated={a[0] for op,a in ops if op==DECORATE and len(a)>=2 and a[1]==NON_READABLE}
    qualified=[]
    for variable in sorted(variables-decorated):
        loads=[];safe=True
        for opcode,args in ops:
            if variable not in args: continue
            if opcode in (NAME,MEMBER_NAME,DECORATE,MEMBER_DECORATE,ENTRY_POINT): continue
            if opcode==VARIABLE and len(args)>=3 and args[1]==variable: continue
            if opcode==LOAD and len(args)>=3 and args[2]==variable:
                loads.append(args[1]);continue
            safe=False;break
        if not safe or not loads: continue
        for loaded in loads:
            writes=0
            for opcode,args in ops:
                if loaded not in args: continue
                if opcode in (NAME,MEMBER_NAME,DECORATE,MEMBER_DECORATE): continue
                if opcode==LOAD and len(args)>=3 and args[1]==loaded: continue
                if opcode==IMAGE_WRITE and len(args)>=3 and args[0]==loaded:
                    writes+=1;continue
                safe=False;break
            if not safe or not writes:
                safe=False;break
        if safe: qualified.append(variable)
    words=header[:];inserted=False
    for opcode,args in ops:
        # Annotations precede all type declarations in the logical layout.
        if not inserted and 19<=opcode<=39:
            for variable in qualified: words.extend([(3<<16)|DECORATE,variable,NON_READABLE])
            inserted=True
        words.extend([((1+len(args))<<16)|opcode,*args])
    if qualified and not inserted: raise ValueError('Missing SPIR-V type section')
    return struct.pack('<'+str(len(words))+'I',*words),qualified
