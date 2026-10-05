#include "WebGPUDevice.h"
#include "WebGPUUniformData.h"
#include <limits>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "WebCudaShaderCode.h"
#include "WebCudaShaderFixture.h"
#include "MemcpyShaderFixtures.h"
#include "LumenIntegrateFixture.h"
#include "LumenIntegrateLayout.h"
#include "LumenIntegrateTables.h"
#include "ClearBufferFixture.h"
using namespace UEWebGPU;
static Device GPU;
struct Case {
    Buffer Upload, Staging;
    Texture Target;
    Readback Result;
    uint32_t Index;
    bool Rejections = false;
};
static std::unique_ptr<Case> Active;
struct LumenCase {
    Buffer Constants, Adaptive, Output, Staging;
    Readback Result;
};
static uint32_t LumenInput[33];
static ComputePipeline LumenPipeline;
static ComputePipeline LumenIntegratePipeline;
static ShaderCode LumenIntegrateCode;
static ShaderCode LumenCode;
static std::unique_ptr<LumenCase> Lumen;
struct MemcpyCase {
    Buffer Constants, Source, Destination, Staging;
    Readback Result;
    uint32_t Size, SourceOffset, DestinationOffset, Lanes;
};
static ShaderCode MemcpyCode[4];
static ComputePipeline MemcpyPipeline[4];
static std::unique_ptr<MemcpyCase> Copy;
struct TextureCase {
    Texture Resource;
    Buffer Staging;
    Readback Result;
    uint32_t Format, Mip, FirstLayer, LayerCount;
};
static std::unique_ptr<TextureCase> TextureTest;
struct IntegrateCase {
    Buffer Root,View,BlueNoise,Tiles,Adaptive,RWAdaptive;
    Texture Textures[18];
    TextureView Views[18];
    Sampler PointClamp;
    Buffer Staging[3];
    Readback Results[3];
};
static std::unique_ptr<IntegrateCase> Integrate;
struct UniformCase {
    Buffer Constants[2],Staging[2];
    Readback Results[2];
    std::vector<uint8_t> Expected[2];
};
static std::unique_ptr<UniformCase> UniformTest;
struct ClearCase {
    Buffer Constants,Target,Staging;
    Readback Result;
    uint32_t First,Count,Value;
};
static std::unique_ptr<ClearCase> ClearTest;
static ShaderCode ClearCode;
static ComputePipeline ClearPipeline;
struct TextureClearCase {
    Texture Target;
    Buffer Staging[3];Readback Results[3];
    uint32_t Format,Mip,First,Layers;
    uint8_t Cleared[16]{},Guard[16]{};
};
static std::unique_ptr<TextureClearCase> TextureClear;
static constexpr WGPUTextureFormat TextureFormats[]={WGPUTextureFormat_R8Unorm,WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_R32Uint,WGPUTextureFormat_RGBA16Float,WGPUTextureFormat_RGBA32Float,WGPUTextureFormat_RG11B10Ufloat};
extern "C" {
void gpu_init() { GPU.Initialize(); }
int gpu_state() { return static_cast<int>(GPU.GetState()); }
const char* gpu_error() { return GPU.GetError().c_str(); }
int gpu_texture_caps() { return (GPU.HasTextureFormatsTier1()?1:0)|(GPU.HasRG11B10Renderable()?2:0); }
int gpu_texture_clear_run(uint32_t Format,uint32_t Mip,uint32_t First,uint32_t Layers,uint32_t ValueCase) {
    if(Format>=6 || Mip>=3 || First>=3 || !Layers || Layers>3-First || ValueCase>=3)return 0;
    if(TextureClear)for(const auto& R:TextureClear->Results)if(!R.Complete)return 0;
    TextureClear=std::make_unique<TextureClearCase>();auto& C=*TextureClear;
    C.Format=Format;C.Mip=Mip;C.First=First;C.Layers=Layers;
    WGPUColor Clear,Guard;
    if(Format<=1) {
        const uint8_t Values[3][4]={{0,0,0,0},{128,64,255,255},{255,255,255,255}},Guards[]={64,96,128,255};
        std::memcpy(C.Cleared,Values[ValueCase],4);std::memcpy(C.Guard,Guards,4);
        // Avoid half-integer UNORM quantization ties: 0.5 maps to 127.5/255
        // and render-target clear rounding differs from integer expectation.
        const WGPUColor Colors[]={{0,0,0,0},{128.0/255,64.0/255,2,1},{1,1,1,1}};
        Clear=Colors[ValueCase];Guard={64.0/255,96.0/255,128.0/255,1};
    }else if(Format==2) {
        const uint32_t Values[]={0,0x80000001u,0xffffffffu},Sentinel=17;
        std::memcpy(C.Cleared,&Values[ValueCase],4);std::memcpy(C.Guard,&Sentinel,4);
        Clear={double(Values[ValueCase]),0,0,0};Guard={17,0,0,0};
    }else {
        const float Values[3][4]={{0,0,0,0},{0.5f,0.25f,2,1},{1,1,1,1}},Guards[]={4,2,1,1};
        Clear={Values[ValueCase][0],Values[ValueCase][1],Values[ValueCase][2],Values[ValueCase][3]};Guard={4,2,1,1};
        if(Format==3) {
            const uint16_t Half[3][4]={{0,0,0,0},{0x3800,0x3400,0x4000,0x3c00},{0x3c00,0x3c00,0x3c00,0x3c00}},HalfGuard[]={0x4400,0x4000,0x3c00,0x3c00};
            std::memcpy(C.Cleared,Half[ValueCase],8);std::memcpy(C.Guard,HalfGuard,8);
        }else if(Format==4) {
            std::memcpy(C.Cleared,Values[ValueCase],16);std::memcpy(C.Guard,Guards,16);
        }else {
            const uint32_t Packed[]={0,(14u<<6)|((13u<<6)<<11)|((16u<<5)<<22),(15u<<6)|((15u<<6)<<11)|((15u<<5)<<22)};
            const uint32_t PackedGuard=(17u<<6)|((16u<<6)<<11)|((15u<<5)<<22);
            std::memcpy(C.Cleared,&Packed[ValueCase],4);std::memcpy(C.Guard,&PackedGuard,4);
        }
    }
    if(!GPU.CreateTexture2D(C.Target,9,5,3,3,TextureFormats[Format],WGPUTextureUsage_StorageBinding|WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc))return 0;
    for(uint32_t M=0;M<3;++M)if(!GPU.ClearTextureLayers(C.Target,Guard,M,0,3))return 0;
    if(GPU.ClearTextureLayers(C.Target,Clear,3,0,1) || GPU.ClearTextureLayers(C.Target,Clear,0,3,1) ||
       GPU.ClearTextureLayers(C.Target,Clear,0,2,2) || GPU.ClearTextureLayers(C.Target,Clear,0,0,0) ||
       GPU.ClearTextureLayers(C.Target,{std::numeric_limits<double>::quiet_NaN(),0,0,0},0,0,1))return 0;
    if(!GPU.ClearTextureLayers(C.Target,Clear,Mip,First,Layers))return 0;
    for(uint32_t M=0;M<3;++M) {
        uint32_t Bytes=256*std::max(1u,5u>>M)*3;
        if(!GPU.CreateBuffer(C.Staging[M],Bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead) ||
           !GPU.CopyTextureToBuffer(C.Target,C.Staging[M],256,M,0,3) || !GPU.ReadBuffer(C.Staging[M],Bytes,C.Results[M]))return 0;
    }
    return 1;
}
int gpu_texture_clear_done() { if(!TextureClear)return 0;for(const auto& R:TextureClear->Results)if(!R.Complete)return 0;return 1; }
int gpu_texture_clear_passed() {
    if(!gpu_texture_clear_done() || GPU.GetState()!=Device::State::Ready)return 0;
    const auto& C=*TextureClear;
    for(uint32_t M=0;M<3;++M) {
        uint32_t Width=std::max(1u,9u>>M),Height=std::max(1u,5u>>M),Bpp=C.Target.BytesPerTexel;
        const auto& R=C.Results[M];if(!R.Succeeded || R.Bytes.size()!=256*Height*3)return 0;
        for(uint32_t L=0;L<3;++L)for(uint32_t Y=0;Y<Height;++Y)for(uint32_t Byte=0;Byte<256;++Byte) {
            const auto* Pixel=M==C.Mip && L>=C.First && L<C.First+C.Layers?C.Cleared:C.Guard;
            uint8_t Expected=Byte<Width*Bpp?Pixel[Byte%Bpp]:0;
            if(R.Bytes[(L*Height+Y)*256+Byte]!=Expected) {
                std::printf("Texture clear mismatch: mip=%u layer=%u row=%u byte=%u expected=%u actual=%u\n",M,L,Y,Byte,unsigned(Expected),unsigned(R.Bytes[(L*Height+Y)*256+Byte]));return 0;
            }
        }
    }
    return 1;
}
int gpu_debug_begin() {
    std::string Long(1025,'x');
    if(GPU.PopDebugGroup() || GPU.PushDebugGroup(nullptr) || GPU.PushDebugGroup(Long.c_str()))return 0;
    for(uint32_t I=0;I<128;++I)if(!GPU.PushDebugGroup("depth check"))return 0;
    if(GPU.PushDebugGroup("too deep"))return 0;
    for(uint32_t I=0;I<128;++I)if(!GPU.PopDebugGroup())return 0;
    return GPU.PushDebugGroup("Unreal WebGPU commands") && GPU.PushDebugGroup("Original engine shaders");
}
int gpu_debug_end() { return GPU.PopDebugGroup() && GPU.PopDebugGroup() && !GPU.PopDebugGroup(); }
int gpu_clear_run(uint32_t First,uint32_t Count,uint32_t Value) {
    if(uint64_t(First)+Count>256 || (ClearTest&&!ClearTest->Result.Complete))return 0;
    if(!ClearPipeline.Handle) {
        std::string Error;
        if(!ShaderCode::Decode(ClearBufferFixture,sizeof(ClearBufferFixture),ClearCode,Error) ||
           !GPU.CreateComputePipeline(ClearPipeline,ClearCode.WGSL.c_str(),ClearCode.Entry.c_str()))return 0;
    }
    ClearTest=std::make_unique<ClearCase>();auto& C=*ClearTest;C.First=First;C.Count=Count;C.Value=Value;
    std::vector<uint8_t> Source(ClearCode.SourceConstantBytes),Packed;
    const uint32_t Min[]={First,0,0,0},Max[]={First+Count,1,1,0};
    std::memcpy(Source.data()+ClearBufferLayout::ClearValue,&Value,4);
    std::memcpy(Source.data()+ClearBufferLayout::MinBounds,Min,16);
    std::memcpy(Source.data()+ClearBufferLayout::MaxBounds,Max,16);
    uint32_t Initial[384];for(auto& Word:Initial)Word=0xcdcdcdcd;
    if(!ClearCode.PackConstants(Source.data(),Source.size(),Packed) ||
       !GPU.CreateBuffer(C.Constants,Packed.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.Target,sizeof(Initial),WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc) ||
       !GPU.CreateBuffer(C.Staging,sizeof(Initial),WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst) ||
       !GPU.WriteBuffer(C.Constants,0,Packed.data(),Packed.size()) || !GPU.WriteBuffer(C.Target,0,Initial,sizeof(Initial)))return 0;
    ResourceBinding Bindings[]={{0,&C.Constants},{ClearBufferLayout::ClearResource,&C.Target,nullptr,nullptr,256,1024}};
    if(!GPU.Dispatch(ClearPipeline,Bindings,2,std::max(1u,(First+Count+63)/64),1,1) || !GPU.CopyBuffer(C.Target,C.Staging,sizeof(Initial)))return 0;
    return GPU.ReadBuffer(C.Staging,sizeof(Initial),C.Result);
}
int gpu_clear_done() { return ClearTest&&ClearTest->Result.Complete; }
int gpu_clear_passed() {
    if(!gpu_clear_done() || !ClearTest->Result.Succeeded || ClearTest->Result.Bytes.size()!=1536 || GPU.GetState()!=Device::State::Ready)return 0;
    const auto& C=*ClearTest;
    for(uint32_t I=0;I<384;++I) {
        uint32_t Actual;std::memcpy(&Actual,C.Result.Bytes.data()+I*4,4);
        if(Actual!=((I>=64+C.First && I<64+C.First+C.Count)?C.Value:0xcdcdcdcd))return 0;
    }
    return 1;
}
int gpu_uniform_run() {
    if(UniformTest)for(const auto& R:UniformTest->Results)if(!R.Complete)return 0;
    UniformTest=std::make_unique<UniformCase>();auto& C=*UniformTest;
    uint8_t Source[61];uint32_t Offsets[]={8,32,64};
    for(uint32_t Version=0;Version<2;++Version) {
        for(uint32_t I=0;I<61;++I)Source[I]=uint8_t(I*7+Version*19);
        std::vector<uint8_t> Packed;
        if(!PackUniformData(Source,61,Offsets,3,Packed) || Packed.size()!=64)return 0;
        C.Expected[Version].resize(64);
        for(uint32_t I=0;I<64;++I)C.Expected[Version][I]=(I>=61 || (I>=8&&I<16) || (I>=32&&I<40))?0:uint8_t(I*7+Version*19);
        if(Packed!=C.Expected[Version])return 0;
        if(!GPU.CreateBuffer(C.Constants[Version],64,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc) ||
           !GPU.CreateBuffer(C.Staging[Version],64,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst) ||
           !GPU.WriteBuffer(C.Constants[Version],0,Packed.data(),64))return 0;
    }
    // Read the old version only after uploading the replacement: updates must
    // preserve bytes referenced by an earlier command.
    for(uint32_t I=0;I<2;++I)if(!GPU.CopyBuffer(C.Constants[I],C.Staging[I],64) || !GPU.ReadBuffer(C.Staging[I],64,C.Results[I]))return 0;
    std::vector<uint8_t> Sentinel={17,29,43},Out=Sentinel;
    auto Reject=[&](const void* Data,uint32_t Bytes,const uint32_t* Ptr,size_t Count){return !PackUniformData(Data,Bytes,Ptr,Count,Out)&&Out==Sentinel;};
    uint32_t Duplicate[]={8,8},Unaligned=9,Partial=56;
    if(!Reject(nullptr,61,Offsets,3) || !Reject(Source,65537,nullptr,0) || !Reject(Source,61,nullptr,1) ||
       !Reject(Source,61,Offsets,8193) || !Reject(Source,61,Duplicate,2) || !Reject(Source,61,&Unaligned,1) || !Reject(Source,61,&Partial,1))return 0;
    if(!PackUniformData(nullptr,0,nullptr,0,Out) || !Out.empty())return 0;
    return 1;
}
int gpu_uniform_done() { return UniformTest&&UniformTest->Results[0].Complete&&UniformTest->Results[1].Complete; }
int gpu_uniform_passed() {
    if(!gpu_uniform_done() || GPU.GetState()!=Device::State::Ready)return 0;
    for(uint32_t I=0;I<2;++I)if(!UniformTest->Results[I].Succeeded || UniformTest->Results[I].Bytes!=UniformTest->Expected[I])return 0;
    return 1;
}
int gpu_sampler_checks() {
    auto Desc=WGPU_SAMPLER_DESCRIPTOR_INIT;
    Desc.addressModeU=WGPUAddressMode_Repeat;Desc.addressModeV=WGPUAddressMode_MirrorRepeat;Desc.addressModeW=WGPUAddressMode_ClampToEdge;
    Desc.minFilter=Desc.magFilter=WGPUFilterMode_Linear;Desc.mipmapFilter=WGPUMipmapFilterMode_Linear;
    Desc.lodMinClamp=1;Desc.lodMaxClamp=5;
    Sampler Regular,Comparison,Anisotropic;
    if(!GPU.CreateSampler(Regular,Desc) || GPU.CreateSampler(Regular,Desc))return 0;
    Desc.compare=WGPUCompareFunction_Less;
    if(!GPU.CreateSampler(Comparison,Desc))return 0;
    Desc.compare=WGPUCompareFunction_Undefined;Desc.maxAnisotropy=16;
    if(!GPU.CreateSampler(Anisotropic,Desc))return 0;
    auto Reject=[&](auto Invalid){Sampler S;return !GPU.CreateSampler(S,Invalid)&&!S.Handle;};
    auto Bad=Desc;Bad.maxAnisotropy=0;if(!Reject(Bad))return 0;
    Bad=Desc;Bad.maxAnisotropy=17;if(!Reject(Bad))return 0;
    Bad=Desc;Bad.minFilter=WGPUFilterMode_Nearest;if(!Reject(Bad))return 0;
    Bad=Desc;Bad.lodMinClamp=-1;if(!Reject(Bad))return 0;
    Bad=Desc;Bad.lodMaxClamp=0;if(!Reject(Bad))return 0;
    Bad=Desc;Bad.lodMaxClamp=std::numeric_limits<float>::infinity();if(!Reject(Bad))return 0;
    Bad=Desc;Bad.lodMinClamp=std::numeric_limits<float>::quiet_NaN();if(!Reject(Bad))return 0;
    Bad=Desc;Bad.addressModeU=WGPUAddressMode_Undefined;if(!Reject(Bad))return 0;
    Bad=Desc;Bad.compare=WGPUCompareFunction_Always;if(!Reject(Bad))return 0;
    return 1;
}
int gpu_integrate_compile() {
    if(!GPU.HasTextureFormatsTier1() || GPU.GetState()!=Device::State::Ready) return 0;
    if(LumenIntegratePipeline.Handle) return 1;
    std::string Error;
    if(!ShaderCode::Decode(LumenIntegrateContainer,sizeof(LumenIntegrateContainer),LumenIntegrateCode,Error) || LumenIntegrateCode.Version!=2)return 0;
    std::vector<uint8_t> Encoded;
    if(!LumenIntegrateCode.EncodeV2(Encoded,Error) || Encoded.size()!=sizeof(LumenIntegrateContainer) || std::memcmp(Encoded.data(),LumenIntegrateContainer,Encoded.size()))return 0;
    ShaderCode Invalid;
    for(size_t Cut:{size_t(0),size_t(47),size_t(63),size_t(64),size_t(65),sizeof(LumenIntegrateContainer)-1})
        if(ShaderCode::Decode(LumenIntegrateContainer,Cut,Invalid,Error))return 0;
    auto Corrupt=[&](size_t Offset,uint32_t Value){
        auto Bad=Encoded;for(unsigned I=0;I<4;++I)Bad[Offset+I]=uint8_t(Value>>(I*8));
        return !ShaderCode::Decode(Bad.data(),Bad.size(),Invalid,Error);
    };
    const size_t TableStart=64+LumenIntegrateCode.Resources.size()*32+LumenIntegrateCode.Mappings.size()*12+LumenIntegrateCode.ResourceTableLayoutHashes.size()*4;
    if(!Corrupt(60,1) || !Corrupt(32,65) || !Corrupt(48,0) || !Corrupt(52,33) || !Corrupt(56,65) ||
       !Corrupt(64,64) || !Corrupt(68,7) || !Corrupt(72,1) || !Corrupt(88,4) ||
       !Corrupt(TableStart,32) || !Corrupt(TableStart+4,65536) || !Corrupt(TableStart+8,63) || !Corrupt(TableStart+12,6))return 0;
    return GPU.CreateComputePipeline(LumenIntegratePipeline,LumenIntegrateCode.WGSL.c_str(),LumenIntegrateCode.Entry.c_str());
}
int gpu_integrate_run() {
    if(!gpu_integrate_compile() || !GPU.HasRG11B10Renderable()) return 0;
    if(Integrate)for(const auto& Result:Integrate->Results)if(!Result.Complete)return 0;
    Integrate=std::make_unique<IntegrateCase>();auto& C=*Integrate;
    std::vector<uint8_t> Root(IntegrateLayout::Root::Bytes),View(IntegrateLayout::View::Bytes),BlueNoise(IntegrateLayout::BlueNoise::Bytes);
    auto Write=[](auto& Bytes,uint32_t Offset,const auto& Value){std::memcpy(Bytes.data()+Offset,&Value,sizeof(Value));};
    const uint32_t Min[2]={1,1},Size[2]={7,5},TileSize[2]={1,1},Rect[4]={0,0,9,7};
    const float SizeAndInv[4]={9,7,1.0f/9,1.0f/7},ZTransform[4]={0,1,0,1};
    Write(Root,IntegrateLayout::Root::IntegrateViewMin,Min);
    Write(Root,IntegrateLayout::Root::IntegrateViewSize,Size);
    Write(Root,IntegrateLayout::Root::ViewportTileDimensionsWithOverflow,TileSize);
    Write(View,IntegrateLayout::View::View_ViewRectMinAndSize,Rect);
    Write(View,IntegrateLayout::View::View_BufferSizeAndInvSize,SizeAndInv);
    Write(View,IntegrateLayout::View::View_ViewSizeAndInvSize,SizeAndInv);
    Write(View,IntegrateLayout::View::View_InvDeviceZToWorldZTransform,ZTransform);
    // Disabled bindless slots remain zero. Actual textures are separate bindings.
    if(!GPU.CreateBuffer(C.Root,Root.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.View,View.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.BlueNoise,BlueNoise.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
       !GPU.WriteBuffer(C.Root,0,Root.data(),Root.size()) || !GPU.WriteBuffer(C.View,0,View.data(),View.size()) ||
       !GPU.WriteBuffer(C.BlueNoise,0,BlueNoise.data(),BlueNoise.size())) return 0;
    if(!GPU.CreateBuffer(C.Tiles,16,WGPUBufferUsage_Storage) || !GPU.CreateBuffer(C.Adaptive,16,WGPUBufferUsage_Storage)) return 0;
    if(!GPU.CreateSampler(C.PointClamp,WGPUAddressMode_ClampToEdge,WGPUFilterMode_Nearest,WGPUMipmapFilterMode_Nearest))return 0;
    std::vector<ResourceBinding> Bindings={{0,&C.Root},{1,&C.View},{13,&C.BlueNoise},{30,&C.Tiles},{17,&C.Adaptive},{12,nullptr,nullptr,&C.PointClamp}};
    // Separate SRV and UAV resources avoid read/write aliasing within a pass.
    // GBuffer B's zero shading-model bits select Unreal's original unlit path.
    constexpr uint32_t Slots[]={2,3,4,5,14,15,16,18,19,20,28,29,21,22,23,25,26,27};
    for(uint32_t I=0;I<18;++I) {
        if(I>=12 && I<=14)continue; // Removed unreachable UAV reads.
        bool Integer=I==6 || I==7 || I==8 || (I>=12&&I<=14),Output=I>=15;
        auto Format=Integer?WGPUTextureFormat_R32Uint:WGPUTextureFormat_RGBA8Unorm;
        if(Output)Format=I==16?WGPUTextureFormat_R8Unorm:WGPUTextureFormat_RG11B10Ufloat;
        auto Usage=WGPUTextureUsage_RenderAttachment|(I>=12?WGPUTextureUsage_StorageBinding:WGPUTextureUsage_TextureBinding);
        if(Output)Usage|=WGPUTextureUsage_CopySrc;
        if(!GPU.CreateTexture2D(C.Textures[I],9,7,Output?2:1,1,Format,Usage) ||
           !GPU.CreateTextureView(C.Views[I],C.Textures[I],0,1,0,Output?2:1,Output))return 0;
        for(uint32_t Layer=0;Layer<(Output?2u:1u);++Layer)
            if(!GPU.ClearTexture(C.Textures[I],Output?WGPUColor{1,1,1,1}:WGPUColor{0,0,0,0},0,Layer))return 0;
        Bindings.push_back({Slots[I],nullptr,&C.Views[I]});
    }
    auto Bad=Bindings;
    Bad[0].TextureResource=&C.Views[0];if(GPU.Dispatch(LumenIntegratePipeline,Bad.data(),Bad.size(),1,1,1))return 0;
    Bad=Bindings;Bad[0].BufferOffset=4;if(GPU.Dispatch(LumenIntegratePipeline,Bad.data(),Bad.size(),1,1,1))return 0;
    Bad=Bindings;Bad[0].BufferBytes=C.Root.Size+4;if(GPU.Dispatch(LumenIntegratePipeline,Bad.data(),Bad.size(),1,1,1))return 0;
    Bad=Bindings;Bad.back().Binding=0;if(GPU.Dispatch(LumenIntegratePipeline,Bad.data(),Bad.size(),1,1,1))return 0;
    Bad=Bindings;Bad.back().BufferBytes=4;if(GPU.Dispatch(LumenIntegratePipeline,Bad.data(),Bad.size(),1,1,1))return 0;
    if(!GPU.Dispatch(LumenIntegratePipeline,Bindings.data(),Bindings.size(),0,1,1) ||
       GPU.Dispatch(LumenIntegratePipeline,Bindings.data(),Bindings.size(),0xffffffff,1,1)) return 0;
    // Resource indices and hashes come from original UE metadata, independently
    // of the cooked shader table. The dispatcher resolves the table itself.
    std::vector<ResourceBinding> SceneTable(IntegrateTables::SceneTexturesStruct::Count),NoiseTable(IntegrateTables::BlueNoise::Count),Direct;
    SceneTable[IntegrateTables::SceneTexturesStruct::SceneDepthTexture]={0,nullptr,&C.Views[0]};
    SceneTable[IntegrateTables::SceneTexturesStruct::GBufferATexture]={0,nullptr,&C.Views[1]};
    SceneTable[IntegrateTables::SceneTexturesStruct::GBufferBTexture]={0,nullptr,&C.Views[2]};
    SceneTable[IntegrateTables::SceneTexturesStruct::GBufferCTexture]={0,nullptr,&C.Views[3]};
    SceneTable[IntegrateTables::SceneTexturesStruct::PointClampSampler]={0,nullptr,nullptr,&C.PointClamp};
    NoiseTable[IntegrateTables::BlueNoise::ScalarTexture]={0,nullptr,&C.Views[4]};
    NoiseTable[IntegrateTables::BlueNoise::Vec2Texture]={0,nullptr,&C.Views[5]};
    UniformResourceTable Tables[]={{2,IntegrateTables::SceneTexturesStruct::Hash,SceneTable.data(),SceneTable.size()},
                                  {13,IntegrateTables::BlueNoise::Hash,NoiseTable.data(),NoiseTable.size()}};
    for(const auto& Binding:Bindings) {
        bool InTable=false;for(const auto& T:LumenIntegrateCode.ResourceTableBindings)if(T.Binding==Binding.Binding)InTable=true;
        if(!InTable)Direct.push_back(Binding);
    }
    auto Dispatch=[&](){return GPU.DispatchShader(LumenIntegratePipeline,LumenIntegrateCode,Direct.data(),Direct.size(),Tables,2,1,1,1);};
    Tables[0].LayoutHash^=1;if(Dispatch())return 0;Tables[0].LayoutHash^=1;
    Tables[0].Count=1;if(Dispatch())return 0;Tables[0].Count=SceneTable.size();
    Tables[1].Slot=2;if(Dispatch())return 0;Tables[1].Slot=13;
    if(GPU.DispatchShader(LumenIntegratePipeline,LumenIntegrateCode,Direct.data(),Direct.size(),Tables,1,1,1,1))return 0;
    auto GoodView=Direct.back().TextureResource;Direct.back().TextureResource=&C.Views[0];if(Dispatch())return 0;Direct.back().TextureResource=GoodView;
    std::vector<uint8_t> PackedRoot;
    if(!LumenIntegrateCode.PackConstants(Root.data(),668,PackedRoot) || PackedRoot.size()!=Root.size() ||
       !GPU.WriteBuffer(C.Root,0,PackedRoot.data(),PackedRoot.size()) || !Dispatch())return 0;
    for(uint32_t I=0;I<3;++I) {
        if(!GPU.CreateBuffer(C.Staging[I],256*7*2,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead) ||
           !GPU.CopyTextureToBuffer(C.Textures[I+15],C.Staging[I],256,0,0,2) ||
           !GPU.ReadBuffer(C.Staging[I],256*7*2,C.Results[I]))return 0;
    }
    return 1;
}
int gpu_integrate_done() {
    if(!Integrate)return 0;
    for(const auto& Result:Integrate->Results)if(!Result.Complete)return 0;
    return 1;
}
int gpu_integrate_passed() {
    if(!gpu_integrate_done() || GPU.GetState()!=Device::State::Ready)return 0;
    const uint32_t PackedOne=(15u<<6)|((15u<<6)<<11)|((15u<<5)<<22);
    for(uint32_t I=0;I<3;++I) {
        const auto& Result=Integrate->Results[I];if(!Result.Succeeded)return 0;
        uint32_t Bpp=I==1?1:4;
        for(uint32_t Layer=0;Layer<2;++Layer)for(uint32_t Y=0;Y<7;++Y)for(uint32_t Byte=0;Byte<256;++Byte) {
            uint32_t X=Byte/Bpp;
            bool Written=Layer==0 && X>=1 && X<8 && Y>=1 && Y<6;
            uint8_t Expected=Byte>=9*Bpp || Written?0:I==1?255:uint8_t(PackedOne>>((Byte%4)*8));
            if(Result.Bytes[(Layer*7+Y)*256+Byte]!=Expected)return 0;
        }
    }
    return 1;
}
int gpu_texture_run(uint32_t Format,uint32_t Mip,uint32_t FirstLayer) {
    if(Format>=6 || Mip>=3 || FirstLayer>=3 || GPU.GetState()!=Device::State::Ready || (TextureTest&&!TextureTest->Result.Complete)) return 0;
    TextureTest=std::make_unique<TextureCase>(); auto& C=*TextureTest;
    C.Format=Format; C.Mip=Mip; C.FirstLayer=FirstLayer; C.LayerCount=3-FirstLayer;
    WGPUTextureUsage Usage=WGPUTextureUsage_CopySrc|WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding;
    if((Format!=0&&Format!=5)||GPU.HasTextureFormatsTier1()) Usage|=WGPUTextureUsage_StorageBinding;
    if(!GPU.CreateTexture2D(C.Resource,9,5,3,3,TextureFormats[Format],Usage)) return 0;
    TextureView Array,Single,Invalid;
    if(!GPU.CreateTextureView(Array,C.Resource,0,3,0,3,true) || !GPU.CreateTextureView(Single,C.Resource,2,1,2,1,false) ||
       GPU.CreateTextureView(Invalid,C.Resource,0,1,0,2,false) || GPU.CreateTextureView(Invalid,C.Resource,2,2,0,1,true) ||
       GPU.CreateTextureView(Invalid,C.Resource,0,1,2,2,true) || GPU.CreateTextureView(Invalid,C.Resource,0,0,0,1,false)) return 0;
    for(uint32_t M=0;M<3;++M)for(uint32_t L=0;L<3;++L) {
        const uint32_t Value=1+53*M+17*L;
        WGPUColor Color={Value/255.0,(255-Value)/255.0,0,1};
        if(Format==2) Color={double(1000+Value),0,0,0};
        if(Format>=3) Color={double(1u<<M),double(1u<<L),1,1};
        if(!GPU.ClearTexture(C.Resource,Color,M,L)) return 0;
    }
    uint32_t Height=std::max(1u,5u>>Mip);
    if(!GPU.CreateBuffer(C.Staging,256*Height*C.LayerCount,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead)) return 0;
    if(GPU.CopyTextureToBuffer(C.Resource,C.Staging,255,Mip,FirstLayer,C.LayerCount) ||
       GPU.CopyTextureToBuffer(C.Resource,C.Staging,256,3,FirstLayer,C.LayerCount) ||
       GPU.CopyTextureToBuffer(C.Resource,C.Staging,256,Mip,2,2) ||
       GPU.ClearTexture(C.Resource,{0,0,0,0},0,3)) return 0;
    if(!GPU.CopyTextureToBuffer(C.Resource,C.Staging,256,Mip,FirstLayer,C.LayerCount)) return 0;
    return GPU.ReadBuffer(C.Staging,256*Height*C.LayerCount,C.Result);
}
int gpu_texture_done() { return TextureTest&&TextureTest->Result.Complete; }
int gpu_texture_passed() {
    if(!TextureTest||!TextureTest->Result.Complete||!TextureTest->Result.Succeeded||GPU.GetState()!=Device::State::Ready) return 0;
    auto& C=*TextureTest; uint32_t Width=std::max(1u,9u>>C.Mip),Height=std::max(1u,5u>>C.Mip),Bpp=C.Resource.BytesPerTexel;
    for(uint32_t L=0;L<C.LayerCount;++L) {
        const uint32_t Layer=L+C.FirstLayer,Value=1+53*C.Mip+17*Layer;
        uint8_t Pixel[16]={};
        if(C.Format==0) Pixel[0]=Value;
        if(C.Format==1) { Pixel[0]=Value;Pixel[1]=255-Value;Pixel[3]=255; }
        if(C.Format==2) { uint32_t V=1000+Value;std::memcpy(Pixel,&V,4); }
        if(C.Format==3) { uint16_t V[]={uint16_t((15+C.Mip)<<10),uint16_t((15+Layer)<<10),0x3c00,0x3c00};std::memcpy(Pixel,V,8); }
        if(C.Format==4) { float V[]={float(1u<<C.Mip),float(1u<<Layer),1,1};std::memcpy(Pixel,V,16); }
        if(C.Format==5) { uint32_t V=((15+C.Mip)<<6)|(((15+Layer)<<6)<<11)|((15u<<5)<<22);std::memcpy(Pixel,&V,4); }
        for(uint32_t Y=0;Y<Height;++Y)for(uint32_t X=0;X<256;++X) {
            const uint8_t Expected=X<Width*Bpp?Pixel[X%Bpp]:0;
            if(C.Result.Bytes[(L*Height+Y)*256+X]!=Expected) return 0;
        }
    }
    return 1;
}
int gpu_texture_rejections() {
    Texture Invalid; const auto Usage=WGPUTextureUsage_TextureBinding;
    if(GPU.CreateTexture2D(Invalid,0,5,1,1,WGPUTextureFormat_R8Unorm,Usage) ||
       GPU.CreateTexture2D(Invalid,9,5,0,1,WGPUTextureFormat_R8Unorm,Usage) ||
       GPU.CreateTexture2D(Invalid,9,5,1,5,WGPUTextureFormat_R8Unorm,Usage) ||
       GPU.CreateTexture2D(Invalid,9,5,1,1,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_StorageBinding) ||
       GPU.CreateTexture2D(Invalid,0xffffffff,5,1,1,WGPUTextureFormat_R8Unorm,Usage)) return 0;
    return 1;
}
int gpu_shader_rejections() {
    ShaderCode Code; std::string Error;
    if(!ShaderCode::Decode(ShaderFixture,sizeof(ShaderFixture),Code,Error)) return -1;
    int Rejections=0;
    // Truncation at every byte, including all record and string boundaries.
    for(size_t N=0;N<sizeof(ShaderFixture);++N) {
        if(ShaderCode::Decode(ShaderFixture,N,Code,Error)) return -2;
        ++Rejections;
    }
    auto Corrupt=[&](size_t Offset,uint32_t Value) {
        std::vector<uint8_t> Data(ShaderFixture,ShaderFixture+sizeof(ShaderFixture));
        for(size_t I=0;I<4;++I) Data[Offset+I]=uint8_t(Value>>(I*8));
        if(ShaderCode::Decode(Data.data(),Data.size(),Code,Error)) return false;
        ++Rejections; return true;
    };
    if(!Corrupt(0,0) || !Corrupt(4,2) || !Corrupt(8,0) || !Corrupt(12,0) || !Corrupt(20,1025) ||
       !Corrupt(24,0xffffffff) || !Corrupt(28,0xffffffff) || !Corrupt(32,0xffffffff) ||
       !Corrupt(36,0xffffffff) || !Corrupt(40,0xffffffff) || !Corrupt(44,0xffffffff) ||
       !Corrupt(48,16) || !Corrupt(52,3) || !Corrupt(56,0) || !Corrupt(72,0xffffffff) ||
       !Corrupt(76,0xffffffff) || !Corrupt(80,0) || !Corrupt(88,0)) return -3;
    std::vector<uint8_t> Packed;
    if(Code.PackConstants(nullptr,128,Packed) || Code.PackConstants(LumenInput,123,Packed)) return -4;
    if(!Code.PackConstants(LumenInput,124,Packed) || Packed.size()!=32) return -5;
    return Rejections+2;
}
int gpu_run(uint32_t Index) {
    if (GPU.GetState() != Device::State::Ready || (Active && !Active->Result.Complete)) return 0;
    Active = std::make_unique<Case>(); auto& C = *Active; C.Index = Index;
    if (Index == 0) {
        std::vector<uint32_t> Input(1024); for(uint32_t i=0;i<Input.size();++i)Input[i]=i*1664525u+1013904223u;
        if (!GPU.CreateBuffer(C.Upload,4096,WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc) ||
            !GPU.CreateBuffer(C.Staging,4096,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead))return 0;
        C.Rejections = !GPU.WriteBuffer(C.Upload,4092,Input.data(),8) && !GPU.WriteBuffer(C.Upload,1,Input.data(),4) && !GPU.CopyBuffer(C.Upload,C.Staging,4100) && !GPU.CopyBuffer(C.Upload,C.Upload,4);
        if(!GPU.WriteBuffer(C.Upload,0,Input.data(),4096) || !GPU.CopyBuffer(C.Upload,C.Staging,4096))return 0;
        return GPU.ReadBuffer(C.Staging,4096,C.Result);
    }
    if (Index > 3)return 0;
    if(!GPU.CreateRGBA8Texture(C.Target,3,2) || !GPU.CreateBuffer(C.Staging,512,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead))return 0;
    C.Rejections = !GPU.CopyTextureToBuffer(C.Target,C.Staging,12) && !GPU.CopyTextureToBuffer(C.Target,C.Staging,512);
    const WGPUColor Color = {Index==1?1.0:0.0,Index==2?1.0:0.0,Index==3?1.0:0.0,1.0};
    if(!GPU.ClearTexture(C.Target,Color) || !GPU.CopyTextureToBuffer(C.Target,C.Staging,256))return 0;
    return GPU.ReadBuffer(C.Staging,512,C.Result);
}
int gpu_done() { return Active && Active->Result.Complete; }
int gpu_passed() {
    if(!Active || !Active->Result.Complete || !Active->Result.Succeeded || !Active->Rejections || GPU.GetState()!=Device::State::Ready)return 0;
    auto& C=*Active;
    if(C.Index==0) {
        const auto* Values=reinterpret_cast<const uint32_t*>(C.Result.Bytes.data());
        for(uint32_t i=0;i<1024;++i)if(Values[i]!=i*1664525u+1013904223u)return 0;
    } else {
        for(uint32_t y=0;y<2;++y)for(uint32_t x=0;x<3;++x)for(uint32_t k=0;k<4;++k)
            if(C.Result.Bytes[y*256+x*4+k]!=((k==3 || k==C.Index-1)?255:0))return 0;
    }
    return 1;
}
uint32_t* gpu_lumen_input() { return LumenInput; }
int gpu_lumen_run() {
    if (GPU.GetState()!=Device::State::Ready || (Lumen && !Lumen->Result.Complete))return 0;
    if (!LumenPipeline.Handle) {
        std::string Error;
        if(!ShaderCode::Decode(ShaderFixture,sizeof(ShaderFixture),LumenCode,Error) ||
           !GPU.CreateComputePipeline(LumenPipeline,LumenCode.WGSL.c_str(),LumenCode.Entry.c_str())) return 0;
        if(LumenCode.Resources.size()!=3 || LumenCode.Resources[0].Kind!=0 || LumenCode.Resources[1].Kind!=1 || LumenCode.Resources[2].Kind!=2) return 0;
    }
    Lumen=std::make_unique<LumenCase>();auto& C=*Lumen;
    std::vector<uint8_t> Packed;
    if(!LumenCode.PackConstants(LumenInput,124,Packed)) return 0;
    if(!GPU.CreateBuffer(C.Constants,Packed.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.Adaptive,4,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.Output,80,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.Staging,80,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead))return 0;
    uint32_t Sentinels[20];for(auto& Word:Sentinels)Word=0xcdcdcdcd;
    if(!GPU.WriteBuffer(C.Constants,0,Packed.data(),Packed.size()) || !GPU.WriteBuffer(C.Adaptive,0,LumenInput+32,4) ||
       !GPU.WriteBuffer(C.Output,0,Sentinels,80))return 0;
    BufferBinding Bindings[]={{LumenCode.Resources[0].Binding,&C.Constants},{LumenCode.Resources[1].Binding,&C.Adaptive},{LumenCode.Resources[2].Binding,&C.Output}};
    if(!GPU.Dispatch(LumenPipeline,Bindings,3,1,1,1) || !GPU.CopyBuffer(C.Output,C.Staging,80))return 0;
    return GPU.ReadBuffer(C.Staging,80,C.Result);
}
int gpu_lumen_done() { return Lumen && Lumen->Result.Complete; }
const void* gpu_lumen_output() { return Lumen && Lumen->Result.Succeeded ? Lumen->Result.Bytes.data() : nullptr; }
int gpu_memcpy_run(uint32_t Mode,uint32_t Size,uint32_t SourceOffset,uint32_t DestinationOffset) {
    if(Mode>4 || GPU.GetState()!=Device::State::Ready || (Copy&&!Copy->Result.Complete) ||
       uint64_t(Size)+SourceOffset>256 || uint64_t(Size)+DestinationOffset>256) return 0;
    const bool Indirect=Mode==4;if(Indirect)Mode=0;
    auto& Code=MemcpyCode[Mode]; auto& Pipeline=MemcpyPipeline[Mode];
    if(!Pipeline.Handle) {
        std::string Error;
        const uint8_t* Data[]={RootMemcpyFixture,LegacyMemcpyFixture,Uint2MemcpyFixture,Uint4MemcpyFixture};
        size_t Bytes[]={sizeof(RootMemcpyFixture),sizeof(LegacyMemcpyFixture),sizeof(Uint2MemcpyFixture),sizeof(Uint4MemcpyFixture)};
        if(!ShaderCode::Decode(Data[Mode],Bytes[Mode],Code,Error) || !GPU.CreateComputePipeline(Pipeline,Code.WGSL.c_str(),Code.Entry.c_str())) return 0;
        if(Code.Resources.size()!=3 || Code.Resources[0].Kind!=0 || Code.Resources[1].Kind!=1 || Code.Resources[2].Kind!=2) return 0;
    }
    Copy=std::make_unique<MemcpyCase>(); auto& C=*Copy;
    C.Size=Size; C.SourceOffset=SourceOffset; C.DestinationOffset=DestinationOffset;
    C.Lanes=Mode==2?2:Mode==3?4:1;
    // FRDGByteBufferShader's actual FParameters includes Value before Size.
    uint32_t RootParameters[]={0xdeadbeef,Size,SourceOffset,DestinationOffset};
    uint32_t LegacyParameters[]={Size,SourceOffset,DestinationOffset};
    std::vector<uint8_t> Packed;
    if(!Code.PackConstants(Mode==1?LegacyParameters:RootParameters,Mode==1?sizeof(LegacyParameters):sizeof(RootParameters),Packed)) return 0;
    std::vector<uint32_t> Source(256*C.Lanes),Destination(256*C.Lanes,0xcdcdcdcd);
    for(uint32_t I=0;I<Source.size();++I) Source[I]=I*1664525u+1013904223u;
    const size_t Bytes=Source.size()*sizeof(uint32_t);
    if(!GPU.CreateBuffer(C.Constants,Packed.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.Source,Bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst) ||
       !GPU.CreateBuffer(C.Destination,Bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc) ||
       !GPU.CreateBuffer(C.Staging,Bytes,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst)) return 0;
    if(!GPU.WriteBuffer(C.Constants,0,Packed.data(),Packed.size()) || !GPU.WriteBuffer(C.Source,0,Source.data(),Bytes) ||
       !GPU.WriteBuffer(C.Destination,0,Destination.data(),Bytes)) return 0;
    BufferBinding Bindings[]={{Code.Resources[0].Binding,&C.Constants},{Code.Resources[1].Binding,&C.Source},{Code.Resources[2].Binding,&C.Destination}};
    if(Indirect) {
        // Unreal's original copy shader produces arguments on the GPU, then
        // another invocation consumes them without a CPU readback or wait.
        Buffer ArgumentSource,Arguments,ArgumentConstants;
        const uint32_t Groups[]={Size?(Size+63)/64:0,1,1,0};
        const uint32_t CopyArguments[]={0,3,0,4};
        std::vector<uint8_t> ArgumentData;
        if(!Code.PackConstants(CopyArguments,sizeof(CopyArguments),ArgumentData) ||
           !GPU.CreateBuffer(ArgumentSource,16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst) ||
           !GPU.CreateBuffer(Arguments,32,WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect) ||
           !GPU.CreateBuffer(ArgumentConstants,ArgumentData.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
           !GPU.WriteBuffer(ArgumentSource,0,Groups,sizeof(Groups)) || !GPU.WriteBuffer(ArgumentConstants,0,ArgumentData.data(),ArgumentData.size()))return 0;
        BufferBinding Generate[]={{Code.Resources[0].Binding,&ArgumentConstants},{Code.Resources[1].Binding,&ArgumentSource},{Code.Resources[2].Binding,&Arguments}};
        if(!GPU.Dispatch(Pipeline,Generate,3,1,1,1))return 0;
        ResourceBinding Run[]={{Code.Resources[0].Binding,&C.Constants},{Code.Resources[1].Binding,&C.Source},{Code.Resources[2].Binding,&C.Destination}};
        if(GPU.DispatchIndirect(Pipeline,Run,3,Arguments,2) || GPU.DispatchIndirect(Pipeline,Run,3,Arguments,24) ||
           GPU.DispatchIndirect(Pipeline,Run,3,Arguments,UINT64_MAX-3) || GPU.DispatchIndirect(Pipeline,Run,3,C.Source,0))return 0;
        if(!GPU.DispatchIndirect(Pipeline,Run,3,Arguments,16))return 0;
    } else if(!GPU.Dispatch(Pipeline,Bindings,3,(Size+63)/64,1,1))return 0;
    if(!GPU.CopyBuffer(C.Destination,C.Staging,Bytes))return 0;
    return GPU.ReadBuffer(C.Staging,Bytes,C.Result);
}
int gpu_memcpy_done() { return Copy&&Copy->Result.Complete; }
int gpu_memcpy_passed() {
    if(!Copy || !Copy->Result.Complete || !Copy->Result.Succeeded || Copy->Result.Bytes.size()!=1024*Copy->Lanes || GPU.GetState()!=Device::State::Ready) return 0;
    const auto& C=*Copy; const auto* Values=reinterpret_cast<const uint32_t*>(C.Result.Bytes.data());
    for(uint32_t I=0;I<256*C.Lanes;++I) {
        uint32_t Expected=(I>=C.DestinationOffset*C.Lanes && I<(C.DestinationOffset+C.Size)*C.Lanes)?((I-C.DestinationOffset*C.Lanes+C.SourceOffset*C.Lanes)*1664525u+1013904223u):0xcdcdcdcd;
        if(Values[I]!=Expected) return 0;
    }
    return 1;
}
}
