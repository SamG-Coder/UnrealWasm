#include "WebGPURHIResources.h"
#include "WebGPUUniformData.h"
#include "RHIShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "ShaderCore.h"
#include "RHICoreShader.h"
#include "ClearReplacementShaders.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebGPUCompute,Log,All);

static void RequireWebGPUCommand(bool Accepted,const TCHAR* Command)
{
    if(!Accepted)UE_LOG(LogWebGPUCompute,Fatal,TEXT("WebGPU rejected unsupported or invalid %s"),Command);
}

static WGPUTextureFormat WebGPUFormat(EPixelFormat Format,bool SRGB=false)
{
    switch(Format) {
        case PF_R8:return SRGB?WGPUTextureFormat_Undefined:WGPUTextureFormat_R8Unorm;
        case PF_R8G8B8A8:return SRGB?WGPUTextureFormat_RGBA8UnormSrgb:WGPUTextureFormat_RGBA8Unorm;
        case PF_B8G8R8A8:return SRGB?WGPUTextureFormat_BGRA8UnormSrgb:WGPUTextureFormat_BGRA8Unorm;
        default:if(SRGB)return WGPUTextureFormat_Undefined;
    }
    switch(Format) {
        case PF_R32_UINT:return WGPUTextureFormat_R32Uint;
        case PF_FloatRGBA:return WGPUTextureFormat_RGBA16Float;
        case PF_A32B32G32R32F:return WGPUTextureFormat_RGBA32Float;
        case PF_FloatR11G11B10:return WGPUTextureFormat_RG11B10Ufloat;
        case PF_DepthStencil:return WGPUTextureFormat_Depth24PlusStencil8;
        case PF_ShadowDepth:return WGPUTextureFormat_Depth32Float;
        default:return WGPUTextureFormat_Undefined;
    }
}

