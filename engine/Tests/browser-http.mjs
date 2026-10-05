import assert from 'node:assert/strict';
import http from 'node:http';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {chromium} from 'file:///D:/cuda-webshader/node_modules/playwright/index.mjs';
const out=new URL('../artifacts/browser-http/',import.meta.url);
const timers=new Set();
const server=http.createServer(async(req,res)=>{
 try {
  if(req.url==='/') {res.setHeader('Content-Type','text/html');return res.end('<!doctype html><title>WebCuda HTTP validation</title>');}
  if(['/http.mjs','/http.wasm'].includes(req.url)) {
   res.setHeader('Content-Type',req.url.endsWith('.wasm')?'application/wasm':'text/javascript');
   return res.end(await readFile(new URL(req.url.slice(1),out)));
  }
  if(req.url==='/echo') {
   const chunks=[];for await(const chunk of req)chunks.push(chunk);
   res.setHeader('x-case',req.headers['x-case']||'none');res.setHeader('content-type','application/octet-stream');
   return res.end(Buffer.concat(chunks));
  }
  if(req.url==='/missing'){res.writeHead(404);return res.end('missing');}
  if(req.url==='/redirect'){res.writeHead(302,{Location:'/utf8'});return res.end();}
  if(req.url==='/utf8'){res.setHeader('Content-Type','text/plain; charset=utf-8');return res.end('Unreal → Wasm ✓');}
  if(req.url==='/head'){res.setHeader('Content-Length','123');return res.end();}
  if(req.url==='/large')return res.end(Buffer.alloc(1024,97));
  if(req.url==='/drop')return req.socket.destroy();
  if(req.url==='/slow'){
   const timer=setTimeout(()=>{timers.delete(timer);res.end('late');},400);timers.add(timer);return;
  }
  if(req.url==='/stream'){
   res.writeHead(200,{'Content-Type':'application/octet-stream'});res.write(Buffer.alloc(100,1));
   const timer=setTimeout(()=>{timers.delete(timer);res.end(Buffer.alloc(200,2));},200);timers.add(timer);return;
  }
  res.writeHead(404);res.end();
 }catch(error){res.destroy(error);}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin=`http://127.0.0.1:${server.address().port}`;
const report={date:new Date().toISOString(),fullEngine:false,engineRequestObjectExecuted:false,checks:[]};
let browser;
try {
 browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 report.browser=browser.version();const page=await browser.newPage();
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(origin);
 await page.evaluate(async()=>{const {default:create}=await import('/http.mjs');globalThis.transport=await create();});
 const start=async index=>page.evaluate(i=>transport._http_start(i),index);
 const finish=async id=>{
  await page.waitForFunction(id=>transport._http_poll(id)===1,id,{timeout:10000});
  return page.evaluate(id=>({
   error:transport._http_error(id),status:transport._http_status(id),
   url:transport.UTF8ToString(Number(transport._http_url(id))),headers:transport.UTF8ToString(Number(transport._http_headers(id))),
   body:Array.from(transport.HEAPU8.subarray(Number(transport._http_body(id)),Number(transport._http_body(id))+transport._http_size(id)))
  }),id);
 };
 const release=async id=>page.evaluate(id=>transport._http_forget(id),id);
 let id=await start(0),result=await finish(id);
 assert.equal(result.error,0);assert.equal(result.status,200);assert.match(result.headers,/x-case: binary/);
 assert.deepEqual(result.body,Array.from({length:8192},(_,i)=>(i*37+11)&255));await release(id);
 report.checks.push('POST copies 8192 binary bytes and custom headers before source storage expires');
 id=await start(1);result=await finish(id);assert.equal(result.error,0);assert.equal(result.status,404);
 assert.equal(Buffer.from(result.body).toString(),'missing');await release(id);
 report.checks.push('HTTP 404 is a completed server response, not a connection failure');
 for(const index of [2,11]){
  id=await start(index);result=await finish(id);assert.equal(result.error,0);
  assert.equal(result.url,origin+'/utf8');assert.equal(Buffer.from(result.body).toString(),'Unreal → Wasm ✓');await release(id);
 }
 report.checks.push('redirect effective URL and UTF-8 response bytes');
 for(const index of [3,8]){id=await start(index);result=await finish(id);assert.equal(result.error,3);await release(id);}
 report.checks.push('total timeout and activity timeout');
 id=await start(4);await page.evaluate(id=>transport._http_cancel(id),id);
 result=await finish(id);assert.equal(result.error,2);await release(id);
 await page.waitForTimeout(450);assert.equal(await page.evaluate(()=>transport._http_active()),0);
 report.checks.push('cancellation is terminal; late Fetch completion cannot revive a released job');
 id=await start(5);await page.waitForFunction(id=>transport._http_progress(id)===100,id);
 const heapBefore=await page.evaluate(()=>transport.HEAPU8.length);
 assert.equal(await page.evaluate(()=>transport._http_grow()),37);
 assert.ok(await page.evaluate(()=>transport.HEAPU8.length)>heapBefore);
 result=await finish(id);assert.equal(result.error,0);assert.deepEqual(result.body,[...Array(100).fill(1),...Array(200).fill(2)]);await release(id);
 report.checks.push('incremental download progress and ordered chunks across Wasm memory growth');
 for(const [index,error] of [[6,5],[7,1],[9,4],[10,4]]){
  id=await start(index);result=await finish(id);assert.equal(result.error,error);await release(id);
 }
 report.checks.push('response byte limit, dropped connection, unsupported URL and invalid header errors');
 id=await start(12);result=await finish(id);assert.equal(result.status,200);assert.equal(result.error,0);assert.equal(result.body.length,0);await release(id);
 report.checks.push('HEAD response has no body despite nonzero Content-Length');
 const jobs=await Promise.all(Array.from({length:12},()=>start(11)));
 for(const job of jobs){assert.equal((await finish(job)).error,0);await release(job);}
 for(let i=0;i<20;i++){id=await start(4);await release(id);}
 await page.waitForTimeout(450);
 assert.equal(await page.evaluate(()=>transport._http_active()),0);assert.deepEqual(errors,[]);
 report.checks.push('12 concurrent requests and 20 abandoned jobs clean up without late callbacks');
 report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;}
finally{
 report.wasmSHA256=createHash('sha256').update(await readFile(new URL('http.wasm',out))).digest('hex');
 await writeFile(new URL('validation.json',out),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
 await browser?.close();for(const timer of timers)clearTimeout(timer);
 server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
