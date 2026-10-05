import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {execFileSync} from 'node:child_process';
import {serve} from '../../scripts/serve.mjs';
const root=path.resolve(import.meta.dirname,'../..'),out=path.join(root,'engine/artifacts/webgpu');
const custom=process.argv.includes('--custom');
const browserExecutable=custom?'D:/ChromiumRTXCuda/src/out/RTXCuda/chrome.exe':'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const {chromium}=await import(pathToFileURL('D:/cuda-webshader/node_modules/playwright/index.mjs'));
const {server,origin}=await serve();let browser;
const report={date:new Date().toISOString(),browserLane:custom?'ChromiumRTXCuda':'Edge',browserExecutable,fullUnrealRenderer:false,customShaders:false,nativeGpuBridge:false,cases:[]};
try {
 browser=await chromium.launch({executablePath:browserExecutable,headless:true});
 report.browser=await browser.version();
 const page=await browser.newPage();page.on('pageerror',e=>console.error(e));
 page.on('console',message=>{if(message.text().startsWith('Texture clear mismatch:'))console.error(message.text());});
 await page.goto(origin+'/engine-runtime/test.html');
 await page.evaluate(async()=>{
  if(!navigator.gpu)throw Error('WebGPU is not available');
  const {default:create}=await import('/engine-webgpu/resources.mjs');
  globalThis.gpuTest=await create({instantiateWasm(imports,receive){WebAssembly.instantiateStreaming(fetch('/engine-webgpu/resources.wasm'),imports).then(({instance,module})=>{globalThis.gpuMemory=instance.exports.memory;receive(instance,module);});return {};}});
  gpuTest._gpu_init();
 });
 await page.waitForFunction(()=>[2,3].includes(gpuTest._gpu_state()),{},{timeout:30000});
 assert.equal(await page.evaluate(()=>gpuTest._gpu_state()),2,await page.evaluate(()=>gpuTest.UTF8ToString(Number(gpuTest._gpu_error()))));
 assert.equal(await page.evaluate(()=>gpuTest._gpu_debug_begin()),1,'Nested debug markers and invalid stack input rejection');
 assert.equal(await page.evaluate(()=>gpuTest._gpu_sampler_checks()),1,'Sampler descriptors, comparison, anisotropy and rejected invalid parameters');
 assert.equal(await page.evaluate(()=>gpuTest._gpu_uniform_run()),1,'Uniform layout packing and invalid-input rejection');
 await page.waitForFunction(()=>gpuTest._gpu_uniform_done(),{},{timeout:30000});
 assert.equal(await page.evaluate(()=>gpuTest._gpu_uniform_passed()),1,'Both uniform upload versions retain exact constants, zero resource slots and padding');
 report.uniformUploads={versions:2,exactBytesCompared:128,resourcePointersZeroed:true,paddingZeroed:true,invalidInputsRejected:7,unrealFactoryExecuted:false};
 report.samplers={created:3,invalidDescriptorsRejected:9,duplicateCreationRejected:true};
 report.shaderContainerRejections=await page.evaluate(()=>gpuTest._gpu_shader_rejections());
 assert.ok(report.shaderContainerRejections>20,'Shader container truncation, invalid metadata and constant ranges rejected');
 assert.equal(await page.evaluate(()=>gpuTest._gpu_integrate_compile()),1,'Original Lumen integration pipeline creation through C++ Wasm');
 assert.equal(await page.evaluate(()=>gpuTest._gpu_integrate_run()),1,await page.evaluate(()=>gpuTest.UTF8ToString(Number(gpuTest._gpu_error()))));
 await page.waitForFunction(()=>gpuTest._gpu_integrate_done(),{},{timeout:30000});
 assert.equal(await page.evaluate(()=>gpuTest._gpu_integrate_passed()),1,'Original Lumen unlit branch writes exact packed/R8 outputs, preserving outside pixels and the second layer');
 for(let index=0;index<4;index++) {
  assert.equal(await page.evaluate(i=>gpuTest._gpu_run(i),index),1,'C++ resource commands accepted');
  await page.waitForFunction(()=>gpuTest._gpu_done(),{},{timeout:30000});
  assert.equal(await page.evaluate(()=>gpuTest._gpu_passed()),1,'Exact GPU readback and invalid-command rejection');
  report.cases.push({name:index===0?'buffer upload/copy/readback':`RGBA8 clear/copy/readback channel ${index-1}`,passed:true,bytesCompared:index===0?4096:24});
 }
 const manifest=JSON.parse(await readFile(path.join(out,'shader-manifest.json'),'utf8'));
 assert.equal(createHash('sha256').update(await readFile(manifest.source)).digest('hex'),manifest.sourceSHA256);
 const casesPath=path.join(root,'engine/artifacts/lumen-cases.bin'),nativePath=path.join(out,custom?'lumen-native-custom.bin':'lumen-native-edge.bin');
 execFileSync(path.join(root,'engine/artifacts/native-reference.exe'),[manifest.source,casesPath,nativePath]);
 const cases=await readFile(casesPath),reference=await readFile(nativePath),count=cases.readUInt32LE();
 assert.equal(cases.length,4+count*132);assert.equal(reference.length,count*80);
 for(let i=0;i<count;i++) {
  const input=Array.from({length:33},(_,j)=>cases.readUInt32LE(4+i*132+j*4));
  assert.equal(await page.evaluate(input=>{const pointer=gpuTest._gpu_lumen_input();if(typeof pointer!=='bigint')throw Error('Expected Wasm64');new Uint32Array(gpuMemory.buffer,Number(pointer),33).set(input);return gpuTest._gpu_lumen_run();},input),1);
  await page.waitForFunction(()=>gpuTest._gpu_lumen_done(),{},{timeout:30000});
  const actual=await page.evaluate(()=>{const pointer=gpuTest._gpu_lumen_output();if(!pointer)throw Error('Lumen readback failed');return [...new Uint32Array(gpuMemory.buffer,Number(pointer),20)];});
  assert.deepEqual(actual,Array.from({length:20},(_,j)=>reference.readUInt32LE(i*80+j*4)),'Original Lumen pass must match hardware Direct3D');
  assert.deepEqual(actual.slice(18),[0xcdcdcdcd,0xcdcdcdcd]);
 }
 report.lumen={entry:manifest.entry,cases:count,exactUintComparisons:count*20,sourceSHA256:manifest.sourceSHA256,wgslSHA256:manifest.wgslSHA256,shaderContainerSHA256:manifest.shaderSHA256,fullLighting:false};
 const copyManifest=JSON.parse(await readFile(path.join(out,'memcpy-manifest.json'),'utf8'));
 let copyCases=0;
 for(const mode of [0,1])for(const size of [0,1,63,64,65,127,129])for(const offset of [0,5,19]) {
  assert.equal(await page.evaluate(({mode,size,offset})=>gpuTest._gpu_memcpy_run(mode,size,offset,23-offset),{mode,size,offset}),1);
  await page.waitForFunction(()=>gpuTest._gpu_memcpy_done(),{},{timeout:30000});
  assert.equal(await page.evaluate(()=>gpuTest._gpu_memcpy_passed()),1,'Original memcpy kernel preserves exact copy range and surrounding words');
  ++copyCases;
 }
 assert.equal(await page.evaluate(()=>gpuTest._gpu_memcpy_run(0,257,0,0)),0);
 report.rootAndGlobalConstants={...copyManifest,cases:copyCases,exactUintComparisons:copyCases*256,lumenUploadBytes:124,rootCopyUploadBytes:16,legacyCopyUploadBytes:12};
 const vectorManifest=JSON.parse(await readFile(path.join(out,'vector-memcpy-manifest.json'),'utf8'));
 let vectorCases=0,vectorComparisons=0;
 for(const mode of [2,3])for(const size of [0,1,63,64,65,127,129])for(const offset of [0,5,19]) {
  assert.equal(await page.evaluate(({mode,size,offset})=>gpuTest._gpu_memcpy_run(mode,size,offset,23-offset),{mode,size,offset}),1);
  await page.waitForFunction(()=>gpuTest._gpu_memcpy_done(),{},{timeout:30000});
  assert.equal(await page.evaluate(()=>gpuTest._gpu_memcpy_passed()),1,'Original vector memcpy preserves element stride, offsets and untouched words');
  ++vectorCases;vectorComparisons+=256*(mode===2?2:4);
 }
 for(const mode of [2,3])assert.equal(await page.evaluate(mode=>gpuTest._gpu_memcpy_run(mode,257,0,0),mode),0);
 report.vectorBuffers={shaders:vectorManifest,cases:vectorCases,exactUintComparisons:vectorComparisons};
 let indirectCases=0;
 for(const size of [0,1,63,64,65,127,129])for(const offset of [0,5,19]) {
  assert.equal(await page.evaluate(({size,offset})=>gpuTest._gpu_memcpy_run(4,size,offset,23-offset),{size,offset}),1);
  await page.waitForFunction(()=>gpuTest._gpu_memcpy_done(),{},{timeout:30000});
  assert.equal(await page.evaluate(()=>gpuTest._gpu_memcpy_passed()),1,'GPU-produced indirect arguments drive the original copy kernel with exact output and guards');
  ++indirectCases;
 }
 report.indirectDispatch={cases:indirectCases,exactUintComparisons:indirectCases*256,gpuProducedArguments:true,cpuArgumentReadback:false,zeroWorkgroupsTested:true,invalidArgumentRangesRejected:4};
 let clearCases=0;
 for(const count of [0,1,63,64,65,129])for(const first of [0,5,19])for(const value of [0,0xffffffff,0x12345678,0x3f800000]) {
  assert.equal(await page.evaluate(args=>gpuTest._gpu_clear_run(args.first,args.count,args.value),{first,count,value}),1);
  await page.waitForFunction(()=>gpuTest._gpu_clear_done(),{},{timeout:30000});
  assert.equal(await page.evaluate(()=>gpuTest._gpu_clear_passed()),1,'Original Unreal clear shader preserves subview boundaries, exact values and outside guards');
  ++clearCases;
 }
 assert.equal(await page.evaluate(()=>gpuTest._gpu_clear_run(255,2,1)),0);
 const clearManifest=JSON.parse(await readFile(path.join(out,'clear-buffer-manifest.json'),'utf8'));
 report.bufferClear={...clearManifest,cases:clearCases,exactUintComparisons:clearCases*384,subviewOffsetBytes:256,unrealContextExecuted:false};
 console.log(`PASS: ${indirectCases} GPU-produced indirect dispatches / ${indirectCases*256} exact uint checks; ${clearCases} original buffer clears / ${clearCases*384} exact uint checks`);
 const textureCaps=await page.evaluate(()=>gpuTest._gpu_texture_caps());
 const formats=['r8unorm','rgba8unorm','r32uint','rgba16float','rgba32float','rg11b10ufloat'];
 report.textures={textureFormatsTier1:!!(textureCaps&1),rg11b10Renderable:!!(textureCaps&2),shaderWritesTested:false,cases:[],exactBytesCompared:0};
 assert.equal(await page.evaluate(()=>gpuTest._gpu_texture_rejections()),1,'Invalid texture sizes, mip counts and formats rejected');
 for(let format=0;format<formats.length;++format) {
  if(format===5&&!(textureCaps&2))continue;
  for(const mip of [0,1,2])for(const firstLayer of [0,1,2]) {
   assert.equal(await page.evaluate(({format,mip,firstLayer})=>gpuTest._gpu_texture_run(format,mip,firstLayer),{format,mip,firstLayer}),1,'Format-preserving texture allocation/views/clear/copy accepted');
   await page.waitForFunction(()=>gpuTest._gpu_texture_done(),{},{timeout:30000});
   assert.equal(await page.evaluate(()=>gpuTest._gpu_texture_passed()),1,`${formats[format]} mip ${mip} layer ${firstLayer} exact storage bytes and row padding`);
   report.textures.cases.push({format:formats[format],mip,firstLayer,layers:3-firstLayer,passed:true});
   report.textures.exactBytesCompared+=256*Math.max(1,5>>mip)*(3-firstLayer);
  }
 }
 report.platformServices=await page.evaluate(async()=>{const {default:create}=await import('/engine-platform/services.mjs');const m=await create();return m._platform_run();});
 let textureClearCases=0;
 for(let format=0;format<6;++format)for(let mip=0;mip<3;++mip)for(let first=0;first<3;++first)for(let layers=1;layers<=3-first;++layers)for(let valueCase=0;valueCase<3;++valueCase) {
  const args={format,mip,first,layers,valueCase};
  assert.equal(await page.evaluate(a=>gpuTest._gpu_texture_clear_run(a.format,a.mip,a.first,a.layers,a.valueCase),args),1,'Texture UAV clear accepted');
  await page.waitForFunction(()=>gpuTest._gpu_texture_clear_done(),{},{timeout:30000});
  assert.equal(await page.evaluate(()=>gpuTest._gpu_texture_clear_passed()),1,`Exact texture UAV clear and unchanged mips/layers: ${JSON.stringify(args)}`);
  ++textureClearCases;
 }
 report.textureClears={cases:textureClearCases,exactBytesCompared:textureClearCases*6144,formats:6,integerHighBitsTested:true,unrealContextExecuted:false};
 console.log(`PASS: ${textureClearCases} texture clear cases / ${textureClearCases*6144} exact bytes including untouched mips/layers and row padding`);
 assert.equal(await page.evaluate(()=>gpuTest._gpu_debug_end()),1,'Debug marker stack closed after original shader commands');
 report.debugGroups={nestedGroups:2,depthLimitChecked:128,underflowRejected:true,invalidLabelRejected:true,gpuTimestamps:false};
 assert.equal(report.platformServices,0,'Actual Unreal platform adapters must pass');
 assert.equal(await page.evaluate(()=>gpuTest._gpu_state()),2,await page.evaluate(()=>gpuTest.UTF8ToString(Number(gpuTest._gpu_error()))));
 const integrate=JSON.parse(await readFile(path.join(out,'integrate-manifest.json'),'utf8'));
 report.lumenIntegrationPipeline={entry:integrate.entry,wgslSHA256:integrate.wgslSHA256,shaderContainerSHA256:integrate.shaderContainerSHA256,resourceFormats:integrate.resourceFormats,createdThroughWasm:true,dispatched:true,branch:'unlit-material',boundResources:21,resourceTableBindings:7,engineProducedContainer:true,layoutHashRejectionTested:true,formatRejectionTested:true,exactBytesCompared:10752,litMaterialEvaluated:false,fullLighting:false};
 report.passed=true;report.wasm64=true;report.exactResourceBytesCompared=4168;
 console.log('PASS: original Lumen integration unlit branch from UE-produced WCSH v2; 21 C++ bindings including 7 UE resource-table entries; 10752 exact output/guard bytes; lit-material lighting not exercised');
 console.log(`PASS: ${report.textures.cases.length} texture cases / ${report.textures.exactBytesCompared} exact storage/padding bytes; tier1=${report.textures.textureFormatsTier1}`);
 console.log(`PASS: C++ Wasm64 -> WebGPU; 4168 resource bytes; ${count} original Lumen cases / ${count*20} uint comparisons; ${copyCases} root/global constant cases / ${copyCases*256} copy/guard comparisons; ${vectorCases} vector cases / ${vectorComparisons} copy/guard comparisons; UE platform services`);
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;console.error(error);}finally{
 report.wasmSHA256=createHash('sha256').update(await readFile(path.join(out,'resources.wasm'))).digest('hex');
 report.backendSHA256=createHash('sha256').update(await readFile(path.join(root,'Source/WebGPURHI/Private/WebGPUDevice.cpp'))).digest('hex');
 report.uniformPackingSHA256=createHash('sha256').update(await readFile(path.join(root,'Source/WebGPURHI/Public/WebGPUUniformData.h'))).digest('hex');
 report.bridgeSHA256=createHash('sha256').update(await readFile(path.join(root,'engine/Runtime/webgpu-wasm64.js'))).digest('hex');
 await writeFile(path.join(out,custom?'validation-custom.json':'validation.json'),JSON.stringify(report,null,2));
 await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