static WGPUVertexFormat WebGPUVertexFormat(EVertexElementType Type,uint32& Bytes)
{
    switch(Type) {
#define VERTEX_CASE(UE,WG,Size) case UE:Bytes=Size;return WG
        VERTEX_CASE(VET_Float1,WGPUVertexFormat_Float32,4);
        VERTEX_CASE(VET_Float2,WGPUVertexFormat_Float32x2,8);
        VERTEX_CASE(VET_Float3,WGPUVertexFormat_Float32x3,12);
        VERTEX_CASE(VET_Float4,WGPUVertexFormat_Float32x4,16);
        VERTEX_CASE(VET_PackedNormal,WGPUVertexFormat_Snorm8x4,4);
        VERTEX_CASE(VET_UByte4,WGPUVertexFormat_Uint8x4,4);
        VERTEX_CASE(VET_UByte4N,WGPUVertexFormat_Unorm8x4,4);
        VERTEX_CASE(VET_Color,WGPUVertexFormat_Unorm8x4BGRA,4);
        VERTEX_CASE(VET_Short2,WGPUVertexFormat_Sint16x2,4);
        VERTEX_CASE(VET_Short4,WGPUVertexFormat_Sint16x4,8);
        VERTEX_CASE(VET_Short2N,WGPUVertexFormat_Snorm16x2,4);
        VERTEX_CASE(VET_Short4N,WGPUVertexFormat_Snorm16x4,8);
        VERTEX_CASE(VET_Half2,WGPUVertexFormat_Float16x2,4);
        VERTEX_CASE(VET_Half4,WGPUVertexFormat_Float16x4,8);
        VERTEX_CASE(VET_UShort2,WGPUVertexFormat_Uint16x2,4);
        VERTEX_CASE(VET_UShort4,WGPUVertexFormat_Uint16x4,8);
        VERTEX_CASE(VET_UShort2N,WGPUVertexFormat_Unorm16x2,4);
        VERTEX_CASE(VET_UShort4N,WGPUVertexFormat_Unorm16x4,8);
        VERTEX_CASE(VET_URGB10A2N,WGPUVertexFormat_Unorm10_10_10_2,4);
        VERTEX_CASE(VET_UInt,WGPUVertexFormat_Uint32,4);
#undef VERTEX_CASE
        default:Bytes=0;return WGPUVertexFormat(0);
    }
}
bool FWebGPUVertexDeclaration::Initialize(const FVertexDeclarationElementList& InElements)
{
    if(!Layouts.empty() || InElements.Num()>16)return false;
    std::array<int32,16> Slots;Slots.fill(-1);uint32 Locations=0;
    Attributes.reserve(16);Layouts.reserve(16);Streams.reserve(16);
    for(const auto& E:InElements) {
        uint32 Bytes=0;const auto Format=WebGPUVertexFormat(E.Type,Bytes);
        if(!Bytes || E.StreamIndex>=16 || E.AttributeIndex>=16 || E.Stride%4 || E.Stride>2048 ||
           E.Offset%FMath::Min(Bytes,4u) || (E.Stride&&uint32(E.Offset)+Bytes>E.Stride) || E.bUseInstanceIndex>1 || (Locations&(1u<<E.AttributeIndex)))return false;
        Locations|=1u<<E.AttributeIndex;
        int32& Slot=Slots[E.StreamIndex];
        if(Slot<0) {
            Slot=int32(Layouts.size());Attributes.emplace_back();Streams.push_back(E.StreamIndex);
            auto L=WGPU_VERTEX_BUFFER_LAYOUT_INIT;L.arrayStride=E.Stride;L.stepMode=E.bUseInstanceIndex?WGPUVertexStepMode_Instance:WGPUVertexStepMode_Vertex;
            Layouts.push_back(L);
        }
        if(Layouts[Slot].arrayStride!=E.Stride || (Layouts[Slot].stepMode==WGPUVertexStepMode_Instance)!=bool(E.bUseInstanceIndex))return false;
        auto A=WGPU_VERTEX_ATTRIBUTE_INIT;A.format=Format;A.offset=E.Offset;A.shaderLocation=E.AttributeIndex;Attributes[Slot].push_back(A);
    }
    // Dense WebGPU slots map back to original Unreal stream indices at draw time.
    for(size_t I=0;I<Layouts.size();++I){Layouts[I].attributeCount=Attributes[I].size();Layouts[I].attributes=Attributes[I].data();}
    Elements=InElements;return true;
}
TRefCountPtr<FWebGPUVertexDeclaration> CreateWebGPUVertexDeclaration(const FVertexDeclarationElementList& Elements)
{
    TRefCountPtr<FWebGPUVertexDeclaration> Result=new FWebGPUVertexDeclaration;
    return Result->Initialize(Elements)?Result:nullptr;
}
static WGPUCompareFunction WebGPUCompare(ECompareFunction Compare)
{
    switch(Compare) {
        case CF_Less:return WGPUCompareFunction_Less;case CF_LessEqual:return WGPUCompareFunction_LessEqual;
        case CF_Greater:return WGPUCompareFunction_Greater;case CF_GreaterEqual:return WGPUCompareFunction_GreaterEqual;
        case CF_Equal:return WGPUCompareFunction_Equal;case CF_NotEqual:return WGPUCompareFunction_NotEqual;
        case CF_Never:return WGPUCompareFunction_Never;case CF_Always:return WGPUCompareFunction_Always;
        default:return WGPUCompareFunction_Undefined;
    }
}
static WGPUStencilOperation WebGPUStencil(EStencilOp Op)
{
    switch(Op) {
        case SO_Keep:return WGPUStencilOperation_Keep;case SO_Zero:return WGPUStencilOperation_Zero;
        case SO_Replace:return WGPUStencilOperation_Replace;case SO_Invert:return WGPUStencilOperation_Invert;
        case SO_SaturatedIncrement:return WGPUStencilOperation_IncrementClamp;case SO_SaturatedDecrement:return WGPUStencilOperation_DecrementClamp;
        case SO_Increment:return WGPUStencilOperation_IncrementWrap;case SO_Decrement:return WGPUStencilOperation_DecrementWrap;
        default:return WGPUStencilOperation_Undefined;
    }
}
static bool WebGPUStencilFace(WGPUStencilFaceState& Face,bool Enabled,ECompareFunction Test,EStencilOp Fail,EStencilOp DepthFail,EStencilOp Pass)
{
    Face=WGPU_STENCIL_FACE_STATE_INIT;
    if(!Enabled)return true;
    Face.compare=WebGPUCompare(Test);Face.failOp=WebGPUStencil(Fail);Face.depthFailOp=WebGPUStencil(DepthFail);Face.passOp=WebGPUStencil(Pass);
    return Face.compare!=WGPUCompareFunction_Undefined && Face.failOp!=WGPUStencilOperation_Undefined &&
           Face.depthFailOp!=WGPUStencilOperation_Undefined && Face.passOp!=WGPUStencilOperation_Undefined;
}
static bool WebGPUDepthState(const FDepthStencilStateInitializerRHI& In,WGPUDepthStencilState& Out)
{
    Out=WGPU_DEPTH_STENCIL_STATE_INIT;Out.depthWriteEnabled=In.bEnableDepthWrite?WGPUOptionalBool_True:WGPUOptionalBool_False;Out.depthCompare=WebGPUCompare(In.DepthTest);
    Out.stencilReadMask=In.StencilReadMask;Out.stencilWriteMask=(In.bEnableFrontFaceStencil||In.bEnableBackFaceStencil)?In.StencilWriteMask:0;
    const bool Enabled=In.bEnableFrontFaceStencil||In.bEnableBackFaceStencil;
    if(Out.depthCompare==WGPUCompareFunction_Undefined ||
        !WebGPUStencilFace(Out.stencilFront,Enabled,In.FrontFaceStencilTest,In.FrontFaceStencilFailStencilOp,In.FrontFaceDepthFailStencilOp,In.FrontFacePassStencilOp))return false;
    // Unreal's back-face flag enables a separate back-face state. Without it,
    // both faces use the front state (see D3D12State.cpp), not disabled stencil.
    if(!In.bEnableBackFaceStencil){Out.stencilBack=Out.stencilFront;return true;}
    return WebGPUStencilFace(Out.stencilBack,true,In.BackFaceStencilTest,In.BackFaceStencilFailStencilOp,In.BackFaceDepthFailStencilOp,In.BackFacePassStencilOp);
}
static WGPUBlendOperation WebGPUBlendOp(EBlendOperation Op)
{
    switch(Op) {
        case BO_Add:return WGPUBlendOperation_Add;case BO_Subtract:return WGPUBlendOperation_Subtract;
        case BO_ReverseSubtract:return WGPUBlendOperation_ReverseSubtract;case BO_Min:return WGPUBlendOperation_Min;case BO_Max:return WGPUBlendOperation_Max;
        default:return WGPUBlendOperation_Undefined;
    }
}
static WGPUBlendFactor WebGPUBlendFactor(EBlendFactor Factor,bool Alpha)
{
    switch(Factor) {
        case BF_Zero:return WGPUBlendFactor_Zero;case BF_One:return WGPUBlendFactor_One;
        case BF_SourceColor:return Alpha?WGPUBlendFactor_SrcAlpha:WGPUBlendFactor_Src;
        case BF_InverseSourceColor:return Alpha?WGPUBlendFactor_OneMinusSrcAlpha:WGPUBlendFactor_OneMinusSrc;
        case BF_DestColor:return Alpha?WGPUBlendFactor_DstAlpha:WGPUBlendFactor_Dst;
        case BF_InverseDestColor:return Alpha?WGPUBlendFactor_OneMinusDstAlpha:WGPUBlendFactor_OneMinusDst;
        case BF_SourceAlpha:return WGPUBlendFactor_SrcAlpha;case BF_InverseSourceAlpha:return WGPUBlendFactor_OneMinusSrcAlpha;
        case BF_DestAlpha:return WGPUBlendFactor_DstAlpha;case BF_InverseDestAlpha:return WGPUBlendFactor_OneMinusDstAlpha;
        case BF_ConstantBlendFactor:return WGPUBlendFactor_Constant;case BF_InverseConstantBlendFactor:return WGPUBlendFactor_OneMinusConstant;
        default:return WGPUBlendFactor_Undefined; // Dual-source blending requires an unrequested device feature.
    }
}
static bool WebGPUBlend(const FBlendStateInitializerRHI::FRenderTarget& In,WGPUBlendState& Out)
{
    Out=WGPU_BLEND_STATE_INIT;
    Out.color.operation=WebGPUBlendOp(In.ColorBlendOp);Out.alpha.operation=WebGPUBlendOp(In.AlphaBlendOp);
    Out.color.srcFactor=WebGPUBlendFactor(In.ColorSrcBlend,false);Out.color.dstFactor=WebGPUBlendFactor(In.ColorDestBlend,false);
    Out.alpha.srcFactor=WebGPUBlendFactor(In.AlphaSrcBlend,true);Out.alpha.dstFactor=WebGPUBlendFactor(In.AlphaDestBlend,true);
    if(Out.color.operation==WGPUBlendOperation_Min || Out.color.operation==WGPUBlendOperation_Max)Out.color.srcFactor=Out.color.dstFactor=WGPUBlendFactor_One;
    if(Out.alpha.operation==WGPUBlendOperation_Min || Out.alpha.operation==WGPUBlendOperation_Max)Out.alpha.srcFactor=Out.alpha.dstFactor=WGPUBlendFactor_One;
    return !(uint32(In.ColorWriteMask)&~15u) && Out.color.operation!=WGPUBlendOperation_Undefined && Out.alpha.operation!=WGPUBlendOperation_Undefined &&
        Out.color.srcFactor!=WGPUBlendFactor_Undefined && Out.color.dstFactor!=WGPUBlendFactor_Undefined && Out.alpha.srcFactor!=WGPUBlendFactor_Undefined && Out.alpha.dstFactor!=WGPUBlendFactor_Undefined;
}
TRefCountPtr<FWebGPURasterizerState> CreateWebGPURasterizerState(const FRasterizerStateInitializerRHI& In)
{
    if(In.FillMode!=FM_Solid || In.CullMode>=ERasterizerCullMode_Num || In.DepthClipMode!=ERasterizerDepthClipMode::DepthClip ||
       !FMath::IsFinite(In.DepthBias) || !FMath::IsFinite(In.SlopeScaleDepthBias) || In.DepthBias!=0)return nullptr;
    // Nonzero constant bias needs a depth-format-specific conversion; reject it
    // until that conversion is implemented instead of changing Unreal's units.
    return new FWebGPURasterizerState(In);
}
TRefCountPtr<FWebGPUDepthStencilState> CreateWebGPUDepthStencilState(const FDepthStencilStateInitializerRHI& In)
{
    WGPUDepthStencilState State;return WebGPUDepthState(In,State)?new FWebGPUDepthStencilState(In):nullptr;
}
TRefCountPtr<FWebGPUBlendState> CreateWebGPUBlendState(const FBlendStateInitializerRHI& In)
{
    for(uint32 I=0;I<(In.bUseIndependentRenderTargetBlendStates?MaxSimultaneousRenderTargets:1);++I){WGPUBlendState State;if(!WebGPUBlend(In.RenderTargets[I],State))return nullptr;}
    return new FWebGPUBlendState(In);
}
bool FWebGPUGraphicsPipelineState::Initialize(UEWebGPU::Device& Device,const FGraphicsPipelineStateInitializer& In,FString& Error)
{
    auto Reject=[&](const TCHAR* Why){Error=Why;return false;};
    if(Resource.Handle || !In.BoundShaderState.VertexDeclarationRHI || !In.BoundShaderState.GetVertexShader() || !In.BoundShaderState.GetPixelShader() ||
       In.BoundShaderState.GetGeometryShader() || In.BoundShaderState.GetMeshShader() || In.BoundShaderState.GetAmplificationShader() || In.BoundShaderState.GetWorkGraphShader())return Reject(TEXT("Unsupported or missing graphics shader stages"));
    if(!In.RasterizerState || !In.DepthStencilState || !In.BlendState || In.NumSamples!=1 || In.RenderTargetsEnabled>MaxSimultaneousRenderTargets ||
       In.ConservativeRasterization!=EConservativeRasterization::Disabled || In.bDepthBounds || In.MultiViewCount || In.bHasFragmentDensityAttachment ||
       In.ShadingRate!=VRSSR_1x1 || In.SubpassHint!=ESubpassHint::None || In.SubpassIndex)return Reject(TEXT("Unsupported graphics pipeline state"));
    FRasterizerStateInitializerRHI Raster;FDepthStencilStateInitializerRHI Depth;FBlendStateInitializerRHI Blend;
    if(!In.RasterizerState->GetInitializer(Raster) || !In.DepthStencilState->GetInitializer(Depth) || !In.BlendState->GetInitializer(Blend) ||
       !CreateWebGPURasterizerState(Raster) || !CreateWebGPUDepthStencilState(Depth) || !CreateWebGPUBlendState(Blend))return Reject(TEXT("Invalid fixed-function state"));
    auto State=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    State.primitive.frontFace=WGPUFrontFace_CCW;
    State.primitive.cullMode=Raster.CullMode==CM_CW?WGPUCullMode_Back:Raster.CullMode==CM_CCW?WGPUCullMode_Front:WGPUCullMode_None;
    switch(In.PrimitiveType) {
        case PT_TriangleList:State.primitive.topology=WGPUPrimitiveTopology_TriangleList;break;
        case PT_LineList:State.primitive.topology=WGPUPrimitiveTopology_LineList;break;
        case PT_PointList:State.primitive.topology=WGPUPrimitiveTopology_PointList;break;
        default:return Reject(TEXT("Unsupported primitive topology"));
    }
    auto* Decl=static_cast<FWebGPUVertexDeclaration*>(In.BoundShaderState.VertexDeclarationRHI);
    State.vertex.bufferCount=Decl->Layouts.size();State.vertex.buffers=Decl->Layouts.data();
    WGPUColorTargetState Targets[MaxSimultaneousRenderTargets];WGPUBlendState Blends[MaxSimultaneousRenderTargets];
    auto Fragment=WGPU_FRAGMENT_STATE_INIT;Fragment.targetCount=In.RenderTargetsEnabled;Fragment.targets=Targets;State.fragment=&Fragment;
    for(uint32 I=0;I<In.RenderTargetsEnabled;++I) {
        Targets[I]=WGPU_COLOR_TARGET_STATE_INIT;
        Targets[I].format=WebGPUFormat(EPixelFormat(In.RenderTargetFormats[I]),EnumHasAnyFlags(In.RenderTargetFlags[I],ETextureCreateFlags::SRGB));
        if(Targets[I].format==WGPUTextureFormat_Undefined || In.RenderTargetFormats[I]==PF_DepthStencil || In.RenderTargetFormats[I]==PF_ShadowDepth)return Reject(TEXT("Unsupported color attachment format"));
        const auto& B=Blend.RenderTargets[Blend.bUseIndependentRenderTargetBlendStates?I:0];
        WebGPUBlend(B,Blends[I]);Targets[I].writeMask=WGPUColorWriteMask(B.ColorWriteMask.GetValue());
        const bool Enabled=B.ColorBlendOp!=BO_Add || B.ColorSrcBlend!=BF_One || B.ColorDestBlend!=BF_Zero || B.AlphaBlendOp!=BO_Add || B.AlphaSrcBlend!=BF_One || B.AlphaDestBlend!=BF_Zero;
        if(Enabled)Targets[I].blend=&Blends[I];
    }
    if(Blend.bUseAlphaToCoverage)return Reject(TEXT("Alpha-to-coverage requires multisample support"));
    WGPUDepthStencilState DS;
    if(In.DepthStencilTargetFormat!=PF_Unknown) {
        if(In.DepthStencilTargetFormat!=PF_DepthStencil && In.DepthStencilTargetFormat!=PF_ShadowDepth)return Reject(TEXT("Unsupported depth attachment format"));
        WebGPUDepthState(Depth,DS);DS.format=WebGPUFormat(In.DepthStencilTargetFormat);DS.depthBiasSlopeScale=Raster.SlopeScaleDepthBias;
        if(In.DepthStencilTargetFormat==PF_ShadowDepth && (Depth.bEnableFrontFaceStencil||Depth.bEnableBackFaceStencil))return Reject(TEXT("Stencil state requires a stencil attachment"));
        State.depthStencil=&DS;
    }else if(Depth.bEnableDepthWrite || Depth.DepthTest!=CF_Always || Depth.bEnableFrontFaceStencil || Depth.bEnableBackFaceStencil || Raster.SlopeScaleDepthBias!=0)return Reject(TEXT("Depth/stencil state requires an attachment"));
    auto* V=static_cast<FWebGPUVertexShader*>(In.BoundShaderState.GetVertexShader());auto* P=static_cast<FWebGPUPixelShader*>(In.BoundShaderState.GetPixelShader());
    if(!Device.CreateRenderPipeline(Resource,V->Resource,P->Resource,State))return Reject(TEXT("WebGPU graphics pipeline creation failed"));
    Vertex=V;Pixel=P;Declaration=Decl;Primitive=In.PrimitiveType;Error.Empty();return true;
}
TRefCountPtr<FWebGPUGraphicsPipelineState> CreateWebGPUGraphicsPipelineState(UEWebGPU::Device& Device,const FGraphicsPipelineStateInitializer& In,FString& Error)
{
    TRefCountPtr<FWebGPUGraphicsPipelineState> Result=new FWebGPUGraphicsPipelineState;
    return Result->Initialize(Device,In,Error)?Result:nullptr;
}

