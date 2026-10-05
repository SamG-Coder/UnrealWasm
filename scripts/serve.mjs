import http from 'node:http';
import path from 'node:path';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
const root=path.resolve(import.meta.dirname,'..');
export async function serve(port=0){
 const server=http.createServer(async(req,res)=>{
  try{
   const url=new URL(req.url,'http://localhost');
   let base=path.join(root,'web'),relative=url.pathname.slice(1)||'index.html';
   if(url.pathname.startsWith('/webcuda/')){base=path.resolve(process.env.WEBCUDA_ROOT||'D:/cuda-webshader','src');relative=url.pathname.slice(9);}
   if(url.pathname.startsWith('/artifacts/')){base=path.join(root,'artifacts');relative=url.pathname.slice(11);}
   if(url.pathname.startsWith('/engine-runtime/')){base=path.join(root,'engine/Runtime');relative=url.pathname.slice(16);}
   if(url.pathname.startsWith('/engine-shaders/')){base=path.join(root,'engine/artifacts/exported');relative=url.pathname.slice(16);}
   if(['/engine-oodle/oodle.mjs','/engine-oodle/oodle.wasm'].includes(url.pathname)){base=path.join(root,'engine/artifacts/oodle');relative=path.basename(url.pathname);}
   if(['/engine-webgpu/resources.mjs','/engine-webgpu/resources.wasm'].includes(url.pathname)){base=path.join(root,'engine/artifacts/webgpu');relative=path.basename(url.pathname);}
   if(['/engine-raster/raster.mjs','/engine-raster/raster.wasm'].includes(url.pathname)){base=path.join(root,'engine/artifacts/raster-probe');relative=path.basename(url.pathname);}
   if(['/engine-platform/services.mjs','/engine-platform/services.wasm'].includes(url.pathname)){base=path.join(root,'engine/artifacts/platform-services');relative=path.basename(url.pathname);}
   if(['/engine-host/host.mjs','/engine-host/host.wasm'].includes(url.pathname)){base=path.join(root,'engine/artifacts/browser-host');relative=path.basename(url.pathname);}
   if(['/engine-icu/check.mjs','/engine-icu/check.wasm'].includes(url.pathname)){base=path.join(root,'engine/artifacts/icu64');relative=path.basename(url.pathname);}
   if(['/engine-slate/check.mjs','/engine-slate/check.wasm'].includes(url.pathname)){base=path.join(root,'engine/artifacts/slate-dependencies');relative=path.basename(url.pathname);}
   if(url.pathname==='/engine-bridge.wasm'){base=path.join(root,'Binaries/WebCuda');relative='WebCudaBridge.wasm';}
   const file=path.resolve(base,decodeURIComponent(relative));
   if(!file.startsWith(base+path.sep))throw Error('Invalid path');
   const content=await readFile(file);
   res.writeHead(200,{'Content-Type':{'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css','.json':'application/json','.wasm':'application/wasm','.png':'image/png','.cu':'text/plain'}[path.extname(file)]||'application/octet-stream','Cache-Control':'no-store','Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp'});res.end(content);
  }catch{res.writeHead(404);res.end('Not found');}
 });
 await new Promise(resolve=>server.listen(port,'127.0.0.1',resolve));
 return {server,origin:`http://127.0.0.1:${server.address().port}`};
}
if(import.meta.url===pathToFileURL(process.argv[1]).href){const {origin}=await serve(Number(process.env.PORT||8088));console.log('Sample: '+origin);}
