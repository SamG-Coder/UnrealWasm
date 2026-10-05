import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {serve} from '../../scripts/serve.mjs';
import {chromium} from 'file:///D:/cuda-webshader/node_modules/playwright/index.mjs';
const {server,origin}=await serve();
const report={date:new Date().toISOString(),fullEngine:false,fullSlateUI:false};
let browser;
try{
 browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:false});report.browser=browser.version();
 const page=await browser.newPage();await page.goto(origin+'/engine-runtime/test.html');
 report.runtime=await page.evaluate(async()=>{
   const lines=[];const {default:create}=await import('/engine-slate/check.mjs');
   const module=await create({print:line=>lines.push(line)});const exitCode=module._slate_test();
   return {exitCode,lines};
 });
 assert.equal(report.runtime.exitCode,0);
 report.results=JSON.parse(report.runtime.lines.find(line=>line.startsWith('{')));delete report.runtime.lines;
 assert.equal(report.results.glyphs,12);assert.equal(report.results.arabicGlyphs,3);assert.equal(report.results.latinGlyphs,5);
 report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;}
finally{
 report.wasmSHA256=createHash('sha256').update(await readFile(new URL('../artifacts/slate-dependencies/check.wasm',import.meta.url))).digest('hex');
 await writeFile(new URL('../artifacts/slate-dependencies/validation.json',import.meta.url),JSON.stringify(report,null,2));
 console.log(JSON.stringify(report,null,2));await browser?.close();server.closeAllConnections();await new Promise(r=>server.close(r));
}