TRefCountPtr<FWebGPUBuffer> CreateWebGPUBuffer(UEWebGPU::Device& Device, const FRHIBufferCreateDesc& Desc)
{
    constexpr EBufferUsageFlags Supported = EBufferUsageFlags::Static | EBufferUsageFlags::Dynamic | EBufferUsageFlags::Volatile |
        EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ByteAddressBuffer | EBufferUsageFlags::SourceCopy |
        EBufferUsageFlags::DrawIndirect | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::VertexBuffer |
        EBufferUsageFlags::IndexBuffer | EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::UniformBuffer;
    if (!Desc.Size || Desc.Size % 4 || EnumHasAnyFlags(Desc.Usage, ~Supported) ||
        (Desc.InitAction != ERHIBufferInitAction::Default && Desc.InitAction != ERHIBufferInitAction::Zeroed)) return nullptr;
    WGPUBufferUsage Usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    if (EnumHasAnyFlags(Desc.Usage, EBufferUsageFlags::VertexBuffer)) Usage |= WGPUBufferUsage_Vertex;
    if (EnumHasAnyFlags(Desc.Usage, EBufferUsageFlags::IndexBuffer)) Usage |= WGPUBufferUsage_Index;
    if (EnumHasAnyFlags(Desc.Usage, EBufferUsageFlags::UniformBuffer)) Usage |= WGPUBufferUsage_Uniform;
    if (EnumHasAnyFlags(Desc.Usage, EBufferUsageFlags::DrawIndirect)) Usage |= WGPUBufferUsage_Indirect;
    if (EnumHasAnyFlags(Desc.Usage, EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ByteAddressBuffer | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)) Usage |= WGPUBufferUsage_Storage;
    TRefCountPtr<FWebGPUBuffer> Buffer = new FWebGPUBuffer(Desc);
    // WebGPU initializes newly allocated buffers to zero.
    if (!Device.CreateBuffer(Buffer->Resource, Desc.Size, Usage)) return nullptr;
    return Buffer;
}
TRefCountPtr<FWebGPUTexture> CreateWebGPUTexture(UEWebGPU::Device& Device, const FRHITextureCreateDesc& Desc)
{
    constexpr ETextureCreateFlags Supported = ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV |
        ETextureCreateFlags::DepthStencilTargetable | ETextureCreateFlags::SRGB;
    if ((Desc.Dimension != ETextureDimension::Texture2D && Desc.Dimension != ETextureDimension::Texture2DArray) ||
        Desc.NumSamples != 1 || (Desc.Dimension == ETextureDimension::Texture2D && Desc.ArraySize != 1) || Desc.Extent.X <= 0 || Desc.Extent.Y <= 0 ||
        EnumHasAnyFlags(Desc.Flags, ~Supported) || Desc.InitAction != ERHITextureInitAction::Default) return nullptr;
    const bool Depth=Desc.Format==PF_DepthStencil || Desc.Format==PF_ShadowDepth;
    const auto Format=WebGPUFormat(Desc.Format,EnumHasAnyFlags(Desc.Flags,ETextureCreateFlags::SRGB));
    if(Format==WGPUTextureFormat_Undefined || (Depth&&EnumHasAnyFlags(Desc.Flags,ETextureCreateFlags::UAV|ETextureCreateFlags::RenderTargetable)) ||
       (!Depth&&EnumHasAnyFlags(Desc.Flags,ETextureCreateFlags::DepthStencilTargetable)))return nullptr;
    WGPUTextureUsage Usage = Depth?WGPUTextureUsage_None:WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst;
    if (EnumHasAnyFlags(Desc.Flags,ETextureCreateFlags::RenderTargetable|ETextureCreateFlags::DepthStencilTargetable)) Usage |= WGPUTextureUsage_RenderAttachment;
    if (EnumHasAnyFlags(Desc.Flags,ETextureCreateFlags::ShaderResource)) Usage |= WGPUTextureUsage_TextureBinding;
    // Supported storage formats can also serve as attachments for native
    // WebGPU clears. Keep their exact format; no replacement shader is needed.
    if (EnumHasAnyFlags(Desc.Flags,ETextureCreateFlags::UAV)) Usage |= WGPUTextureUsage_StorageBinding|WGPUTextureUsage_RenderAttachment;
    TRefCountPtr<FWebGPUTexture> Texture = new FWebGPUTexture(Desc);
    if (!Device.CreateTexture2D(Texture->Resource,Desc.Extent.X,Desc.Extent.Y,Desc.ArraySize,Desc.NumMips,Format,Usage)) return nullptr;
    if ((Usage&WGPUTextureUsage_TextureBinding) && !Device.CreateTextureView(Texture->DefaultView,Texture->Resource,0,Desc.NumMips,0,Desc.ArraySize,Desc.Dimension==ETextureDimension::Texture2DArray,Depth?WGPUTextureAspect_DepthOnly:WGPUTextureAspect_All))return nullptr;
    return Texture;
}
TRefCountPtr<FWebGPUTextureSRV> CreateWebGPUTextureSRV(UEWebGPU::Device& Device, FWebGPUTexture* Texture, const FRHIViewDesc& Desc)
{
    if (!Texture || !Desc.IsTexture() || !Desc.IsSRV() || !(Texture->Resource.Usage & WGPUTextureUsage_TextureBinding)) return nullptr;
    const auto Info = Desc.Texture.SRV.GetViewInfo(Texture);
    if (Info.Format != Texture->GetDesc().Format || Info.bSRGB!=EnumHasAnyFlags(Texture->GetDesc().Flags,ETextureCreateFlags::SRGB) || Info.Plane != ERHITexturePlane::Primary ||
        (Info.Dimension != FRHIViewDesc::EDimension::Texture2D && Info.Dimension != FRHIViewDesc::EDimension::Texture2DArray)) return nullptr;
    TRefCountPtr<FWebGPUTextureSRV> View = new FWebGPUTextureSRV(Texture,Desc);
    const bool Depth=Info.Format==PF_DepthStencil || Info.Format==PF_ShadowDepth;
    if (!Device.CreateTextureView(View->View,Texture->Resource,Info.MipRange.First,Info.MipRange.Num,Info.ArrayRange.First,Info.ArrayRange.Num,Info.Dimension == FRHIViewDesc::EDimension::Texture2DArray,Depth?WGPUTextureAspect_DepthOnly:WGPUTextureAspect_All)) return nullptr;
    return View;
}

static bool IsBufferViewSupported(UEWebGPU::Device& Device,FWebGPUBuffer* Buffer,const FRHIViewDesc::FBuffer::FViewInfo& Info)
{
    if(Device.GetState()!=UEWebGPU::Device::State::Ready || !Buffer || !(Buffer->Resource.Usage&WGPUBufferUsage_Storage) || Info.bNullView ||
       !Info.SizeInBytes || Info.OffsetInBytes%Device.GetStorageBufferOffsetAlignment() || uint64(Info.OffsetInBytes)+Info.SizeInBytes>Buffer->Resource.Size)return false;
    switch(Info.BufferType) {
        case FRHIViewDesc::EBufferType::Structured:return Info.StrideInBytes==4 || Info.StrideInBytes==8 || Info.StrideInBytes==16;
        case FRHIViewDesc::EBufferType::Typed:return Info.Format==PF_R32_UINT && Info.StrideInBytes==4;
        case FRHIViewDesc::EBufferType::Raw:return Info.StrideInBytes==4;
        default:return false;
    }
}
TRefCountPtr<FWebGPUBufferSRV> CreateWebGPUBufferSRV(UEWebGPU::Device& Device,FWebGPUBuffer* Buffer,const FRHIViewDesc& Desc)
{
    if(!Buffer || !Desc.IsBuffer() || !Desc.IsSRV())return nullptr;
    const auto Info=Desc.Buffer.SRV.GetViewInfo(Buffer);
    if(!IsBufferViewSupported(Device,Buffer,Info))return nullptr;
    TRefCountPtr<FWebGPUBufferSRV> View=new FWebGPUBufferSRV(Buffer,Desc);
    View->Binding={0,&Buffer->Resource,nullptr,nullptr,Info.OffsetInBytes,Info.SizeInBytes};return View;
}
TRefCountPtr<FWebGPUBufferUAV> CreateWebGPUBufferUAV(UEWebGPU::Device& Device,FWebGPUBuffer* Buffer,const FRHIViewDesc& Desc)
{
    if(!Buffer || !Desc.IsBuffer() || !Desc.IsUAV())return nullptr;
    const auto Info=Desc.Buffer.UAV.GetViewInfo(Buffer);
    if(!IsBufferViewSupported(Device,Buffer,Info) || Info.bAtomicCounter || Info.bAppendBuffer)return nullptr;
    TRefCountPtr<FWebGPUBufferUAV> View=new FWebGPUBufferUAV(Buffer,Desc);
    View->Binding={0,&Buffer->Resource,nullptr,nullptr,Info.OffsetInBytes,Info.SizeInBytes};return View;
}
TRefCountPtr<FWebGPUSamplerState> CreateWebGPUSampler(UEWebGPU::Device& Device,const FSamplerStateInitializerRHI& Desc)
{
    if(Desc.MipBias!=0 || Desc.BorderColor!=0 || Desc.MaxAnisotropy>1 || Desc.MaxAnisotropy<0 ||
       (Desc.Filter!=SF_Point && Desc.Filter!=SF_Bilinear && Desc.Filter!=SF_Trilinear))return nullptr;
    auto Address=[](ESamplerAddressMode Mode){
        switch(Mode){case AM_Wrap:return WGPUAddressMode_Repeat;case AM_Clamp:return WGPUAddressMode_ClampToEdge;case AM_Mirror:return WGPUAddressMode_MirrorRepeat;default:return WGPUAddressMode_Undefined;}
    };
    auto Native=WGPU_SAMPLER_DESCRIPTOR_INIT;
    Native.addressModeU=Address(Desc.AddressU);Native.addressModeV=Address(Desc.AddressV);Native.addressModeW=Address(Desc.AddressW);
    Native.minFilter=Native.magFilter=Desc.Filter==SF_Point?WGPUFilterMode_Nearest:WGPUFilterMode_Linear;
    Native.mipmapFilter=Desc.Filter==SF_Trilinear?WGPUMipmapFilterMode_Linear:WGPUMipmapFilterMode_Nearest;
    Native.lodMinClamp=Desc.MinMipLevel;Native.lodMaxClamp=Desc.MaxMipLevel;
    if(Desc.SamplerComparisonFunction==SCF_Less)Native.compare=WGPUCompareFunction_Less;
    else if(Desc.SamplerComparisonFunction!=SCF_Never)return nullptr;
    TRefCountPtr<FWebGPUSamplerState> Sampler=new FWebGPUSamplerState;
    if(!Device.CreateSampler(Sampler->Resource,Native))return nullptr;return Sampler;
}

