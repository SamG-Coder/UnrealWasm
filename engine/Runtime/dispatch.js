// Browser side of the Wasm64 command ABI. GPU work is compiled Unreal code;
// this module only binds resources, snapshots CPU constants and submits work.
export class WebCudaDispatchQueue {
 constructor(runtime,memory){
  if(!runtime.native)throw Error('WebCuda requires the native CUDA browser backend');
  this.runtime=runtime;this.memory=memory;this.shaders=new Map();this.resources=new Map();this.nextResource=1;
 }
 async registerShader(id,source,manifest){
  if(this.shaders.has(id))throw Error('Shader ID already registered');
  const hash=[...new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(source)))].map(x=>x.toString(16).padStart(2,'0')).join('');
  if(hash!==manifest.cudaSHA256)throw Error('CUDA artifact hash mismatch');
  if(manifest.profile!=='uint-buffer-compute'||manifest.schema!==1)throw Error('Unsupported shader ABI');
  const kernel=await this.runtime.native.kernel(source,{entry:manifest.entry,workgroupSize:manifest.workgroupSize,parameters:manifest.parameters.map(({name,type})=>({name,type}))});
  this.shaders.set(id,{kernel,manifest});
 }
 registerBuffer(buffer){const id=this.nextResource++;this.resources.set(id,buffer);return id;}
 unregisterBuffer(id){this.resources.delete(id);}
 bytes(pointer,length){
  if(typeof pointer==='bigint'&&(pointer<0n||pointer>BigInt(Number.MAX_SAFE_INTEGER)))throw RangeError('Invalid Wasm pointer');
  const start=Number(pointer);
  if(!Number.isSafeInteger(start)||start<0||!Number.isSafeInteger(length)||length<0||start+length>this.memory.buffer.byteLength)throw RangeError('Wasm memory range is out of bounds');
  return new Uint8Array(this.memory.buffer,start,length);
 }
 async submit(pointer){
  // Read the entire packet before the first await; memory growth or a later
  // render-thread command must not change an in-flight submission's bindings.
  const packet=this.bytes(pointer,48),view=new DataView(packet.buffer,packet.byteOffset,48);
  if(view.getUint32(0,true)!==1)throw Error('Unsupported dispatch packet version');
  const shader=this.shaders.get(view.getUint32(4,true));if(!shader)throw Error('Unknown shader');
  const groups=[8,12,16].map(o=>view.getUint32(o,true));
  if(groups.some(n=>!n))throw RangeError('Dispatch dimensions must be nonzero');
  const constantSize=view.getUint32(20,true),constantPointer=view.getBigUint64(24,true);
  const resourceCount=view.getUint32(32,true),resourcePointer=view.getBigUint64(40,true);
  const constants=this.bytes(constantPointer,constantSize).slice();
  const resourceBytes=this.bytes(resourcePointer,resourceCount*8).slice(),resourceView=new DataView(resourceBytes.buffer);
  const buffers=shader.manifest.parameters.filter(p=>p.kind==='structuredBuffer');
  const cbs=shader.manifest.parameters.filter(p=>p.kind==='constantBuffer');
  if(cbs.length!==1||resourceCount!==buffers.length||constantSize!==cbs[0].bytes)throw Error('Packet does not match shader layout');
  const bindings={},scalars={};
  buffers.forEach((p,i)=>{
   const id=resourceView.getUint32(i*8,true),count=resourceView.getUint32(i*8+4,true),buffer=this.resources.get(id);
   if(!buffer||buffer.destroyed||!count||count*p.elementBytes>buffer.size)throw RangeError('Invalid GPU resource binding');
   bindings[p.name]=buffer;scalars[p.name+'_count']=count;
  });
  const constantBuffer=await this.runtime.createSharedBuffer(constants);
  try{
   bindings[cbs[0].name]=constantBuffer;
   await this.runtime.native.batch().dispatch(shader.kernel.bind(bindings,scalars),groups).submit();
  }finally{this.runtime.destroyBuffer(constantBuffer);}
 }
}
