import {readFile,writeFile,mkdir} from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=path.resolve(import.meta.dirname,'..');
const lib=process.env.WEBCUDA_ROOT||'D:/cuda-webshader';
const {compile,serializableArtifact}=await import(pathToFileURL(path.join(lib,'src/compiler/compiler.js')));
const source=await readFile(path.join(root,'web/fixture.cu'),'utf8');
const artifact=compile(source,{entry:'render',workgroupSize:[8,8,1],includeNativeSource:true});
await mkdir(path.join(root,'web/generated'),{recursive:true});
await writeFile(path.join(root,'web/generated/fixture.json'),JSON.stringify(serializableArtifact(artifact),null,2));
await writeFile(path.join(root,'web/generated/fixture.wgsl'),artifact.wgsl);
// Keep the camera, material shading and tone mapping identical. Replace only
// analytic box intersection with OptiX triangle traversal for the RTX probe.
let optix=source.replace(/__global__ void render\([^\n]+\)\{/,`extern "C" __global__ void __raygen__main(){
 unsigned* pixels=params.pixels; const float* boxes=params.boxes; const float* camera=params.camera;
 int width=optixGetLaunchDimensions().x,height=optixGetLaunchDimensions().y;`)
.replace('int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;','int x=optixGetLaunchIndex().x,y=optixGetLaunchIndex().y;')
.replace(/ for\(int i=0;i<count;i\+\+\)\{[\s\S]*?\n \}/,` unsigned id=0,depth=0;
 optixTrace(params.scene,o,d,.0001f,10000.0f,0.0f,255,OPTIX_RAY_FLAG_NONE,0,1,0,id,depth);
 if(id){hit=(int)id-1;nearest=__uint_as_float(depth);}`)
.replace(/  for\(int i=0;i<count;i\+\+\)\{[\s\S]*?\n  \}/,`  unsigned blocked=0,unused=0;
  optixTrace(params.scene,start,l,.0001f,dist,0.0f,255,OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,0,1,0,blocked,unused);
  if(blocked)visibility=0.0f;`);
optix+='\nextern "C" __global__ void __miss__main(){optixSetPayload_0(0);}\nextern "C" __global__ void __closesthit__main(){optixSetPayload_0(optixGetPrimitiveIndex()/12+1);optixSetPayload_1(__float_as_uint(optixGetRayTmax()));}\n';
if(optix.includes('i<count'))throw Error('OptiX generation left an analytic traversal');
await writeFile(path.join(root,'web/generated/fixture-optix.cu'),optix);
console.log('Built WGSL, dual-backend artifact, and OptiX traversal variant.');