bool GetWebGPUResourceBinding(FRHIResource* Resource,UEWebGPU::ResourceBinding& Out)
{
    Out={};if(!Resource)return true; // Unused resource-table entries may be null.
    switch(Resource->GetType()) {
        case RRT_Texture:Out.TextureResource=&static_cast<FWebGPUTexture*>(Resource)->DefaultView;return Out.TextureResource->Handle!=nullptr;
        case RRT_SamplerState:Out.SamplerResource=&static_cast<FWebGPUSamplerState*>(Resource)->Resource;return Out.SamplerResource->Handle!=nullptr;
        case RRT_ShaderResourceView:{
            auto* View=static_cast<FRHIShaderResourceView*>(Resource);
            if(View->IsBuffer())Out=static_cast<FWebGPUBufferSRV*>(View)->Binding;
            else Out.TextureResource=&static_cast<FWebGPUTextureSRV*>(View)->View;
            return true;
        }
        case RRT_UnorderedAccessView:{
            auto* View=static_cast<FRHIUnorderedAccessView*>(Resource);
            if(View->IsBuffer())Out=static_cast<FWebGPUBufferUAV*>(View)->Binding;
            else Out.TextureResource=&static_cast<FWebGPUTextureUAV*>(View)->View;
            return true;
        }
        default:return false;
    }
}
bool FWebGPUUniformBuffer::Update(UEWebGPU::Device& Device,const void* Contents)
{
    const auto& Layout=GetLayout();
    if(Device.GetState()!=UEWebGPU::Device::State::Ready || EnumHasAnyFlags(Layout.Flags,ERHIUniformBufferFlags::UniformView))return false;
    TArray<TRefCountPtr<FRHIResource>> NewResources;
    std::vector<UEWebGPU::ResourceBinding> NewBindings;
    std::vector<uint32_t> Offsets;
    for(const auto& Member:Layout.Resources) {
        switch(Member.MemberType) {
            case UBMT_TEXTURE:case UBMT_SAMPLER:case UBMT_SRV:case UBMT_UAV:case UBMT_RDG_TEXTURE:
            case UBMT_RDG_TEXTURE_SRV:case UBMT_RDG_TEXTURE_NON_PIXEL_SRV:case UBMT_RDG_BUFFER_SRV:
            case UBMT_RDG_TEXTURE_UAV:case UBMT_RDG_BUFFER_UAV:break;
            default:return false;
        }
        FRHIResource* Resource=Contents?GetShaderParameterResourceRHI(Contents,Member.MemberOffset,Member.MemberType):nullptr;
        if(Resource) {
            const bool Texture=Member.MemberType==UBMT_TEXTURE || Member.MemberType==UBMT_RDG_TEXTURE;
            const bool Sampler=Member.MemberType==UBMT_SAMPLER;
            const bool UAV=Member.MemberType==UBMT_UAV || Member.MemberType==UBMT_RDG_TEXTURE_UAV || Member.MemberType==UBMT_RDG_BUFFER_UAV;
            const auto Expected=Texture?RRT_Texture:Sampler?RRT_SamplerState:UAV?RRT_UnorderedAccessView:RRT_ShaderResourceView;
            if(Resource->GetType()!=Expected)return false;
        }
        UEWebGPU::ResourceBinding Binding{};
        if(!GetWebGPUResourceBinding(Resource,Binding))return false;
        NewResources.Add(Resource);NewBindings.push_back(Binding);Offsets.push_back(Member.MemberOffset);
    }
    std::vector<uint8_t> Zero;
    if(!Contents){Zero.resize(Layout.ConstantBufferSize,0);Contents=Zero.data();}
    std::vector<uint8_t> Packed;
    if(!UEWebGPU::PackUniformData(Contents,Layout.ConstantBufferSize,Offsets.data(),Offsets.size(),Packed))return false;
    std::unique_ptr<UEWebGPU::Buffer> NewConstants;
    if(!Packed.empty()) {
        // Rename on update: already encoded/submitted bind groups keep their
        // old buffer. A later upload must not overwrite an earlier draw's data.
        NewConstants=std::make_unique<UEWebGPU::Buffer>();
        if(!Device.CreateBuffer(*NewConstants,Packed.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
           !Device.WriteBuffer(*NewConstants,0,Packed.data(),Packed.size()))return false;
    }
    Constants=std::move(NewConstants);Bindings=std::move(NewBindings);ResourceTable=MoveTemp(NewResources);return true;
}
TRefCountPtr<FWebGPUUniformBuffer> CreateWebGPUUniformBuffer(UEWebGPU::Device& Device,const FRHIUniformBufferLayout* Layout,const void* Contents)
{
    if(!Layout || Layout->ConstantBufferSize>65536 || Layout->Resources.Num()>8192)return nullptr;
    TRefCountPtr<FWebGPUUniformBuffer> Buffer=new FWebGPUUniformBuffer(Layout);
    if(!Buffer->Update(Device,Contents))return nullptr;return Buffer;
}

static bool DecodeWebGPUShader(const FRHICreateShaderDesc& Desc,uint32 Stage,UEWebGPU::ShaderCode& Code,FShaderResourceTable& ShaderResourceTable,FString& Error)
{
    if(!Code.WGSL.empty()) {Error=TEXT("Shader already initialized");return false;}
    if(Desc.Code.Num()<4) {Error=TEXT("Missing Unreal shader-code trailer");return false;}
    int32 OptionalBytes=0;FMemory::Memcpy(&OptionalBytes,Desc.Code.GetData()+Desc.Code.Num()-4,4);
    if(OptionalBytes<4 || OptionalBytes>Desc.Code.Num()) {Error=TEXT("Invalid Unreal shader-code trailer");return false;}
    FShaderCodeReader Reader(Desc.Code);std::string DecodeError;
    if(!UEWebGPU::ShaderCode::Decode(Desc.Code.GetData(),Reader.GetActualShaderCodeSize(),Code,DecodeError)) {Error=UTF8_TO_TCHAR(DecodeError.c_str());return false;}
    if(Code.Stage!=Stage){Error=TEXT("Exported shader stage does not match the requested RHI shader");return false;}
    ShaderResourceTable.ResourceTableBits=Code.ResourceTableBits;
    ShaderResourceTable.ResourceTableLayoutHashes.Append(Code.ResourceTableLayoutHashes.data(),int32(Code.ResourceTableLayoutHashes.size()));
    auto CopyTokens=[&](TArray<uint32>& Out,uint32 Kind){auto Tokens=Code.MakeResourceTableTokens(Kind);Out.Append(Tokens.data(),int32(Tokens.size()));};
    CopyTokens(ShaderResourceTable.TextureMap,4);CopyTokens(ShaderResourceTable.ShaderResourceViewMap,1);
    CopyTokens(ShaderResourceTable.UnorderedAccessViewMap,2);CopyTokens(ShaderResourceTable.SamplerMap,6);
    return true;
}
bool FWebGPUComputeShader::Initialize(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error)
{
    if(Pipeline.Handle || !DecodeWebGPUShader(Desc,1,Code,ShaderResourceTable,Error))return false;
    UE::RHICore::InitStaticUniformBufferSlots(this);
    if(!Device.CreateComputePipeline(Pipeline,Code.WGSL.c_str(),Code.Entry.c_str())) {Error=UTF8_TO_TCHAR(Device.GetError().c_str());return false;}
    Error.Empty();return true;
}
TRefCountPtr<FWebGPUComputeShader> CreateWebGPUComputeShader(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error)
{
    TRefCountPtr<FWebGPUComputeShader> Shader=new FWebGPUComputeShader;
    if(!Shader->Initialize(Device,Desc,Error))return nullptr;return Shader;
}
bool FWebGPUVertexShader::Initialize(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error)
{
    UEWebGPU::ShaderCode Code;
    if(Resource.Handle || !DecodeWebGPUShader(Desc,2,Code,ShaderResourceTable,Error))return false;
    UE::RHICore::InitStaticUniformBufferSlots(this);
    if(!Device.CreateRasterShader(Resource,Code)){Error=TEXT("Could not create vertex shader module");return false;}
    Error.Empty();return true;
}
bool FWebGPUPixelShader::Initialize(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error)
{
    UEWebGPU::ShaderCode Code;
    if(Resource.Handle || !DecodeWebGPUShader(Desc,3,Code,ShaderResourceTable,Error))return false;
    UE::RHICore::InitStaticUniformBufferSlots(this);
    if(!Device.CreateRasterShader(Resource,Code)){Error=TEXT("Could not create pixel shader module");return false;}
    Error.Empty();return true;
}
TRefCountPtr<FWebGPUVertexShader> CreateWebGPUVertexShader(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error)
{
    TRefCountPtr<FWebGPUVertexShader> Shader=new FWebGPUVertexShader;
    if(!Shader->Initialize(Device,Desc,Error))return nullptr;return Shader;
}
TRefCountPtr<FWebGPUPixelShader> CreateWebGPUPixelShader(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error)
{
    TRefCountPtr<FWebGPUPixelShader> Shader=new FWebGPUPixelShader;
    if(!Shader->Initialize(Device,Desc,Error))return nullptr;return Shader;
}

void FWebGPUShaderParameterState::SetShader(FRHIShader* InShader,const UEWebGPU::ShaderCode* InCode)
{
    if(Shader.GetReference()==InShader)return;
    Shader=InShader;Code=InCode;Resources={};Uniforms={};
    Constants.assign(Code?Code->SourceConstantBytes:0,0);Written.assign(Constants.size(),0);
}
bool FWebGPUShaderParameterState::SetUniformBuffer(uint32 Slot,FWebGPUUniformBuffer* Buffer)
{
    if(!Shader || Slot>=Uniforms.size())return false;
    const auto& Hashes=Code->ResourceTableLayoutHashes;
    if(Buffer && Slot<Hashes.size() && Hashes[Slot] && Buffer->GetLayout().GetHash()!=Hashes[Slot])return false;
    Uniforms[Slot]=Buffer;return true;
}
bool FWebGPUShaderParameterState::SetParameters(TConstArrayView<uint8> Data,TConstArrayView<FRHIShaderParameter> Parameters,
    TConstArrayView<FRHIShaderParameterResource> ResourceParameters,TConstArrayView<FRHIShaderParameterResource> BindlessParameters)
{
    if(!Shader || !BindlessParameters.IsEmpty())return false;
    // Validate the whole batch before changing retained state. Unreal can
    // supply only the changed constant ranges in a later parameter batch.
    auto NextConstants=Constants,NextWritten=Written;
    auto NextResources=Resources;auto NextUniforms=Uniforms;
    for(const auto& Parameter:Parameters) {
        if(Parameter.BufferIndex!=0 || uint32(Parameter.ByteOffset)+Parameter.ByteSize>uint32(Data.Num()) ||
           uint32(Parameter.BaseIndex)+Parameter.ByteSize>Constants.size())return false;
        if(Parameter.ByteSize) {
            FMemory::Memcpy(NextConstants.data()+Parameter.BaseIndex,Data.GetData()+Parameter.ByteOffset,Parameter.ByteSize);
            std::fill_n(NextWritten.begin()+Parameter.BaseIndex,Parameter.ByteSize,uint8(1));
        }
    }
    for(const auto& Parameter:ResourceParameters) {
        using Type=FRHIShaderParameterResource::EType;
        if(Parameter.Type==Type::UniformBuffer) {
            if(Parameter.Index>=NextUniforms.size() || (Parameter.Resource && Parameter.Resource->GetType()!=RRT_UniformBuffer))return false;
            auto* Buffer=static_cast<FWebGPUUniformBuffer*>(Parameter.Resource);
            const auto& Hashes=Code->ResourceTableLayoutHashes;
            if(Buffer && Parameter.Index<Hashes.size() && Hashes[Parameter.Index] && Buffer->GetLayout().GetHash()!=Hashes[Parameter.Index])return false;
            NextUniforms[Parameter.Index]=Buffer;continue;
        }
        if(Parameter.Index>=NextResources.size())return false;
        ERHIResourceType Expected;
        switch(Parameter.Type) {
            case Type::Texture:Expected=RRT_Texture;break;
            case Type::ResourceView:Expected=RRT_ShaderResourceView;break;
            case Type::UnorderedAccessView:Expected=RRT_UnorderedAccessView;break;
            case Type::Sampler:Expected=RRT_SamplerState;break;
            default:return false;
        }
        if(Parameter.Resource && Parameter.Resource->GetType()!=Expected)return false;
        const UEWebGPU::ShaderResource* Descriptor=nullptr;
        for(const auto& Candidate:Code->Resources)if(Candidate.Binding==Parameter.Index)Descriptor=&Candidate;
        if(!Descriptor)return false;
        for(const auto& Table:Code->ResourceTableBindings)if(Table.Binding==Parameter.Index)return false;
        const bool Matches=(Descriptor->Kind==1 && Expected==RRT_ShaderResourceView) ||
            ((Descriptor->Kind==2 || Descriptor->Kind==5) && Expected==RRT_UnorderedAccessView) ||
            (Descriptor->Kind==4 && (Expected==RRT_Texture || Expected==RRT_ShaderResourceView)) ||
            (Descriptor->Kind==6 && Expected==RRT_SamplerState);
        if(!Matches)return false;
        NextResources[Parameter.Index]=Parameter.Resource;
    }
    Constants=std::move(NextConstants);Written=std::move(NextWritten);
    Resources=std::move(NextResources);Uniforms=std::move(NextUniforms);return true;
}
bool FWebGPUComputeState::Dispatch(UEWebGPU::Device& Device,uint32 X,uint32 Y,uint32 Z)
{
    return DispatchCommands(Device,X,Y,Z,nullptr,0);
}
bool FWebGPUComputeState::DispatchIndirect(UEWebGPU::Device& Device,FWebGPUBuffer* Arguments,uint32 Offset)
{
    return Arguments && DispatchCommands(Device,0,0,0,Arguments,Offset);
}
bool FWebGPUComputeState::DispatchCommands(UEWebGPU::Device& Device,uint32 X,uint32 Y,uint32 Z,FWebGPUBuffer* Arguments,uint32 Offset)
{
    if(!Shader || !Shader->Pipeline.Handle)return false;
    const auto& Code=Shader->Code;
    FWebGPUPreparedParameters Prepared;
    if(!Prepare(Device,Prepared))return false;
    const auto& Bindings=Prepared.Resources;const auto& Tables=Prepared.Tables;
    if(Arguments)return Code.Version==2?Device.DispatchShaderIndirect(Shader->Pipeline,Code,Bindings.data(),Bindings.size(),Tables.data(),Tables.size(),Arguments->Resource,Offset):
        Device.DispatchIndirect(Shader->Pipeline,Bindings.data(),Bindings.size(),Arguments->Resource,Offset);
    return Code.Version==2?Device.DispatchShader(Shader->Pipeline,Code,Bindings.data(),Bindings.size(),Tables.data(),Tables.size(),X,Y,Z):
        Device.Dispatch(Shader->Pipeline,Bindings.data(),Bindings.size(),X,Y,Z);
}
bool FWebGPUShaderParameterState::Prepare(UEWebGPU::Device& Device,FWebGPUPreparedParameters& Out) const
{
    if(!Shader || !Code || Out.Root.Handle || !Out.Resources.empty() || !Out.Tables.empty())return false;
    // Do not submit an invocation with constants never provided by Unreal.
    for(const auto& Mapping:Code->Mappings)for(uint32 I=0;I<Mapping.Bytes;++I)if(!Written[Mapping.Source+I])return false;
    auto& Root=Out.Root;
    if(Code->ConstantBytes) {
        std::vector<uint8_t> Packed;
        if(!Code->PackConstants(Constants.data(),Constants.size(),Packed) ||
           !Device.CreateBuffer(Root,Packed.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst) ||
           !Device.WriteBuffer(Root,0,Packed.data(),Packed.size()))return false;
    }
    uint64 TableBindings=0;
    for(const auto& Entry:Code->ResourceTableBindings)TableBindings|=uint64(1)<<Entry.Binding;
    auto& Bindings=Out.Resources;
    for(const auto& Descriptor:Code->Resources) {
        if(TableBindings&(uint64(1)<<Descriptor.Binding))continue;
        UEWebGPU::ResourceBinding Binding{};Binding.Binding=Descriptor.Binding;
        if(Descriptor.Kind==0)Binding.BufferResource=&Root;
        else if(Descriptor.Kind==3) {
            if(!Uniforms[Descriptor.Binding])return false;
            Binding.BufferResource=Uniforms[Descriptor.Binding]->GetConstants();
            if(!Binding.BufferResource)return false;
        } else {
            auto* Resource=Resources[Descriptor.Binding].GetReference();
            if(!Resource || !GetWebGPUResourceBinding(Resource,Binding))return false;
            if((Descriptor.Kind==1 || Descriptor.Kind==2) && !Binding.BufferResource)return false;
            Binding.Binding=Descriptor.Binding;
        }
        Bindings.push_back(Binding);
    }
    auto& Tables=Out.Tables;
    for(uint32 Slot=0;Slot<32;++Slot)if(Code->ResourceTableBits&(uint32(1)<<Slot)) {
        if(!Uniforms[Slot])return false;
        Tables.push_back(Uniforms[Slot]->GetTable(Slot));
    }
    return true;
}
TRefCountPtr<FWebGPUTextureUAV> CreateWebGPUTextureUAV(UEWebGPU::Device& Device, FWebGPUTexture* Texture, const FRHIViewDesc& Desc)
{
    if (!Texture || !Desc.IsTexture() || !Desc.IsUAV() || !(Texture->Resource.Usage & WGPUTextureUsage_StorageBinding)) return nullptr;
    const auto Info = Desc.Texture.UAV.GetViewInfo(Texture);
    if (Info.Format != Texture->GetDesc().Format || Info.Plane != ERHITexturePlane::Primary ||
        (Info.Dimension != FRHIViewDesc::EDimension::Texture2D && Info.Dimension != FRHIViewDesc::EDimension::Texture2DArray)) return nullptr;
    TRefCountPtr<FWebGPUTextureUAV> View = new FWebGPUTextureUAV(Texture,Desc);
    if (!Device.CreateTextureView(View->View,Texture->Resource,Info.MipLevel,1,Info.ArrayRange.First,Info.ArrayRange.Num,Info.Dimension == FRHIViewDesc::EDimension::Texture2DArray)) return nullptr;
    return View;
}

void FWebGPUComputeContext::RHISetComputePipelineState(FRHIComputePipelineState* InPipeline)
{
    RequireWebGPUCommand(InPipeline && InPipeline->IsValid() && InPipeline->GetComputeShader(),TEXT("compute pipeline"));
    Pipeline=InPipeline;Pipeline->MarkUsed();
    State.SetShader(static_cast<FWebGPUComputeShader*>(Pipeline->GetComputeShader()));
    ApplyStaticUniformBuffers();
}
void FWebGPUComputeContext::ApplyStaticUniformBuffers()
{
    if(!Pipeline)return;
    const auto& Slots=Pipeline->GetComputeShader()->GetStaticSlots();
    for(int32 Index=0;Index<Slots.Num();++Index)if(IsUniformBufferStaticSlotValid(Slots[Index])) {
        RequireWebGPUCommand(Slots[Index]<StaticUniforms.size() &&
            State.SetUniformBuffer(Index,StaticUniforms[Slots[Index]].GetReference()),TEXT("static uniform buffer layout"));
    }
}
void FWebGPUComputeContext::RHIDispatchComputeShader(uint32 X,uint32 Y,uint32 Z)
{
    RequireWebGPUCommand(State.Dispatch(Device,X,Y,Z),TEXT("compute dispatch"));
}
void FWebGPUComputeContext::RHIDispatchIndirectComputeShader(FRHIBuffer* Arguments,uint32 Offset)
{
    RequireWebGPUCommand(State.DispatchIndirect(Device,static_cast<FWebGPUBuffer*>(Arguments),Offset),TEXT("indirect compute dispatch"));
}
void FWebGPUComputeContext::RHISetShaderParameters(FRHIComputeShader* Shader,TConstArrayView<uint8> Data,TConstArrayView<FRHIShaderParameter> Parameters,
    TConstArrayView<FRHIShaderParameterResource> Resources,TConstArrayView<FRHIShaderParameterResource> Bindless)
{
    RequireWebGPUCommand(Pipeline && Pipeline->GetComputeShader()==Shader && State.SetParameters(Data,Parameters,Resources,Bindless),TEXT("compute shader parameters"));
}
void FWebGPUComputeContext::RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& Bindings)
{
    std::array<TRefCountPtr<FWebGPUUniformBuffer>,MAX_UNIFORM_BUFFER_STATIC_SLOTS> Next{};
    for(int32 Index=0;Index<Bindings.GetUniformBufferCount();++Index) {
        const auto Slot=Bindings.GetSlot(Index);auto* Buffer=Bindings.GetUniformBuffer(Index);
        RequireWebGPUCommand(Slot<Next.size(),TEXT("static uniform buffer slot"));
        Next[Slot]=static_cast<FWebGPUUniformBuffer*>(Buffer);
    }
    StaticUniforms=std::move(Next);ApplyStaticUniformBuffers();
}
void FWebGPUComputeContext::RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot,FRHIUniformBuffer* Buffer)
{
    RequireWebGPUCommand(Slot<StaticUniforms.size(),TEXT("static uniform buffer slot"));
    StaticUniforms[Slot]=static_cast<FWebGPUUniformBuffer*>(Buffer);ApplyStaticUniformBuffers();
}
void FWebGPUComputeContext::RHISetUniformBufferDynamicOffset(FUniformBufferStaticSlot Slot,uint32 Offset)
{
    RequireWebGPUCommand(Slot<StaticUniforms.size() && Offset==0,TEXT("uniform buffer dynamic offset"));
}
void FWebGPUComputeContext::ClearBufferUAV(FRHIUnorderedAccessView* UAV,uint32 Value)
{
    RequireWebGPUCommand(UAV && UAV->IsBuffer(),TEXT("buffer UAV clear resource type"));
    const auto& Binding=static_cast<FWebGPUBufferUAV*>(UAV)->Binding;
    RequireWebGPUCommand(Binding.BufferBytes && Binding.BufferBytes%4==0 && Binding.BufferBytes/4<=MAX_uint32,TEXT("buffer UAV clear range"));
    FRHIComputeCommandList_RecursiveHazardous Commands(this);
    const uint32 Values[]={Value};
    // Use Unreal's existing clear shader and parameter binding helper. Buffer
    // storage is untyped in WebGPU; raw/structured clears repeat the X bits.
    ClearUAVShader_T<EClearReplacementResourceType::Buffer,EClearReplacementValueType::Uint32,1,false>(
        Commands,UAV,uint32(Binding.BufferBytes/4),1,1,Values);
}
void FWebGPUComputeContext::RHIClearUAVUint(FRHIUnorderedAccessView* UAV,const FUintVector4& Values)
{
    RequireWebGPUCommand(UAV!=nullptr,TEXT("null UAV clear"));
    if(UAV->IsBuffer()){ClearBufferUAV(UAV,Values.X);return;}
    auto* Texture=static_cast<FWebGPUTexture*>(UAV->GetResource());
    RequireWebGPUCommand(Texture->GetDesc().Format==PF_R32_UINT,TEXT("integer clear of a floating-point texture"));
    const auto Info=UAV->GetDesc().Texture.UAV.GetViewInfo(Texture);
    RequireWebGPUCommand(Device.ClearTextureLayers(Texture->Resource,{double(Values.X),double(Values.Y),double(Values.Z),double(Values.W)},
        Info.MipLevel,Info.ArrayRange.First,Info.ArrayRange.Num),TEXT("texture UAV integer clear"));
}
void FWebGPUComputeContext::RHIClearUAVFloat(FRHIUnorderedAccessView* UAV,const FVector4f& Values)
{
    RequireWebGPUCommand(UAV!=nullptr,TEXT("null UAV clear"));
    if(!UAV->IsBuffer()) {
        auto* Texture=static_cast<FWebGPUTexture*>(UAV->GetResource());
        RequireWebGPUCommand(Texture->GetDesc().Format!=PF_R32_UINT,TEXT("floating-point clear of an integer texture"));
        const auto Info=UAV->GetDesc().Texture.UAV.GetViewInfo(Texture);
        RequireWebGPUCommand(Device.ClearTextureLayers(Texture->Resource,{Values.X,Values.Y,Values.Z,Values.W},
            Info.MipLevel,Info.ArrayRange.First,Info.ArrayRange.Num),TEXT("texture UAV float clear"));return;
    }
    const auto* View=static_cast<FWebGPUBufferUAV*>(UAV);
    const auto Info=View->GetDesc().Buffer.UAV.GetViewInfo(static_cast<FRHIBuffer*>(View->GetResource()));
    RequireWebGPUCommand(Info.BufferType!=FRHIViewDesc::EBufferType::Typed,TEXT("float clear of an integer typed buffer"));
    uint32 Bits;FMemory::Memcpy(&Bits,&Values.X,sizeof(Bits));ClearBufferUAV(UAV,Bits);
}

