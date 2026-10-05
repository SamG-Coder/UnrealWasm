#include "WebGPUDevice.h"
#include "ScreenFixtures.h"
#include <memory>
#include <cstdio>
#include <cstring>
using namespace UEWebGPU;
static Device GPU;
static RasterShader VS,PS;
static RenderPipeline Pipelines[17];
struct DrawCase {
    Texture Source,Target,Depth;
    TextureView SourceView,TargetView,DepthView,DepthSampleView;
    Buffer Upload,Vertices,Indices,Constants,SecondConstants,Staging;
    Sampler Point;
    Readback Result;
    uint32_t X,Y,W,H,Flip,StateCase;
};
static std::unique_ptr<DrawCase> Active;
static uint8_t Pixel(uint32_t X,uint32_t Y,uint32_t Channel) {
    return Channel==0?uint8_t(X*31):Channel==1?uint8_t(Y*29):Channel==2?uint8_t((X+Y)*17):255;
}
extern "C" {
void raster_init(){GPU.Initialize();}
int raster_state(){return int(GPU.GetState());}
const char* raster_error(){return GPU.GetError().c_str();}
int raster_run(uint32_t Mode,uint32_t Rectangle,uint32_t Flip,uint32_t StateCase) {
    if(Mode>2 || Rectangle>1 || Flip>1 || StateCase>16 || (StateCase&&(Rectangle||Flip)) || (Active&&!Active->Result.Complete))return 0;
    auto& Pipeline=Pipelines[StateCase];
    if(!VS.Handle) {
        ShaderCode V,P;std::string Error;
        if(!ShaderCode::Decode(Screen_vs,sizeof(Screen_vs),V,Error) || !ShaderCode::Decode(Screen_ps,sizeof(Screen_ps),P,Error))return 0;
        // Stage header failures must reject before reaching the GPU.
        for(const auto& Pair:{std::pair<const uint8_t*,size_t>{Screen_vs,sizeof(Screen_vs)},{Screen_ps,sizeof(Screen_ps)}}) {
            for(uint32_t Word:{1u,2u,3u,4u,5u}) {
                std::vector<uint8_t> Bad(Pair.first,Pair.first+Pair.second);Bad[Word*4]=Word==1?2:1;
                ShaderCode Check;if(ShaderCode::Decode(Bad.data(),Bad.size(),Check,Error))return 0;
            }
        }
        if(!GPU.CreateRasterShader(VS,V) || !GPU.CreateRasterShader(PS,P))return 0;
    }
    if(!Pipeline.Handle) {
        WGPUVertexAttribute Attributes[2]={WGPU_VERTEX_ATTRIBUTE_INIT,WGPU_VERTEX_ATTRIBUTE_INIT};
        Attributes[0].format=Attributes[1].format=WGPUVertexFormat_Float32x2;Attributes[1].offset=8;Attributes[1].shaderLocation=1;
        auto Layout=WGPU_VERTEX_BUFFER_LAYOUT_INIT;Layout.arrayStride=16;Layout.attributeCount=2;Layout.attributes=Attributes;
        auto Target=WGPU_COLOR_TARGET_STATE_INIT;Target.format=WGPUTextureFormat_RGBA8Unorm;
        auto Fragment=WGPU_FRAGMENT_STATE_INIT;Fragment.targetCount=1;Fragment.targets=&Target;
        auto State=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;State.vertex.bufferCount=1;State.vertex.buffers=&Layout;State.fragment=&Fragment;
        State.primitive.topology=WGPUPrimitiveTopology_TriangleList;
        auto DS=WGPU_DEPTH_STENCIL_STATE_INIT;
        if(StateCase>=3 && StateCase<=7) {
            DS.format=StateCase<=4?WGPUTextureFormat_Depth32Float:WGPUTextureFormat_Depth24PlusStencil8;
            DS.depthWriteEnabled=WGPUOptionalBool_False;DS.depthCompare=StateCase==4?WGPUCompareFunction_Greater:WGPUCompareFunction_Less;
            if(StateCase>=5){DS.stencilFront.compare=DS.stencilBack.compare=WGPUCompareFunction_Equal;DS.stencilWriteMask=0;}
            State.depthStencil=&DS;
        }
        auto Blend=WGPU_BLEND_STATE_INIT;
        if(StateCase==8)Target.writeMask=WGPUColorWriteMask_Red;
        if(StateCase==9) {
            Blend.color.srcFactor=Blend.alpha.srcFactor=WGPUBlendFactor_Constant;
            Blend.color.dstFactor=Blend.alpha.dstFactor=WGPUBlendFactor_OneMinusConstant;Target.blend=&Blend;
        }
        if(StateCase==10||StateCase==11){State.primitive.cullMode=WGPUCullMode_Back;State.primitive.frontFace=StateCase==10?WGPUFrontFace_CCW:WGPUFrontFace_CW;}
        if(GPU.CreateRenderPipeline(Pipeline,PS,VS,State) || !GPU.CreateRenderPipeline(Pipeline,VS,PS,State))return 0;
    }
    Active=std::make_unique<DrawCase>();auto& C=*Active;
    C.X=Rectangle?3:0;C.Y=Rectangle?2:0;C.W=Rectangle?8:16;C.H=Rectangle?7:12;C.Flip=Flip;C.StateCase=StateCase;
    if(!GPU.CreateTexture2D(C.Source,8,8,1,1,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_CopyDst|WGPUTextureUsage_TextureBinding) ||
       !GPU.CreateTexture2D(C.Target,16,12,1,1,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc) ||
       !GPU.CreateTextureView(C.SourceView,C.Source,0,1,0,1,false) || !GPU.CreateTextureView(C.TargetView,C.Target,0,1,0,1,false) ||
       !GPU.CreateSampler(C.Point,WGPUAddressMode_ClampToEdge,WGPUFilterMode_Nearest,WGPUMipmapFilterMode_Nearest))return 0;
    uint8_t Pixels[256*8]{};
    for(uint32_t Y=0;Y<8;++Y)for(uint32_t X=0;X<8;++X)for(uint32_t K=0;K<4;++K)Pixels[Y*256+X*4+K]=Pixel(X,Y,K);
    if(!GPU.CreateBuffer(C.Upload,sizeof(Pixels),WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc) ||
       !GPU.WriteBuffer(C.Upload,0,Pixels,sizeof(Pixels)) || !GPU.CopyBufferToTexture(C.Upload,C.Source,256))return 0;
    const float Vertices[]={99,99,99,99, 99,99,99,99, 0,0,0,0, 1,0,1,0, 1,1,1,1, 0,1,0,1, 0,0,0,0, 1,1,1,1};
    // One prefix vertex is skipped by the binding offset; another by First or BaseVertex.
    // Non-indexed order is 0,1,2 / 3,0,2, matching the same quad.
    const float Constants[]={float(C.W),float(C.H),float(C.X),float(C.Y), Flip?-8.f:8.f,8,Flip?8.f:0.f,0, 1.f/16,1.f/12,1.f/8,1.f/8};
    if(!GPU.CreateBuffer(C.Vertices,sizeof(Vertices),WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst) || !GPU.WriteBuffer(C.Vertices,0,Vertices,sizeof(Vertices)) ||
       !GPU.CreateBuffer(C.Constants,sizeof(Constants),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) || !GPU.WriteBuffer(C.Constants,0,Constants,sizeof(Constants)))return 0;
    ResourceBinding VB[]={{0,&C.Constants}},PB[]={{0,nullptr,&C.SourceView},{1,nullptr,nullptr,&C.Point}};
    StageBindings Vertex{VB,1},Fragment{PB,2};VertexBufferBinding Buffer{&C.Vertices,16};
    auto Attachment=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;Attachment.view=C.TargetView.Handle;Attachment.loadOp=WGPULoadOp_Clear;
    Attachment.storeOp=WGPUStoreOp_Store;Attachment.clearValue={17./255,34./255,51./255,1};
    auto Pass=WGPU_RENDER_PASS_DESCRIPTOR_INIT;Pass.colorAttachmentCount=1;Pass.colorAttachments=&Attachment;
    auto DepthAttachment=WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    if(StateCase>=3 && StateCase<=7) {
        const auto Format=StateCase<=4?WGPUTextureFormat_Depth32Float:WGPUTextureFormat_Depth24PlusStencil8;
        if(!GPU.CreateTexture2D(C.Depth,16,12,1,1,Format,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding) ||
           !GPU.CreateTextureView(C.DepthView,C.Depth,0,1,0,1,false) || !GPU.CreateTextureView(C.DepthSampleView,C.Depth,0,1,0,1,false,WGPUTextureAspect_DepthOnly))return 0;
        if(GPU.ClearTexture(C.Depth,{1,1,1,1}))return 0;
        DepthAttachment.view=C.DepthView.Handle;DepthAttachment.depthLoadOp=WGPULoadOp_Clear;DepthAttachment.depthStoreOp=WGPUStoreOp_Store;DepthAttachment.depthClearValue=1;
        if(StateCase>=5){DepthAttachment.stencilLoadOp=WGPULoadOp_Clear;DepthAttachment.stencilStoreOp=WGPUStoreOp_Store;DepthAttachment.stencilClearValue=23;}
        Pass.depthStencilAttachment=&DepthAttachment;
    }
    DrawState DrawState;
    if(StateCase==1){DrawState.HasViewport=true;DrawState.X=4;DrawState.Y=3;DrawState.Width=8;DrawState.Height=6;}
    if(StateCase==2){DrawState.HasScissor=true;DrawState.ScissorX=5;DrawState.ScissorY=4;DrawState.ScissorWidth=4;DrawState.ScissorHeight=3;}
    DrawState.StencilReference=StateCase==6?22:23;DrawState.BlendConstant={0,1,0,1};
    DrawArguments Args;Args.Count=6;Args.First=1;
    if(Mode) {
        uint16_t I16[]={99,99,0,1,2,0,2,3};uint32_t I32[]={99,99,0,1,2,0,2,3};
        size_t Bytes=Mode==1?sizeof(I16):sizeof(I32);
        if(!GPU.CreateBuffer(C.Indices,Bytes,WGPUBufferUsage_Index|WGPUBufferUsage_CopyDst) || !GPU.WriteBuffer(C.Indices,0,Mode==1?static_cast<void*>(I16):static_cast<void*>(I32),Bytes))return 0;
        Args.Indices=&C.Indices;Args.IndexFormat=Mode==1?WGPUIndexFormat_Uint16:WGPUIndexFormat_Uint32;Args.First=2;Args.BaseVertex=1;
        auto Bad=Args;Bad.Count=7;if(GPU.Draw(Pipeline,Pass,Vertex,Fragment,&Buffer,1,Bad))return 0;
    }
    StageBindings Missing{};if(GPU.Draw(Pipeline,Pass,Missing,Fragment,&Buffer,1,Args))return 0;
    auto BadBuffer=Buffer;BadBuffer.Offset=2;if(GPU.Draw(Pipeline,Pass,Vertex,Fragment,&BadBuffer,1,Args))return 0;
    auto BadPixel=Fragment;BadPixel.Count=1;if(GPU.Draw(Pipeline,Pass,Vertex,BadPixel,&Buffer,1,Args))return 0;
    if(StateCase>=12) {
        RenderPass ActivePass;
        if(!GPU.BeginRenderPass(ActivePass,Pass,16,12) || GPU.BeginRenderPass(ActivePass,Pass,16,12))return 0;
        auto FirstState=DrawState;FirstState.HasScissor=true;FirstState.ScissorWidth=8;FirstState.ScissorHeight=12;
        if(StateCase==14){FirstState.HasScissor=false;FirstState.HasViewport=true;FirstState.X=4;FirstState.Y=3;FirstState.Width=8;FirstState.Height=6;}
        if(StateCase!=15 && !GPU.DrawInRenderPass(ActivePass,Pipeline,Vertex,Fragment,&Buffer,1,Args,FirstState))return 0;
        if(StateCase==16) {
            if(!GPU.EndRenderPass(ActivePass))return 0;
            Attachment.loadOp=WGPULoadOp_Load;
            if(!GPU.BeginRenderPass(ActivePass,Pass,16,12))return 0;
        }
        float Second[12];std::memcpy(Second,Constants,sizeof(Second));
        if(StateCase==12||StateCase==16){Second[4]=-8;Second[6]=8;}
        if(!GPU.CreateBuffer(C.SecondConstants,sizeof(Second),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
           !GPU.WriteBuffer(C.SecondConstants,0,Second,sizeof(Second)))return 0;
        ResourceBinding SecondVB[]={{0,&C.SecondConstants}};StageBindings SecondVertex{SecondVB,1};
        auto SecondState=DrawState;
        if(StateCase==12||StateCase==16){SecondState.HasScissor=true;SecondState.ScissorX=8;SecondState.ScissorWidth=8;SecondState.ScissorHeight=12;}
        if(StateCase==14){SecondState.HasScissor=true;SecondState.ScissorWidth=4;SecondState.ScissorHeight=12;}
        const auto& SecondPipeline=StateCase==13?Pipelines[8]:Pipeline;
        if(StateCase!=15 && !GPU.DrawInRenderPass(ActivePass,SecondPipeline,SecondVertex,Fragment,&Buffer,1,Args,SecondState))return 0;
        if(!GPU.EndRenderPass(ActivePass) || GPU.EndRenderPass(ActivePass))return 0;
        if(GPU.DrawInRenderPass(ActivePass,Pipeline,Vertex,Fragment,&Buffer,1,Args,DrawState))return 0;
    }else if(!GPU.Draw(Pipeline,Pass,Vertex,Fragment,&Buffer,1,Args,DrawState))return 0;
    if(StateCase==1){C.X=4;C.Y=3;C.W=8;C.H=6;}
    if(!GPU.CreateBuffer(C.Staging,256*12,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead) || !GPU.CopyTextureToBuffer(C.Target,C.Staging,256))return 0;
    return GPU.ReadBuffer(C.Staging,256*12,C.Result);
}
int raster_done(){return Active&&Active->Result.Complete;}
int raster_passed(){
    if(!Active || !Active->Result.Succeeded || GPU.GetState()!=Device::State::Ready || Active->Result.Bytes.size()!=256*12)return 0;
    auto& C=*Active;
    for(uint32_t Y=0;Y<12;++Y)for(uint32_t B=0;B<256;++B) {
        uint32_t X=B/4,K=B%4;uint8_t Expected=0;
        if(X<16) {
            const uint8_t Background[]={17,34,51,255};Expected=Background[K];
            const bool Clip=C.StateCase==2 && (X<5||X>=9||Y<4||Y>=7);
            const bool Rejected=C.StateCase==4||C.StateCase==6||C.StateCase==10||C.StateCase==15;
            const bool Masked=(C.StateCase==8&&K!=0)||(C.StateCase==9&&(K==0||K==2))||(C.StateCase==13&&X>=8&&K!=0);
            if(!Clip && !Rejected && !Masked && X>=C.X && X<C.X+C.W && Y>=C.Y && Y<C.Y+C.H) {
                uint32_t SX=((X-C.X)*2+1)*8/(C.W*2),SY=((Y-C.Y)*2+1)*8/(C.H*2);
                if(C.Flip || ((C.StateCase==12||C.StateCase==16)&&X>=8))SX=7-SX;Expected=Pixel(SX,SY,K);
                if(C.StateCase==14 && X>=4) {
                    Expected=Background[K];
                    if(X<12 && Y>=3 && Y<9)Expected=Pixel(X-4,((Y-3)*2+1)*8/12,K);
                }
            }
        }
        auto Actual=C.Result.Bytes[Y*256+B];if(Actual!=Expected){std::printf("Raster mismatch x=%u y=%u channel=%u got=%u expected=%u\n",X,Y,K,Actual,Expected);return 0;}
    }
    return 1;
}
}
