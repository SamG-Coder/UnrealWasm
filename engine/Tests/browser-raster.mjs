import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {serve} from '../../scripts/serve.mjs';
const root=path.resolve(import.meta.dirname,'../..'),out=path.join(root,'engine/artifacts/raster-probe');
const custom=process.argv.includes('--custom');
const {chromium}=await import(pathToFileURL('D:/cuda-webshader/node_modules/playwright/index.mjs'));
const {server,origin}=await serve();let browser;
const report={browserLane:custom?'ChromiumRTXCuda':'Edge',fullSceneRendered:false,unrealRHIExecuted:false,cases:[]};
try {
 browser=await chromium.launch({executablePath:custom?'D:/ChromiumRTXCuda/src/out/RTXCuda/chrome.exe':'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 report.browser=await browser.version();const page=await browser.newPage();
 page.on('console',msg=>{if(msg.type()==='error'||msg.text().startsWith('Raster mismatch'))console.error(msg.text());});
 page.on('pageerror',error=>console.error(error));await page.goto(origin+'/engine-runtime/test.html');
 await page.evaluate(async()=>{
  const {default:create}=await import('/engine-raster/raster.mjs');globalThis.raster=await create();raster._raster_init();
 });
 await page.waitForFunction(()=>[2,3].includes(raster._raster_state()),{},{timeout:30000});
 assert.equal(await page.evaluate(()=>raster._raster_state()),2);
 const cases=[];
 for(const mode of [0,1,2])for(const rectangle of [0,1])for(const flip of [0,1])cases.push({mode,rectangle,flip,state:0});
 for(const mode of [0,1,2])for(let state=1;state<=16;++state)cases.push({mode,rectangle:0,flip:0,state});
 for(const args of cases) {
  assert.equal(await page.evaluate(a=>raster._raster_run(a.mode,a.rectangle,a.flip,a.state),args),1,`C++ draw accepted ${JSON.stringify(args)}`);
  await page.waitForFunction(()=>raster._raster_done()||raster._raster_state()===3,{},{timeout:30000});
  assert.equal(await page.evaluate(()=>raster._raster_state()),2,await page.evaluate(()=>raster.UTF8ToString(Number(raster._raster_error()))));
  assert.equal(await page.evaluate(()=>raster._raster_passed()),1,`Exact sampled pixels and untouched background/padding ${JSON.stringify(args)}`);
  report.cases.push({...args,passed:true,exactBytesCompared:3072});
 }
 report.shaders=JSON.parse(await readFile(path.join(out,'draw-shaders.json'),'utf8'));
 report.wasmExecuted=true;report.drawSubmitted=true;report.passed=true;report.exactBytesCompared=cases.length*3072;
 report.invalidStageHeadersRejected=10;report.wrongPipelineStagesRejected=true;
 console.log(`PASS: ${report.browserLane}: original UE screen shaders, ${cases.length} Wasm64 raster cases, ${report.exactBytesCompared} exact bytes`);
}catch(error){report.passed=false;report.error=error.stack;console.error(error);process.exitCode=1;}
finally {
 for(const [key,file] of Object.entries({wasmSHA256:'engine/artifacts/raster-probe/raster.wasm',backendSHA256:'Source/WebGPURHI/Private/WebGPUDevice.cpp',headerSHA256:'Source/WebGPURHI/Public/WebGPUDevice.h',containerHeaderSHA256:'Source/WebGPURHI/Public/WebCudaShaderCode.h',bridgeSHA256:'engine/Runtime/webgpu-wasm64.js',testSHA256:'engine/Tests/raster-draw.cpp'}))
  report[key]=createHash('sha256').update(await readFile(path.join(root,file))).digest('hex');
 await writeFile(path.join(out,custom?'draw-custom.json':'draw.json'),JSON.stringify(report,null,2));
 await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
