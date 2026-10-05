import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {serve} from '../../scripts/serve.mjs';
const out=path.resolve(import.meta.dirname,'../artifacts/oodle');
const data=path.join(out,'cases');await mkdir(data,{recursive:true});
const {chromium}=await import(pathToFileURL('D:/cuda-webshader/node_modules/playwright/index.mjs'));
const {server,origin}=await serve();let browser;
const report={date:new Date().toISOString(),codec:'Oodle Kraken 2.9.16',nativeHostBridge:false,fullUnrealEngine:false,cases:[]};
try {
 browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 const page=await browser.newPage();page.on('console',msg=>console.log('[browser]',msg.text()));await page.goto(origin+'/engine-runtime/test.html');
 report.browser=await browser.version();
 await page.evaluate(async()=>{
  const {default:create}=await import('/engine-oodle/oodle.mjs');
  globalThis.oodle=await create({instantiateWasm(imports,receive){
   WebAssembly.instantiateStreaming(fetch('/engine-oodle/oodle.wasm'),imports).then(({instance,module})=>{globalThis.rawOodle=instance.exports;receive(instance,module);});return {};
  }});
  const raw=globalThis.rawOodle,p=raw.malloc(16n);
  if(typeof p!=='bigint'||!p)throw Error('Expected raw Wasm64 pointer');raw.free(p);
  Object.defineProperty(globalThis.oodle,'HEAPU8',{get:()=>new Uint8Array(raw.memory.buffer)});
  globalThis.oodle._oodle_init();
 });
 for(const size of [1,15,256,4096,65535,262143,262144,262145,1048576])for(const pattern of ['zero','repeated','random']){
  const label=`${size}-${pattern}`,raw=Buffer.alloc(size);let state=0x183123;
  for(let i=0;i<size;i++){state^=state<<13;state^=state>>>17;state^=state<<5;raw[i]=pattern==='zero'?0:pattern==='repeated'?'Unreal Engine Oodle Wasm compatibility\n'.charCodeAt(i%38):state&255;}
  const source=path.join(data,label+'.raw'),native=path.join(data,label+'.native'),wasm=path.join(data,label+'.wasm'),decoded=path.join(data,label+'.decoded');
  await writeFile(source,raw);
  execFileSync(path.join(out,'oodle-native.exe'),['compress',source,native,String(size)]);
  const packed=await readFile(native);
  const result=await page.evaluate(({raw64,packed64,size})=>{
   const m=globalThis.oodle,from64=s=>Uint8Array.from(atob(s),c=>c.charCodeAt(0));
   const raw=from64(raw64),packed=from64(packed64),bound=m._oodle_bound(size);
   // Emscripten wraps malloc/free with JS Number pointers; our C exports keep i64.
   const p=BigInt(m._malloc(Math.max(size,packed.length))),q=BigInt(m._malloc(Math.max(size,bound)+32));
   if(!p||!q)throw Error('Allocation failed');
   try {
    m.HEAPU8.set(packed,Number(p));m.HEAPU8.fill(0xcd,Number(q),Number(q)+size+32);
    const decoded=m._oodle_decompress(p,packed.length,q,size);
    if(decoded!==size)throw Error('Decode length '+decoded);
    for(let i=0;i<size;i++)if(m.HEAPU8[Number(q)+i]!==raw[i])throw Error('Decoded data mismatch '+i);
    if(!m.HEAPU8.subarray(Number(q)+size,Number(q)+size+32).every(x=>x===0xcd))throw Error('Decode guard overwritten');
    m.HEAPU8.set(raw,Number(p));m.HEAPU8.fill(0xcd,Number(q),Number(q)+bound+32);
    const count=m._oodle_compress(p,size,q);
    if(count<=0||count>bound)throw Error('Encode failure '+count);
    if(!m.HEAPU8.subarray(Number(q)+bound,Number(q)+bound+32).every(x=>x===0xcd))throw Error('Encode guard overwritten');
    const encoded=m.HEAPU8.slice(Number(q),Number(q)+count);let s='';for(let i=0;i<encoded.length;i+=32768)s+=String.fromCharCode(...encoded.subarray(i,i+32768));
    m.HEAPU8.set(packed,Number(p));
    const truncated=m._oodle_decompress(p,1,q,size);
    if(truncated>0)throw Error('Truncated stream accepted');
    return {encoded:btoa(s),decoded,truncated,wasm64:true};
   }finally{m._free(p);m._free(q);}
  },{raw64:raw.toString('base64'),packed64:packed.toString('base64'),size});
  const wasmBytes=Buffer.from(result.encoded,'base64');await writeFile(wasm,wasmBytes);
  execFileSync(path.join(out,'oodle-native.exe'),['decompress',wasm,decoded,String(size)]);
  assert.deepEqual(await readFile(decoded),raw,'Windows must decode Wasm output exactly');
  report.cases.push({size,pattern,nativeBytes:packed.length,wasmBytes:wasmBytes.length,exactNativeToWasm:true,exactWasmToNative:true,truncatedRejected:true,guardsIntact:true});
 }
 report.passed=true;report.wasm64=true;
 console.log(`PASS: ${report.cases.length} cases, Windows -> browser Wasm64 and browser Wasm64 -> Windows, exact byte comparisons`);
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;console.error(error);}finally{
 report.wasmSHA256=createHash('sha256').update(await readFile(path.join(out,'oodle.wasm'))).digest('hex');
 await writeFile(path.join(out,'validation.json'),JSON.stringify(report,null,2));
 await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