static bool WebGPUTransitionStorageValid()
{
    return GRHITransitionPrivateData_SizeInBytes==sizeof(FWebGPUTransitionData) &&
        GRHITransitionPrivateData_AlignInBytes==alignof(FWebGPUTransitionData);
}
static bool WebGPUAccessSupported(FRHIViewableResource* Resource,ERHIAccess Access)
{
    constexpr ERHIAccess Supported=ERHIAccess::SRVMask|ERHIAccess::UAVMask|ERHIAccess::CopySrc|ERHIAccess::CopyDest|
        ERHIAccess::IndirectArgs|ERHIAccess::VertexOrIndexBuffer|ERHIAccess::RTV|ERHIAccess::DSVRead|ERHIAccess::DSVWrite|ERHIAccess::Discard;
    if(!IsValidAccess(Access) || EnumHasAnyFlags(Access,~Supported))return false;
    if(Resource->GetType()==RRT_Buffer) {
        const auto Usage=static_cast<FWebGPUBuffer*>(Resource)->Resource.Usage;
        if(EnumHasAnyFlags(Access,ERHIAccess::RTV|ERHIAccess::DSVRead|ERHIAccess::DSVWrite))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::SRVMask|ERHIAccess::UAVMask) && !(Usage&WGPUBufferUsage_Storage))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::IndirectArgs) && !(Usage&WGPUBufferUsage_Indirect))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::VertexOrIndexBuffer) && !(Usage&(WGPUBufferUsage_Vertex|WGPUBufferUsage_Index)))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::CopySrc) && !(Usage&WGPUBufferUsage_CopySrc))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::CopyDest) && !(Usage&WGPUBufferUsage_CopyDst))return false;
        return true;
    }
    if(Resource->GetType()==RRT_Texture) {
        const auto Usage=static_cast<FWebGPUTexture*>(Resource)->Resource.Usage;
        if(EnumHasAnyFlags(Access,ERHIAccess::IndirectArgs|ERHIAccess::VertexOrIndexBuffer))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::SRVMask) && !(Usage&WGPUTextureUsage_TextureBinding))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::UAVMask) && !(Usage&WGPUTextureUsage_StorageBinding))return false;
        const auto Format=static_cast<FWebGPUTexture*>(Resource)->Resource.Format;
        const bool Depth=Format==WGPUTextureFormat_Depth32Float || Format==WGPUTextureFormat_Depth24PlusStencil8;
        if(EnumHasAnyFlags(Access,ERHIAccess::RTV) && (Depth || !(Usage&WGPUTextureUsage_RenderAttachment)))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::DSVRead|ERHIAccess::DSVWrite) && (!Depth || !(Usage&WGPUTextureUsage_RenderAttachment)))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::CopySrc) && !(Usage&WGPUTextureUsage_CopySrc))return false;
        if(EnumHasAnyFlags(Access,ERHIAccess::CopyDest) && !(Usage&WGPUTextureUsage_CopyDst))return false;
        return true;
    }
    return false;
}
bool CreateWebGPUTransition(FRHITransition* Transition,const FRHITransitionCreateInfo& Info)
{
    const auto Flags=ERHITransitionCreateFlags::NoFence|ERHITransitionCreateFlags::NoSplit;
    if(!Transition || !WebGPUTransitionStorageValid() || Info.SrcPipelines!=ERHIPipeline::Graphics || Info.DstPipelines!=ERHIPipeline::Graphics ||
       EnumHasAnyFlags(Info.Flags,~Flags) || !Info.AliasingInfos.IsEmpty())return false;
    TArray<TRefCountPtr<FRHIResource>> Retained;
    for(const auto& Item:Info.TransitionInfos) {
        if(!Item.Resource || Item.CommitInfo.IsSet() || EnumHasAnyFlags(Item.Flags,~EResourceTransitionFlags::IgnoreAfterState))return false;
        const auto Type=Item.Resource->GetType();
        if(!((Item.Type==FRHITransitionInfo::EType::Buffer && Type==RRT_Buffer) ||
             (Item.Type==FRHITransitionInfo::EType::Texture && Type==RRT_Texture) ||
             (Item.Type==FRHITransitionInfo::EType::UAV && Type==RRT_UnorderedAccessView)))return false;
        auto* Resource=GetViewableResource(Item);
        if(!Resource || !WebGPUAccessSupported(Resource,Item.AccessBefore) || !WebGPUAccessSupported(Resource,Item.AccessAfter))return false;
        if(Resource->GetType()==RRT_Texture) {
            const auto& Desc=static_cast<FRHITexture*>(Resource)->GetDesc();
            if((!Item.IsAllMips() && Item.MipIndex>=Desc.NumMips) || (!Item.IsAllArraySlices() && Item.ArraySlice>=Desc.ArraySize) ||
               (!Item.IsAllPlaneSlices() && Item.PlaneSlice!=0))return false;
        }else if(!Item.IsWholeResource())return false;
        Retained.Add(Item.Resource);
    }
    auto* Data=new(Transition->GetPrivateData<FWebGPUTransitionData>()) FWebGPUTransitionData;
    Data->Resources=MoveTemp(Retained);return true;
}
void ReleaseWebGPUTransition(FRHITransition* Transition)
{
    RequireWebGPUCommand(Transition && WebGPUTransitionStorageValid(),TEXT("transition release storage"));
    Transition->GetPrivateData<FWebGPUTransitionData>()->~FWebGPUTransitionData();
}
static void AdvanceWebGPUTransitions(TArrayView<const FRHITransition*> Transitions,uint8 Before,uint8 After)
{
    RequireWebGPUCommand(WebGPUTransitionStorageValid(),TEXT("transition private storage"));
    for(int32 I=0;I<Transitions.Num();++I) {
        RequireWebGPUCommand(Transitions[I] && Transitions[I]->GetPrivateData<FWebGPUTransitionData>()->Phase==Before,TEXT("transition begin/end order"));
        for(int32 J=0;J<I;++J)RequireWebGPUCommand(Transitions[I]!=Transitions[J],TEXT("duplicate transition"));
    }
    // Commands are submitted immediately to the same WebGPU queue. WebGPU/Dawn
    // inserts resource barriers between passes; no CPU wait is necessary.
    for(const auto* Transition:Transitions)Transition->GetPrivateData<FWebGPUTransitionData>()->Phase=After;
}
void FWebGPUComputeContext::RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions)
{
    AdvanceWebGPUTransitions(Transitions,0,1);
}
void FWebGPUComputeContext::RHIEndTransitions(TArrayView<const FRHITransition*> Transitions)
{
    AdvanceWebGPUTransitions(Transitions,1,2);
}
#if WITH_RHI_BREADCRUMBS
void FWebGPUComputeContext::RHIBeginBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb)
{
    RequireWebGPUCommand(Breadcrumb!=nullptr,TEXT("null GPU breadcrumb"));
    FRHIBreadcrumbNode::FBuffer Name;
    RequireWebGPUCommand(Device.PushDebugGroup(TCHAR_TO_UTF8(Breadcrumb->GetTCHAR(Name))),TEXT("GPU breadcrumb begin"));
    Breadcrumbs.Add(Breadcrumb);
}
void FWebGPUComputeContext::RHIEndBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb)
{
    RequireWebGPUCommand(!Breadcrumbs.IsEmpty() && Breadcrumbs.Last()==Breadcrumb && Device.PopDebugGroup(),TEXT("GPU breadcrumb end order"));
    Breadcrumbs.Pop();
}
#endif
static_assert(!std::is_abstract_v<FWebGPUComputeContext>,"WebGPU compute context must implement Unreal's required methods");

