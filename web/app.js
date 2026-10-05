import {GpuRuntime,requestPermission} from '/webcuda/runtime/runtime.js';
const $=s=>document.querySelector(s),canvas=$('canvas'),ctx=canvas.getContext('webgpu');
const report={schema:1,scope:'Capability prototype; browser lanes do not run Unreal',browserNanite:false,browserLumen:false,dlssImplemented:false,measurements:[],errors:[]};
let fixture,artifact,rt,resources,mode,width=1280,height=720,busy=false;
const status=text=>$('#status').textContent=text;
const showReport=()=>$('#report').textContent=JSON.stringify(report,null,2);
function setBusy(value){busy=value;document.querySelectorAll('button,select,input').forEach(e=>e.disabled=value);}
function fail(error){report.errors.push(String(error.stack||error));status(error.message);showReport();console.error(error);}
async function load(){
 const bytes=await (await fetch('generated/scene.wasm')).arrayBuffer();
 const {instance}=await WebAssembly.instantiate(bytes),e=instance.exports;
 const copy=(ptr,n)=>new Float32Array(e.memory.buffer,ptr,n).slice();
 const count=e.scene_box_count();
 fixture={count,boxes:copy(e.scene_boxes(),count*12),camera:copy(e.scene_camera(),7),light:copy(e.scene_light(),3),vertices:copy(e.scene_vertices(),e.scene_vertex_count()*3)};
 report.fixture={wasm:true,version:e.scene_version(),boxes:count,triangles:e.scene_vertex_count()/3};
 artifact=await (await fetch('generated/fixture.json')).json();
 try{const response=await fetch('/artifacts/native/report.json');if(response.ok)report.native=await response.json();}catch{}
 await select('webgpu');
 window.sample.ready=true;
}
async function select(next){
 if(busy)throw Error('Sample is busy');setBusy(true);window.sample.ready=false;
 try{
  status('Preparing '+next+'…');
  if(rt){await rt.dispose();rt=null;resources=null;}
  mode=next;
  document.querySelectorAll('[data-mode]').forEach(b=>b.classList.toggle('active',b.dataset.mode===mode));
  canvas.hidden=mode==='native';$('#native-image').hidden=mode!=='native';
  $('#light').value=1;
  if(mode==='native'){
   const response=await fetch('/artifacts/native/report.json');if(!response.ok)throw Error('Run scripts/native.ps1 Benchmark to create the native capture.');
   report.native=await response.json();$('#native-image').src='/artifacts/native/scene.png?'+Date.now();
   $('#renderer').textContent='Native Unreal · captured frame';$('#path').textContent='Unreal / DX12';
   $('#adapter').textContent=report.native.gpu;$('#timing').textContent=report.native.p50Ms.toFixed(2)+' ms';
   status('Native capture · fixed light · full Unreal renderer · timings are separate from browser probes.');
  }else{
   const custom=mode!=='webgpu';
   rt=await GpuRuntime.create({backend:'webgpu',nativeInterop:custom?{requirements:{sharedBuffers:true,...(mode==='optix'?{optix:true}:{})}}:false,onError:fail});
   if(custom&&!rt.native)throw Error('Custom path unavailable: '+JSON.stringify(rt.nativeInteropStatus));
   width=Number($('#resolution').value);height=width*9/16;canvas.width=width;canvas.height=height;
   ctx.configure({device:rt.device,format:'rgba8unorm',alphaMode:'opaque',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_DST});
   const alloc=data=>custom?rt.createSharedBuffer(data):rt.createBuffer(data);
   resources={pixels:await alloc(width*height*4),boxes:await alloc(fixture.boxes),camera:await alloc(new Float32Array([...fixture.camera,...fixture.light]))};
   if(mode==='optix'){
    resources.vertices=await alloc(fixture.vertices);
    resources.scene=await rt.native.createAccelerationStructure({vertexCount:fixture.vertices.length/3,allowUpdate:false});
    resources.kernel=await rt.native.rayTracingPipeline(await (await fetch('generated/fixture-optix.cu')).text(),{maxTraceDepth:1,numPayloadValues:2,parameters:[{name:'pixels',type:'buffer',element:'uint'},{name:'boxes',type:'buffer',element:'float'},{name:'camera',type:'buffer',element:'float'}]});
    await rt.native.batch().buildAccelerationStructure(resources.scene,resources.vertices).submit();
    resources.invocation=resources.kernel.bind(resources.scene,{pixels:resources.pixels,boxes:resources.boxes,camera:resources.camera});
   }else{
    resources.kernel=custom?await rt.native.kernel(artifact):await rt.kernel(artifact);
    resources.invocation=resources.kernel.bind({pixels:resources.pixels,boxes:resources.boxes,camera:resources.camera},{width,height,count:fixture.count});
   }
   report[mode]={runtime:rt.describe(),native:custom?rt.native.capabilities:null,adapterFeatures:[...rt.adapter.features],wgslFeatures:[...navigator.gpu.wgslLanguageFeatures]};
   await frame();
   $('#renderer').textContent={webgpu:'Wasm fixture · WGSL / WebGPU',cuda:'Wasm fixture · native CUDA',optix:'Wasm fixture · OptiX hardware traversal'}[mode];
   $('#path').textContent={webgpu:'Standard WebGPU',cuda:'Native CUDA interop',optix:'RTX / OptiX interop'}[mode];
   $('#dimensions').textContent=`${width} × ${height}`;
   $('#adapter').textContent=rt.adapter.info.description||rt.adapter.info.device||rt.adapter.info.vendor;
   $('#timing').textContent='Not measured';status('Ready · same C++ geometry and camera · '+(custom?'native GPU bridge active':'standard browser GPU path'));
  }
  showReport();window.sample.ready=true;
 }finally{setBusy(false);if(mode==='native'){$('#benchmark').disabled=true;$('#light').disabled=true;$('#probes').disabled=true;}}
}
async function frame(){
 if(!rt||!resources)throw Error('No renderer active');
 if(mode==='webgpu')await rt.batch().dispatch(resources.invocation,[Math.ceil(width/8),Math.ceil(height/8)]).submit();
 else if(mode==='cuda')await rt.native.batch().dispatch(resources.invocation,[Math.ceil(width/8),Math.ceil(height/8)]).submit();
 else await rt.native.batch().trace(resources.invocation,[width,height]).submit();
 const encoder=rt.device.createCommandEncoder();
 encoder.copyBufferToTexture({buffer:resources.pixels.gpuBuffer,bytesPerRow:width*4},{texture:ctx.getCurrentTexture()},[width,height]);
 rt.device.queue.submit([encoder.finish()]);await rt.device.queue.onSubmittedWorkDone();
}
async function light(value){
 if(busy||!rt)return;
 setBusy(true);
 try{rt.device.queue.writeBuffer(resources.camera.gpuBuffer,7*4,new Float32Array([value]));await frame();}
 finally{setBusy(false);}
}
async function benchmark(){
 if(busy||mode==='native')return;setBusy(true);
 try{
  status('Warming 20 frames, then measuring 60 completed frames…');
  for(let i=0;i<20;i++)await frame();
  const times=[];for(let i=0;i<60;i++){const t=performance.now();await frame();times.push(performance.now()-t);}
  const sorted=[...times].sort((a,b)=>a-b);
  const result={mode,width,height,lightX:Number($('#light').value),warmupFrames:20,frames:60,meanMs:times.reduce((a,b)=>a+b)/times.length,p50Ms:sorted[29],p95Ms:sorted[56],timing:'Serial completed-frame wall time including dispatch, native bridge if used, presentation submission and queue completion. Excludes compilation/scene construction. Not GPU-only or display refresh FPS.',samplesMs:times};
  report.measurements.push(result);$('#timing').textContent=result.p50Ms.toFixed(2)+' ms';showReport();status('Measurement complete · export the report for raw samples and provenance.');return result;
 }finally{setBusy(false);}
}
async function probes(){
 if(busy||!rt)return;setBusy(true);
 try{
  report.probes={wgslFeatures:[...navigator.gpu.wgslLanguageFeatures],adapterFeatures:[...rt.adapter.features],nativeAtomic64:{status:'not run; select CUDA or RTX'},optix:{status:mode==='optix'?'scene traversal rendered':'not run; select RTX / OptiX'},nanite:'not implemented in browser',lumen:'not implemented in browser',dlss:'not implemented in sample'};
  if(rt.native){
   const data=await rt.createSharedBuffer(new Uint32Array([0xfffffffe,0]));
   try{
    const kernel=await rt.native.kernel('__global__ void probe(unsigned* state){atomicAdd((unsigned long long*)state,1ULL);}',{entry:'probe',workgroupSize:[128,1,1]});
    await rt.native.batch().dispatch(kernel.bind({state:data}),[8]).submit();
    const words=await rt.read(data,Uint32Array);
    if(words[0]!==1022||words[1]!==1)throw Error('64-bit atomic carry mismatch: '+words);
    report.probes.nativeAtomic64={status:'passed',operation:'atomicAdd u64 with 1024 contending threads crossing the 32-bit boundary',words:[...words],doesNotProve:'Nanite visibility atomic operations or complete shader compatibility'};
    rt.device.queue.writeBuffer(data.gpuBuffer,0,new Uint32Array([0xffffffff,0xffffffff]));
    const minimum=await rt.native.kernel('__global__ void minimum(unsigned* state){unsigned i=blockIdx.x*blockDim.x+threadIdx.x;atomicMin((unsigned long long*)state,(2ULL<<32)|(1023ULL-i));}',{entry:'minimum',workgroupSize:[128,1,1]});
    await rt.native.batch().dispatch(minimum.bind({state:data}),[8]).submit();
    const winner=await rt.read(data,Uint32Array);
    if(winner[0]!==0||winner[1]!==2)throw Error('64-bit minimum mismatch: '+winner);
    report.probes.nativeAtomic64.minimum={status:'passed',operation:'atomicMin u64 packed key, 1024 contending candidates',words:[...winner]};
   }finally{rt.destroyBuffer(data);}
  }
  status('Capability probes complete. See report for tested operations and remaining engine features.');showReport();return report.probes;
 }finally{setBusy(false);}
}
window.sample={ready:false,report,select,frame,benchmark,probes,light,pixels:async()=>Array.from(await rt.read(resources.pixels,Uint32Array)),fixture:()=>fixture,dispose:async()=>rt?.dispose()};
document.querySelectorAll('[data-mode]').forEach(b=>b.onclick=async()=>{try{if(['cuda','optix'].includes(b.dataset.mode))await requestPermission();await select(b.dataset.mode);}catch(e){fail(e);}});
$('#resolution').onchange=()=>select(mode).catch(fail);
$('#light').oninput=e=>light(Number(e.target.value)).catch(fail);
$('#reset').onclick=()=>{$('#light').value=1;light(1).catch(fail);};
$('#benchmark').onclick=()=>benchmark().catch(fail);$('#probes').onclick=()=>probes().catch(fail);
$('#export').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:'application/json'}));a.download='sample-report.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);};
load().catch(fail);
