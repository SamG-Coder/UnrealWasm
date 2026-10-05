// Identical source for WGSL/WebGPU and native CUDA. This is a capability renderer,
// not Unreal's renderer, Nanite, or Lumen. Analytic boxes, hard shadows, one light.
__device__ float boxhit(float3 o,float3 d,float3 c,float3 s){
 float ax=(c.x-s.x*.5f-o.x)/d.x, bx=(c.x+s.x*.5f-o.x)/d.x;
 float ay=(c.y-s.y*.5f-o.y)/d.y, by=(c.y+s.y*.5f-o.y)/d.y;
 float az=(c.z-s.z*.5f-o.z)/d.z, bz=(c.z+s.z*.5f-o.z)/d.z;
 float lo=fmaxf(fmaxf(fminf(ax,bx),fminf(ay,by)),fminf(az,bz));
 float hi=fminf(fminf(fmaxf(ax,bx),fmaxf(ay,by)),fmaxf(az,bz));
 return hi>=fmaxf(lo,0.0f)?(lo>0.0f?lo:hi):10000.0f;
}
__device__ float3 unit(float3 v){float s=rsqrtf(v.x*v.x+v.y*v.y+v.z*v.z);return make_float3(v.x*s,v.y*s,v.z*s);}
__device__ float dot3(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
__device__ unsigned channel(float x){return (unsigned)(255.0f*powf(fmaxf(0.0f,x)/(1.0f+fmaxf(0.0f,x)),1.0f/2.2f));}
__global__ void render(unsigned* pixels,const float* boxes,const float* camera,int width,int height,int count){
 int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
 if(x>=width||y>=height)return;
 float3 o=make_float3(camera[0],camera[1],camera[2]);
 float3 f=unit(make_float3(camera[3]-o.x,camera[4]-o.y,camera[5]-o.z));
 // Unreal's left-handed X-forward/Y-right/Z-up camera convention.
 float3 r=unit(make_float3(-f.y,f.x,0.0f));
 float3 u=make_float3(-f.z*r.y,f.z*r.x,f.x*r.y-f.y*r.x);
 float sx=((float(x)+.5f)/float(width)*2.0f-1.0f)*.577350269f;
 float sy=(1.0f-(float(y)+.5f)/float(height)*2.0f)*.577350269f*float(height)/float(width);
 float3 d=unit(make_float3(f.x+r.x*sx+u.x*sy,f.y+r.y*sx+u.y*sy,f.z+r.z*sx+u.z*sy));
 float nearest=10000.0f;int hit=-1;
 for(int i=0;i<count;i++){
  int j=i*12;float t=boxhit(o,d,make_float3(boxes[j],boxes[j+1],boxes[j+2]),make_float3(boxes[j+3],boxes[j+4],boxes[j+5]));
  if(t<nearest){nearest=t;hit=i;}
 }
 float red=.025f,green=.037f,blue=.06f;
 if(hit>=0){
  int j=hit*12;float3 p=make_float3(o.x+d.x*nearest,o.y+d.y*nearest,o.z+d.z*nearest);
  float nx=(p.x-boxes[j])/boxes[j+3],ny=(p.y-boxes[j+1])/boxes[j+4],nz=(p.z-boxes[j+2])/boxes[j+5];
  float3 n=make_float3(0,0,0);
  if(fabsf(nx)>fabsf(ny)&&fabsf(nx)>fabsf(nz))n.x=nx>0?1.0f:-1.0f;
  else if(fabsf(ny)>fabsf(nz))n.y=ny>0?1.0f:-1.0f;else n.z=nz>0?1.0f:-1.0f;
  float3 l=make_float3(camera[7]-p.x,camera[8]-p.y,camera[9]-p.z);
  float dist=sqrtf(dot3(l,l)); l=unit(l);
  float3 start=make_float3(p.x+n.x*.002f,p.y+n.y*.002f,p.z+n.z*.002f);
  float visibility=1.0f;
  for(int i=0;i<count;i++){
   int k=i*12;float t=boxhit(start,l,make_float3(boxes[k],boxes[k+1],boxes[k+2]),make_float3(boxes[k+3],boxes[k+4],boxes[k+5]));
   if(t<dist)visibility=0.0f;
  }
  float diffuse=fmaxf(0.0f,dot3(n,l))*visibility*100.0f/(dist*dist);
  float3 h=unit(make_float3(l.x-d.x,l.y-d.y,l.z-d.z));
  float spec=powf(fmaxf(0.0f,dot3(n,h)),4.0f+120.0f*(1.0f-boxes[j+9]))*visibility*(.15f+boxes[j+10])*3.0f;
  float ambient=.12f+.12f*fmaxf(n.z,0.0f);
  red=boxes[j+6]*(ambient+diffuse)+spec;green=boxes[j+7]*(ambient+diffuse)+spec;blue=boxes[j+8]*(ambient+diffuse)+spec;
 }
 pixels[y*width+x]=channel(red)|(channel(green)<<8)|(channel(blue)<<16)|4278190080u;
}
