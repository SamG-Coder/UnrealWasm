"""Strict layout reader for the host-shareable WGSL types emitted by Tint."""
import re

def align(value,boundary): return (value+boundary-1)//boundary*boundary

class WGSLLayout:
    def __init__(self,code):
        self.blocks=dict(re.findall(r'\bstruct\s+(\w+)\s*\{([^}]+)\}',code))
        self.cache={}
    def type(self,name):
        name=re.sub(r'\s+','',name)
        if name in ('f32','u32','i32'): return dict(align=4,size=4,scalar=name)
        vector=re.fullmatch(r'vec([234])<(f32|u32|i32)>',name)
        if vector:
            count=int(vector[1]);return dict(align=8 if count==2 else 16,size=count*4,scalar=vector[2],columns=count)
        matrix=re.fullmatch(r'mat([234])x([234])<f32>',name)
        if matrix:
            columns,rows=map(int,matrix.groups());v=self.type(f'vec{rows}<f32>')
            return dict(align=v['align'],size=columns*align(v['size'],v['align']),matrixStride=align(v['size'],v['align']),rows=rows,columns=columns)
        array=re.fullmatch(r'array<(.+),(\d+)u?>',name)
        if array:
            element=self.type(array[1]);stride=align(element['size'],element['align'])
            return dict(align=element['align'],size=stride*int(array[2]),stride=stride,elements=int(array[2]),element=element)
        if name in self.cache: return self.cache[name]
        if name not in self.blocks: raise ValueError('Unsupported WGSL host layout '+name)
        fields={};offset=0;alignment=1;body=self.blocks[name]
        pattern=re.compile(r'\s*((?:@\w+\(\d+u?\)\s*)*)(\w+)\s*:\s*([^\n]+?),\s*(?=\n|$)')
        cursor=0
        for match in pattern.finditer(body):
            assert not body[cursor:match.start()].strip(), 'Unparsed WGSL member'
            cursor=match.end();attributes=dict(re.findall(r'@(\w+)\((\d+)u?\)',match[1]))
            assert set(attributes)<= {'align','size'}
            layout=self.type(match[3]);member_align=int(attributes.get('align',layout['align']))
            member_size=int(attributes.get('size',layout['size']))
            assert member_align>=layout['align'] and member_align&(member_align-1)==0 and member_size>=layout['size']
            offset=align(offset,member_align)
            fields[match[2]]=dict(layout,offset=offset,occupiedBytes=member_size)
            offset+=member_size;alignment=max(alignment,member_align)
        assert not body[cursor:].strip() and fields
        result=dict(align=alignment,size=align(offset,alignment),fields=fields)
        self.cache[name]=result
        return result