void FWebGPUGraphicsContext::OutsideRenderPass() const
{
    RequireWebGPUCommand(!Pass,TEXT("operation inside an active graphics render pass"));
}
void FWebGPUGraphicsContext::ApplyStaticUniformBuffers()
{
    if(!Pipeline)return;
    auto Apply=[&](FRHIShader* Shader,FWebGPUShaderParameterState& Parameters) {
        const auto& Slots=Shader->GetStaticSlots();
        for(int32 I=0;I<Slots.Num();++I)if(IsUniformBufferStaticSlotValid(Slots[I]))
            RequireWebGPUCommand(Slots[I]<StaticUniforms.size() && Parameters.SetUniformBuffer(I,StaticUniforms[Slots[I]].GetReference()),TEXT("graphics static uniform buffer"));
    };
    Apply(Pipeline->Vertex,VertexParameters);Apply(Pipeline->Pixel,PixelParameters);
}
void FWebGPUGraphicsContext::RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& Bindings)
{
    Compute.RHISetStaticUniformBuffers(Bindings);StaticUniforms={};
    for(int32 I=0;I<Bindings.GetUniformBufferCount();++I)StaticUniforms[Bindings.GetSlot(I)]=static_cast<FWebGPUUniformBuffer*>(Bindings.GetUniformBuffer(I));
    ApplyStaticUniformBuffers();
}
void FWebGPUGraphicsContext::RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot,FRHIUniformBuffer* Buffer)
{
    Compute.RHISetStaticUniformBuffer(Slot,Buffer);StaticUniforms[Slot]=static_cast<FWebGPUUniformBuffer*>(Buffer);ApplyStaticUniformBuffers();
}
void FWebGPUGraphicsContext::RHISetStreamSource(uint32 Slot,FRHIBuffer* Buffer,uint32 Offset)
{
    RequireWebGPUCommand(Slot<Streams.size() && (!Buffer || (EnumHasAnyFlags(Buffer->GetUsage(),EBufferUsageFlags::VertexBuffer)&&Offset<Buffer->GetSize()&&Offset%4==0)),TEXT("vertex stream"));
    Streams[Slot]=static_cast<FWebGPUBuffer*>(Buffer);StreamOffsets[Slot]=Offset;
}
void FWebGPUGraphicsContext::RHISetViewport(float MinX,float MinY,float MinZ,float MaxX,float MaxY,float MaxZ)
{
    RequireWebGPUCommand(FMath::IsFinite(MinX)&&FMath::IsFinite(MinY)&&FMath::IsFinite(MinZ)&&FMath::IsFinite(MaxX)&&FMath::IsFinite(MaxY)&&FMath::IsFinite(MaxZ)&&
        MinX>=0&&MinY>=0&&MaxX>=MinX&&MaxY>=MinY&&MinZ>=0&&MaxZ<=1&&MaxZ>=MinZ,TEXT("viewport bounds"));
    DynamicState.HasViewport=true;DynamicState.X=MinX;DynamicState.Y=MinY;DynamicState.Width=MaxX-MinX;DynamicState.Height=MaxY-MinY;
    DynamicState.MinDepth=MinZ;DynamicState.MaxDepth=MaxZ;
}
void FWebGPUGraphicsContext::RHISetMultipleViewports(uint32 Count,const FViewportBounds* Bounds)
{
    RequireWebGPUCommand(Count==1&&Bounds,TEXT("multiple viewports"));const auto& B=*Bounds;
    RHISetViewport(B.TopLeftX,B.TopLeftY,B.MinDepth,B.TopLeftX+B.Width,B.TopLeftY+B.Height,B.MaxDepth);
}
void FWebGPUGraphicsContext::RHISetScissorRect(bool Enable,uint32 MinX,uint32 MinY,uint32 MaxX,uint32 MaxY)
{
    RequireWebGPUCommand(!Enable||(MaxX>=MinX&&MaxY>=MinY),TEXT("scissor bounds"));
    DynamicState.HasScissor=Enable;DynamicState.ScissorX=MinX;DynamicState.ScissorY=MinY;DynamicState.ScissorWidth=MaxX-MinX;DynamicState.ScissorHeight=MaxY-MinY;
}
void FWebGPUGraphicsContext::RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* In,uint32 StencilRef,bool ApplyAdditional)
{
    RequireWebGPUCommand(In!=nullptr,TEXT("null graphics pipeline"));Pipeline=static_cast<FWebGPUGraphicsPipelineState*>(In);
    RequireWebGPUCommand(Pipeline->Resource.Handle!=nullptr,TEXT("uninitialized graphics pipeline"));
    VertexParameters.SetShader(Pipeline->Vertex,&Pipeline->Vertex->Resource.Code);PixelParameters.SetShader(Pipeline->Pixel,&Pipeline->Pixel->Resource.Code);
    DynamicState.StencilReference=StencilRef;
    if(ApplyAdditional)ApplyStaticUniformBuffers();
}
#if PLATFORM_USE_FALLBACK_PSO
void FWebGPUGraphicsContext::RHISetGraphicsPipelineState(const FGraphicsPipelineStateInitializer& In,uint32 StencilRef,bool ApplyAdditional)
{
    FString Error;auto NewPipeline=CreateWebGPUGraphicsPipelineState(Device,In,Error);
    RequireWebGPUCommand(NewPipeline!=nullptr,*Error);RHISetGraphicsPipelineState(NewPipeline,StencilRef,ApplyAdditional);
}
#endif
void FWebGPUGraphicsContext::RHISetShaderParameters(FRHIGraphicsShader* Shader,TConstArrayView<uint8> Data,TConstArrayView<FRHIShaderParameter> Parameters,
    TConstArrayView<FRHIShaderParameterResource> Resources,TConstArrayView<FRHIShaderParameterResource> Bindless)
{
    RequireWebGPUCommand(Pipeline!=nullptr,TEXT("graphics parameters without a pipeline"));
    auto* State=Shader==Pipeline->Vertex.GetReference()?&VertexParameters:Shader==Pipeline->Pixel.GetReference()?&PixelParameters:nullptr;
    RequireWebGPUCommand(State&&State->SetParameters(Data,Parameters,Resources,Bindless),TEXT("graphics shader parameters"));
}
uint32 FWebGPUGraphicsContext::VertexCount(uint32 Primitives) const
{
    RequireWebGPUCommand(Pipeline!=nullptr,TEXT("draw without a graphics pipeline"));
    uint32 Factor=Pipeline->Primitive==PT_TriangleList?3:Pipeline->Primitive==PT_LineList?2:Pipeline->Primitive==PT_PointList?1:0;
    RequireWebGPUCommand(Factor&&Primitives<=MAX_uint32/Factor,TEXT("primitive count"));return Primitives*Factor;
}
void FWebGPUGraphicsContext::Draw(const UEWebGPU::DrawArguments& Arguments)
{
    RequireWebGPUCommand(Pass&&Pass->Handle&&Pipeline,TEXT("draw outside render pass"));
    FWebGPUPreparedParameters Vertex,Pixel;
    RequireWebGPUCommand(VertexParameters.Prepare(Device,Vertex)&&PixelParameters.Prepare(Device,Pixel),TEXT("graphics parameter preparation"));
    std::vector<UEWebGPU::VertexBufferBinding> Buffers;
    for(uint32 Slot:Pipeline->Declaration->Streams) {
        RequireWebGPUCommand(Slot<Streams.size()&&Streams[Slot],TEXT("missing vertex stream"));
        Buffers.push_back({&Streams[Slot]->Resource,StreamOffsets[Slot]});
    }
    RequireWebGPUCommand(Device.DrawInRenderPass(*Pass,Pipeline->Resource,Vertex.GetBindings(),Pixel.GetBindings(),Buffers.data(),Buffers.size(),Arguments,DynamicState),TEXT("graphics draw"));
}
void FWebGPUGraphicsContext::RHIDrawPrimitive(uint32 First,uint32 Primitives,uint32 Instances)
{
    UEWebGPU::DrawArguments Args;Args.Count=VertexCount(Primitives);Args.First=First;Args.Instances=Instances;Draw(Args);
}
void FWebGPUGraphicsContext::RHIDrawIndexedPrimitive(FRHIBuffer* Indices,int32 BaseVertex,uint32 FirstInstance,uint32 NumVertices,uint32 StartIndex,uint32 Primitives,uint32 Instances)
{
    RequireWebGPUCommand(Indices&&(Indices->GetStride()==2||Indices->GetStride()==4),TEXT("index buffer format"));
    UEWebGPU::DrawArguments Args;Args.Count=VertexCount(Primitives);Args.First=StartIndex;Args.Instances=Instances;Args.BaseVertex=BaseVertex;Args.FirstInstance=FirstInstance;
    Args.Indices=&static_cast<FWebGPUBuffer*>(Indices)->Resource;Args.IndexFormat=Indices->GetStride()==2?WGPUIndexFormat_Uint16:WGPUIndexFormat_Uint32;
    Draw(Args); // NumVertices is Unreal's index-range hint; WebGPU bounds checks the actual fetches.
}
static WGPULoadOp WebGPULoad(ERenderTargetActions Actions)
{
    // Undefined previous contents may be initialized deterministically to zero.
    return GetLoadAction(Actions)==ERenderTargetLoadAction::ELoad?WGPULoadOp_Load:WGPULoadOp_Clear;
}
static WGPUStoreOp WebGPUStore(ERenderTargetActions Actions)
{
    const auto Store=GetStoreAction(Actions);
    RequireWebGPUCommand(Store==ERenderTargetStoreAction::EStore||Store==ERenderTargetStoreAction::ENoAction,TEXT("multisample attachment resolve"));
    return Store==ERenderTargetStoreAction::EStore?WGPUStoreOp_Store:WGPUStoreOp_Discard;
}
void FWebGPUGraphicsContext::RHIBeginRenderPass(const FRHIRenderPassInfo& Info,const TCHAR* Name)
{
    OutsideRenderPass();
    RequireWebGPUCommand(!Info.MultiViewCount&&!Info.ShadingRateTexture&&!Info.bOcclusionQueries&&!Info.NumOcclusionQueries&&Info.SubpassHint==ESubpassHint::None,TEXT("unsupported render-pass options"));
    std::vector<WGPURenderPassColorAttachment> Colors;WGPURenderPassDepthStencilAttachment Depth=WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    uint32 Width=0,Height=0;
    auto View=[&](FRHITexture* In,uint32 Mip,int32 Slice,bool IsDepth) {
        auto* Texture=static_cast<FWebGPUTexture*>(In);RequireWebGPUCommand(Texture!=nullptr,TEXT("null render attachment"));
        auto& R=Texture->Resource;
        RequireWebGPUCommand(Mip<R.Mips && (R.Usage&WGPUTextureUsage_RenderAttachment) && (Slice>=0||R.Layers==1) && IsDepth==!bool(R.BytesPerTexel),TEXT("render attachment shape/usage"));
        uint32 W=FMath::Max(1u,R.Width>>Mip),H=FMath::Max(1u,R.Height>>Mip);
        RequireWebGPUCommand(!Width||(Width==W&&Height==H),TEXT("attachment extent mismatch"));Width=W;Height=H;
        auto V=std::make_unique<UEWebGPU::TextureView>();
        RequireWebGPUCommand(Device.CreateTextureView(*V,R,Mip,1,Slice<0?0:Slice,1,false),TEXT("render attachment view"));
        auto Handle=V->Handle;AttachmentViews.push_back(std::move(V));Attachments.Add(Texture);return Handle;
    };
    bool Gap=false;
    for(const auto& C:Info.ColorRenderTargets) {
        if(!C.RenderTarget){Gap=true;continue;}
        RequireWebGPUCommand(!Gap&&!C.ResolveTarget,TEXT("sparse or resolved color attachments"));
        auto A=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;A.view=View(C.RenderTarget,C.MipIndex,C.ArraySlice,false);A.loadOp=WebGPULoad(C.Action);A.storeOp=WebGPUStore(C.Action);
        if(GetLoadAction(C.Action)==ERenderTargetLoadAction::EClear){auto Color=C.RenderTarget->GetClearColor();A.clearValue={Color.R,Color.G,Color.B,Color.A};}
        Colors.push_back(A);
    }
    auto Desc=WGPU_RENDER_PASS_DESCRIPTOR_INIT;Desc.colorAttachmentCount=Colors.size();Desc.colorAttachments=Colors.data();
    const auto& D=Info.DepthStencilRenderTarget;
    if(D.DepthStencilTarget) {
        RequireWebGPUCommand(!D.ResolveTarget,TEXT("depth resolve"));Depth.view=View(D.DepthStencilTarget,0,-1,true);
        const auto DepthAction=GetDepthActions(D.Action),StencilAction=GetStencilActions(D.Action);
        Depth.depthReadOnly=!D.ExclusiveDepthStencil.IsDepthWrite();
        Depth.stencilReadOnly=!D.ExclusiveDepthStencil.IsStencilWrite();
        const bool Stencil=D.DepthStencilTarget->GetDesc().Format==PF_DepthStencil;
        if(!Depth.depthReadOnly){Depth.depthLoadOp=WebGPULoad(DepthAction);Depth.depthStoreOp=WebGPUStore(DepthAction);Depth.depthClearValue=0;}
        else RequireWebGPUCommand(GetLoadAction(DepthAction)!=ERenderTargetLoadAction::EClear,TEXT("clear of read-only depth"));
        if(Stencil&&!Depth.stencilReadOnly){Depth.stencilLoadOp=WebGPULoad(StencilAction);Depth.stencilStoreOp=WebGPUStore(StencilAction);}
        else RequireWebGPUCommand(GetLoadAction(StencilAction)!=ERenderTargetLoadAction::EClear,TEXT("clear of read-only or absent stencil"));
        if(GetLoadAction(DepthAction)==ERenderTargetLoadAction::EClear || GetLoadAction(StencilAction)==ERenderTargetLoadAction::EClear) {
            float DepthValue;uint32 StencilValue;D.DepthStencilTarget->GetDepthStencilClearValue(DepthValue,StencilValue);
            if(!Depth.depthReadOnly)Depth.depthClearValue=DepthValue;
            if(Stencil&&!Depth.stencilReadOnly)Depth.stencilClearValue=StencilValue;
        }
        Desc.depthStencilAttachment=&Depth;
    }
    RequireWebGPUCommand(Width&&Height,TEXT("render pass without attachments"));
    FTCHARToUTF8 Label(Name?Name:TEXT("Unreal render pass"));Desc.label={Label.Get(),size_t(Label.Length())};
    Pass=std::make_unique<UEWebGPU::RenderPass>();
    RequireWebGPUCommand(Device.BeginRenderPass(*Pass,Desc,Width,Height),TEXT("render pass begin"));
}
void FWebGPUGraphicsContext::RHIEndRenderPass()
{
    RequireWebGPUCommand(Pass&&Device.EndRenderPass(*Pass),TEXT("render pass end"));Pass.reset();AttachmentViews.clear();Attachments.Empty();
}
void FWebGPUGraphicsContext::RHICopyBufferRegion(FRHIBuffer* Destination,uint64 DestinationOffset,FRHIBuffer* Source,uint64 SourceOffset,uint64 Bytes)
{
    OutsideRenderPass();RequireWebGPUCommand(Source&&Destination&&Device.CopyBufferRegion(static_cast<FWebGPUBuffer*>(Source)->Resource,SourceOffset,
        static_cast<FWebGPUBuffer*>(Destination)->Resource,DestinationOffset,Bytes),TEXT("buffer region copy"));
}
void FWebGPUGraphicsContext::RHIDrawPrimitiveIndirect(FRHIBuffer*,uint32){RequireWebGPUCommand(false,TEXT("indirect graphics draw"));}
void FWebGPUGraphicsContext::RHIDrawIndexedPrimitiveIndirect(FRHIBuffer*,FRHIBuffer*,uint32){RequireWebGPUCommand(false,TEXT("indexed indirect graphics draw"));}
void FWebGPUGraphicsContext::RHIBeginRenderQuery(FRHIRenderQuery*){RequireWebGPUCommand(false,TEXT("render query begin"));}
void FWebGPUGraphicsContext::RHIEndRenderQuery(FRHIRenderQuery*){RequireWebGPUCommand(false,TEXT("render query end"));}
void FWebGPUGraphicsContext::RHISetDepthBounds(float,float){RequireWebGPUCommand(false,TEXT("depth bounds"));}
void FWebGPUGraphicsContext::RHICopyTexture(FRHITexture*,FRHITexture*,const FRHICopyTextureInfo&){RequireWebGPUCommand(false,TEXT("texture-to-texture copy"));}
static_assert(!std::is_abstract_v<FWebGPUGraphicsContext>,"WebGPU graphics context must implement Unreal's required methods");
