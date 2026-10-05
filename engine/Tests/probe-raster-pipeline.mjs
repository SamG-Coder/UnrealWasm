// Compiler/pipeline check for Unreal's original screen shaders. No scene draw.
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {serve} from '../../scripts/serve.mjs';
const root=path.resolve(import.meta.dirname,'../..'),out=path.join(root,'engine/artifacts/raster-probe');
const custom=process.argv.includes('--custom'),stages={};
for(const stage of ['vertex','fragment']) {
 const code=await readFile(path.join(out,stage,'shader.wgsl'),'utf8');
 const manifest=JSON.parse(await readFile(path.join(out,stage,'manifest.json'),'utf8'));
 if(createHash('sha256').update(code).digest('hex')!==manifest.wgslSHA256)throw Error('Stale raster output');
 stages[stage]={code,manifest};
}
const {chromium}=await import(pathToFileURL('D:/cuda-webshader/node_modules/playwright/index.mjs'));
const {server,origin}=await serve();let browser;
const report={browserLane:custom?'ChromiumRTXCuda':'Edge',wasmExecuted:false,drawSubmitted:false,fullSceneRendered:false,
 vertexSHA256:stages.vertex.manifest.wgslSHA256,fragmentSHA256:stages.fragment.manifest.wgslSHA256};
try {
 browser=await chromium.launch({executablePath:custom?'D:/ChromiumRTXCuda/src/out/RTXCuda/chrome.exe':'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 report.browser=await browser.version();const page=await browser.newPage();await page.goto(origin+'/engine-runtime/test.html');
 Object.assign(report,await page.evaluate(async stages=>{
  const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'});if(!adapter)throw Error('No adapter');
  const device=await adapter.requestDevice();device.pushErrorScope('validation');const modules={},messages=[];
  for(const stage of ['vertex','fragment']) {
   modules[stage]=device.createShaderModule({code:stages[stage].code});
   const info=await modules[stage].getCompilationInfo();
   messages.push(...info.messages.map(m=>({stage,type:m.type,message:m.message,lineNum:m.lineNum})));
  }
  let pipelineCreated=false,pipelineError=null;
  if(!messages.some(m=>m.type==='error'))try {
   await device.createRenderPipelineAsync({layout:'auto',
    vertex:{module:modules.vertex,entryPoint:'main',buffers:[{arrayStride:16,attributes:[{shaderLocation:0,offset:0,format:'float32x2'},{shaderLocation:1,offset:8,format:'float32x2'}]}]},
    fragment:{module:modules.fragment,entryPoint:'main',targets:[{format:'rgba8unorm'}]},primitive:{topology:'triangle-list'}});
   pipelineCreated=true;
  }catch(error){pipelineError=error.message;}
  const validationError=(await device.popErrorScope())?.message||null;device.destroy();
  return {pipelineCreated,pipelineError,validationError,messages,passed:pipelineCreated&&!validationError};
 },stages));
 if(!report.passed)process.exitCode=1;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;}
finally {
 await writeFile(path.join(out,custom?'browser-custom.json':'browser.json'),JSON.stringify(report,null,2));
 console.log(JSON.stringify(report,null,2));await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
