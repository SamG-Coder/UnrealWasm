#include "Scene.h"
extern "C" {
static float Vertices[SampleBoxCount*36*3];
const float* scene_vertices() {
 const int triangles[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
 const int corners[24]={-1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1, -1,-1,1, 1,-1,1, 1,1,1, -1,1,1};
 for(int i=0;i<SampleBoxCount;i++)for(int v=0;v<36;v++)for(int a=0;a<3;a++)
   Vertices[(i*36+v)*3+a]=SampleBoxes[i*12+a]+.5f*SampleBoxes[i*12+3+a]*corners[triangles[v]*3+a];
 return Vertices;
}
int scene_vertex_count(){return SampleBoxCount*36;}
const float* scene_boxes() { return SampleBoxes; }
int scene_box_count() { return SampleBoxCount; }
const float* scene_camera() { return SampleCamera; }
const float* scene_light() { return SampleLight; }
int scene_version() { return 1; }
}
