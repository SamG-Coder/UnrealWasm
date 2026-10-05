import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {serve} from '../../scripts/serve.mjs';
import {chromium} from 'file:///D:/cuda-webshader/node_modules/playwright/index.mjs';
const {server,origin}=await serve();
const report={date:new Date().toISOString(),version:'64.1',fullEngine:false,engineCallbackExecuted:false};
let browser;
try{
 browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:false});
 report.browser=browser.version();
 const page=await browser.newPage();
 await page.goto(origin+'/engine-runtime/test.html');
 report.result=await page.evaluate(async()=>{
   const {default:create}=await import('/engine-icu/check.mjs');
   const module=await create();
   return {exitCode:module._icu_test(),missingData:module._icu_test_missing(),opened:module._icu_opened(),closed:module._icu_closed()};
 });
 assert.equal(report.result.exitCode,0);
 assert.equal(report.result.missingData,0);
 assert.ok(report.result.opened>0);
 assert.equal(report.result.opened,report.result.closed);
 report.checks=['Matching ICU64 data from engine installation','WasmFS file reads through UE-compatible callbacks',
   'en_US_POSIX resources','en_US decimal formatting','word boundaries','collation',
   'two init/cleanup cycles without outstanding callback buffers or tracked ICU allocations','missing locale data fails initialization',
   'reject callback replacement while mappings exist','reject incomplete callback pair'];
 report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;}
finally{
 report.wasmSHA256=createHash('sha256').update(await readFile(new URL('../artifacts/icu64/check.wasm',import.meta.url))).digest('hex');
 await writeFile(new URL('../artifacts/icu64/validation.json',import.meta.url),JSON.stringify(report,null,2));
 console.log(JSON.stringify(report,null,2));await browser?.close();server.closeAllConnections();await new Promise(r=>server.close(r));
}
