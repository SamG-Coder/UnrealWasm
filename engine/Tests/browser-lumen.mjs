import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {serve} from '../../scripts/serve.mjs';
const root=path.resolve(import.meta.dirname,'../..'),out=path.join(root,'engine/artifacts');
const entry='SetupAdaptiveProbeIndirectArgsCS';
const manifest=JSON.parse(await readFile(path.join(out,'exported',entry+'.json'),'utf8'));
const shader=await readFile(manifest.source);
assert.equal(createHash('sha256').update(shader).digest('hex'),manifest.sourceSHA256,'Unreal source provenance');
const cases=[];
for(const width of [1,7,8,17,80])for(const adaptive of [0,1,73,1000]){
 const constants=new Uint32Array(32);constants.fill(0xabcdef12);
 constants[4]=8;constants[5]=6;constants[6]=8;constants[10]=width;constants[11]=999;
 constants[29]=width===1?0:127;constants[30]=73;
 cases.push({constants:[...constants],adaptive});
}
for(let i=0;i<12;i++){
 const constants=new Uint32Array(32);constants.fill(0xfefefefe);
 constants[4]=[1,4,12,16][i%4];constants[5]=[2,6,8][i%3];constants[6]=constants[5]+2;
 constants[10]=3+i*5;constants[11]=0;constants[29]=i*311;constants[30]=i*7;
 cases.push({constants:[...constants],adaptive:i*13});
}
const binary=Buffer.alloc(4+cases.length*132);binary.writeUInt32LE(cases.length);
cases.forEach((c,i)=>[...c.constants,c.adaptive].forEach((v,j)=>binary.writeUInt32LE(v,4+i*132+j*4)));
await writeFile(path.join(out,'lumen-cases.bin'),binary);
execFileSync(path.join(out,'native-reference.exe'),[manifest.source,path.join(out,'lumen-cases.bin'),path.join(out,'lumen-native.bin')],{stdio:'inherit'});
const nativeBytes=await readFile(path.join(out,'lumen-native.bin'));
assert.equal(nativeBytes.length,cases.length*80);
const reference=cases.map((_,i)=>Array.from({length:20},(_,j)=>nativeBytes.readUInt32LE(i*80+j*4)));
const {chromium}=await import(pathToFileURL('D:/cuda-webshader/node_modules/playwright/index.mjs'));
const {server,origin}=await serve();let context;
const report={date:new Date().toISOString(),entry,sourceSHA256:manifest.sourceSHA256,cudaSHA256:manifest.cudaSHA256,compiler:manifest.compiler,fullLumenRenderer:false,cases:cases.length};
try{
 context=await chromium.launchPersistentContext(path.join(out,'profiles','lumen-'+Date.now()),{executablePath:'D:/ChromiumRTXCuda/src/out/RTXCuda/chrome.exe',headless:false,chromiumSandbox:true,args:['--no-first-run','--no-default-browser-check']});
 const page=context.pages()[0];page.setDefaultTimeout(90000);
 await page.goto(origin+'/engine-runtime/test.html');
 const cdp=await context.newCDPSession(page);
 await cdp.send('Browser.setPermission',{permission:{name:'native-gpu'},setting:'granted',origin});
 const results=await page.evaluate(async({entry,cases})=>{
  const {GpuRuntime}=await import('/webcuda/runtime/runtime.js');
  const {WebCudaDispatchQueue}=await import('/engine-runtime/dispatch.js');
  const rt=await GpuRuntime.create({backend:'webgpu',nativeInterop:{requirements:{sharedBuffers:true}}});
  const module=await WebAssembly.compile(await (await fetch('/engine-bridge.wasm')).arrayBuffer());
  const imports=WebAssembly.Module.imports(module);
  if(imports.length)throw Error('Unexpected bridge imports: '+JSON.stringify(imports));
  const instance=await WebAssembly.instantiate(module,{}),wasm=instance.exports;
  wasm._initialize?.();
  if(typeof wasm.webcuda_constants()!=='bigint')throw Error('Bridge is not Wasm64');
  const queue=new WebCudaDispatchQueue(rt,wasm.memory);
  const manifest=await (await fetch('/engine-shaders/'+entry+'.json')).json();
  const code=await (await fetch('/engine-shaders/'+entry+'.cu')).text();
  let corruptRejected=false;
  try{await queue.registerShader(99,code+' ',manifest);}catch(e){corruptRejected=e.message.includes('hash mismatch');}
  if(!corruptRejected)throw Error('Corrupted compiler artifact was accepted');
  await queue.registerShader(1,code,manifest);
  const adaptiveBuffer=await rt.createSharedBuffer(new Uint32Array(1));
  const outputBuffer=await rt.createSharedBuffer(new Uint32Array(20));
  const adaptiveHandle=queue.registerBuffer(adaptiveBuffer),outputHandle=queue.registerBuffer(outputBuffer);
  const outputs=[];
  try{
   for(const c of cases){
    new Uint32Array(wasm.memory.buffer,Number(wasm.webcuda_constants()),32).set(c.constants);
    new Uint32Array(wasm.memory.buffer,Number(wasm.webcuda_resources()),4).set([adaptiveHandle,1,outputHandle,20]);
    rt.write(adaptiveBuffer,new Uint32Array([c.adaptive]));
    rt.write(outputBuffer,new Uint32Array(20).fill(0xcdcdcdcd));
    const pointer=wasm.webcuda_dispatch(1,1,1,1,128,2);
    if(!pointer)throw Error('Wasm packet construction failed');
    await queue.submit(pointer);outputs.push([...(await rt.read(outputBuffer,Uint32Array))]);
   }
   const rejects=[];
   const rejected=async(label,callback)=>{let ok=false;try{await callback();}catch{ok=true;}if(!ok)throw Error(label+' was accepted');rejects.push(label);};
   await rejected('out-of-bounds Wasm pointer',()=>queue.submit(0xffffffffffffffffn));
   await rejected('unknown shader',()=>queue.submit(wasm.webcuda_dispatch(999,1,1,1,128,2)));
   await rejected('incorrect constant layout',()=>queue.submit(wasm.webcuda_dispatch(1,1,1,1,124,2)));
   new Uint32Array(wasm.memory.buffer,Number(wasm.webcuda_resources()),4).set([adaptiveHandle,99,outputHandle,20]);
   await rejected('GPU resource range overflow',()=>queue.submit(wasm.webcuda_dispatch(1,1,1,1,128,2)));
   if(wasm.webcuda_dispatch(1,0,1,1,128,2)!==0n)throw Error('Zero dispatch was accepted');
   return {outputs,rejects,corruptRejected,wasm64:true,backend:'native CUDA via ChromiumRTXCuda',imports};
  }finally{rt.destroyBuffer(adaptiveBuffer);rt.destroyBuffer(outputBuffer);await rt.dispose();}
 },{entry,cases});
 assert.deepEqual(results.outputs,reference,'Wasm -> CUDA must exactly match original HLSL on Direct3D');
 assert.ok(reference.some(values=>values.slice(0,18).some(n=>n>1)));
 assert.ok(results.outputs.every(values=>values.slice(0,18).every(n=>n!==0xcdcdcdcd)&&values.slice(18).every(n=>n===0xcdcdcdcd)),'All 18 writes and both guard words');
 report.passed=true;report.exactUintComparisons=cases.length*20;report.result=results;report.nativeReference='Original UE-preprocessed HLSL, Microsoft compiler cs_5_0, hardware Direct3D 11';
 console.log(`PASS: ${cases.length} real Lumen pass cases, ${cases.length*20} exact uint comparisons, Wasm64 -> browser CUDA`);
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;console.error(error);}finally{
 report.wasmSHA256=createHash('sha256').update(await readFile(path.join(root,'Binaries/WebCuda/WebCudaBridge.wasm'))).digest('hex');
 await writeFile(path.join(out,'lumen-validation.json'),JSON.stringify(report,null,2));
 await context?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
