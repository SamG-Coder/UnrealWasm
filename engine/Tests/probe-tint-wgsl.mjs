// Validate generated original-engine WGSL in the actual browser compiler.
// No dispatch or rendered-scene claim is made by this compile-only check.
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {serve} from '../../scripts/serve.mjs';
const root=path.resolve(import.meta.dirname,'../..');
const directory=path.join(root,'engine/artifacts/shader-format/spirv-format-probe');
const file=process.argv.find(arg=>arg.startsWith('--source='))?.slice(9)||path.join(directory,'compiled/shader.wgsl');
const custom=process.argv.includes('--custom');
const code=await readFile(file,'utf8');
const {chromium}=await import(pathToFileURL('D:/cuda-webshader/node_modules/playwright/index.mjs'));
const {server,origin}=await serve();let browser;
const report={source:file,sourceSHA256:createHash('sha256').update(code).digest('hex'),browserLane:custom?'ChromiumRTXCuda':'Edge',dispatched:false,fullSceneRendered:false};
try {
 browser=await chromium.launch({executablePath:custom?'D:/ChromiumRTXCuda/src/out/RTXCuda/chrome.exe':'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 report.browser=await browser.version();const page=await browser.newPage();await page.goto(origin+'/engine-runtime/test.html');
 Object.assign(report,await page.evaluate(async code=>{
  const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'});
  const requiredFeatures=['texture-formats-tier1'].filter(feature=>adapter.features.has(feature));
  const storageTextures=(code.match(/:\s*texture_storage_/g)||[]).length;
  const requiredLimits={maxStorageTexturesPerShaderStage:storageTextures};
  if(adapter.limits.maxStorageTexturesPerShaderStage<storageTextures)throw Error('Adapter storage-texture limit is below the compiled shader requirement');
  const device=await adapter.requestDevice({requiredFeatures,requiredLimits});
  device.pushErrorScope('validation');
  const module=device.createShaderModule({code});
  const info=await module.getCompilationInfo();
  const messages=info.messages.map(({type,message,lineNum,linePos})=>({type,message,lineNum,linePos}));
  let pipelineError=null,pipelineCreated=false;
  if(!messages.some(m=>m.type==='error')) {
   try { await device.createComputePipelineAsync({layout:'auto',compute:{module,entryPoint:'main'}});pipelineCreated=true; }
   catch(error) {pipelineError=error.message;}
  }
  const validationError=(await device.popErrorScope())?.message||null;
  device.destroy();
  return {requiredFeatures,requiredLimits,messages,pipelineCreated,pipelineError,validationError,passed:pipelineCreated&&!validationError};
 },code));
 if(!report.passed)process.exitCode=1;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;}
finally {
 await writeFile(path.join(directory,custom?'browser-custom.json':'browser.json'),JSON.stringify(report,null,2));
 console.log(JSON.stringify(report,null,2));
 await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
