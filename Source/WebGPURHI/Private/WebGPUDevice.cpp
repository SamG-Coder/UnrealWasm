#include "WebGPUDevice.h"
#include "WebCudaShaderCode.h"
#include <cstring>
#include <limits>
#include <algorithm>
#include <cmath>

// Same native ABI as WebGPU's by-value string API; the typed forwarding shim
// supplies missing Wasm64 signature metadata without modifying Emdawn sources.
extern "C" void webcudaCommandEncoderPushDebugGroup(WGPUCommandEncoder,WGPUStringView);

namespace UEWebGPU {
static std::string Text(WGPUStringView Value) {
    return Value.data ? std::string(Value.data, Value.length == WGPU_STRLEN ? std::strlen(Value.data) : Value.length) : "WebGPU error";
}
Device::~Device() {
    if (Queue) wgpuQueueRelease(Queue);
    if (Handle) wgpuDeviceRelease(Handle);
    if (Adapter) wgpuAdapterRelease(Adapter);
    if (Instance) wgpuInstanceRelease(Instance);
}
void Device::Fail(WGPUStringView Message) { Error = Text(Message); Status = State::Failed; }
void Device::Initialize() {
    if (Status != State::Idle) return;
    Status = State::Pending;
    Instance = wgpuCreateInstance(nullptr);
    if (!Instance) { Error = "Could not create WebGPU instance"; Status = State::Failed; return; }
    auto Options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    Options.powerPreference = WGPUPowerPreference_HighPerformance;
    auto Callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    Callback.mode = WGPUCallbackMode_AllowSpontaneous;
    Callback.callback = AdapterReady; Callback.userdata1 = this;
    wgpuInstanceRequestAdapter(Instance, &Options, Callback);
}
void Device::AdapterReady(WGPURequestAdapterStatus Status, WGPUAdapter Adapter, WGPUStringView Message, void* User, void*) {
    auto& Self = *static_cast<Device*>(User);
    if (Status != WGPURequestAdapterStatus_Success) { Self.Fail(Message); return; }
    Self.Adapter = Adapter;
    auto Descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    WGPUFeatureName Features[2]; size_t FeatureCount = 0;
    Self.TextureFormatsTier1 = wgpuAdapterHasFeature(Adapter, WGPUFeatureName_TextureFormatsTier1);
    Self.RG11B10Renderable = wgpuAdapterHasFeature(Adapter, WGPUFeatureName_RG11B10UfloatRenderable);
    if (Self.TextureFormatsTier1) Features[FeatureCount++] = WGPUFeatureName_TextureFormatsTier1;
    if (Self.RG11B10Renderable) Features[FeatureCount++] = WGPUFeatureName_RG11B10UfloatRenderable;
    Descriptor.requiredFeatureCount = FeatureCount; Descriptor.requiredFeatures = Features;
    auto AdapterLimits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(Adapter, &AdapterLimits) != WGPUStatus_Success) { Self.Error = "Could not query WebGPU adapter limits"; Self.Status = State::Failed; return; }
    auto RequestedLimits = WGPU_LIMITS_INIT;
    // Lumen's integration pass needs six storage textures; WebGPU's default
    // device limit is only four even when the adapter supports more.
    RequestedLimits.maxStorageTexturesPerShaderStage = AdapterLimits.maxStorageTexturesPerShaderStage;
    Descriptor.requiredLimits = &RequestedLimits;
    Descriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView Message, void* User, void*) { static_cast<Device*>(User)->Fail(Message); };
    Descriptor.uncapturedErrorCallbackInfo.userdata1 = &Self;
    Descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    Descriptor.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason Reason, WGPUStringView Message, void* User, void*) {
        if (Reason != WGPUDeviceLostReason_Destroyed && Reason != WGPUDeviceLostReason_CallbackCancelled) static_cast<Device*>(User)->Fail(Message);
    };
    Descriptor.deviceLostCallbackInfo.userdata1 = &Self;
    auto Callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    Callback.mode = WGPUCallbackMode_AllowSpontaneous;
    Callback.callback = DeviceReady; Callback.userdata1 = &Self;
    wgpuAdapterRequestDevice(Adapter, &Descriptor, Callback);
}
void Device::DeviceReady(WGPURequestDeviceStatus Status, WGPUDevice Handle, WGPUStringView Message, void* User, void*) {
    auto& Self = *static_cast<Device*>(User);
    if (Status != WGPURequestDeviceStatus_Success) { Self.Fail(Message); return; }
    Self.Handle = Handle;
    if (wgpuDeviceGetLimits(Handle, &Self.Limits) != WGPUStatus_Success) { Self.Error = "Could not query WebGPU device limits"; Self.Status = State::Failed; return; }
    Self.Queue = wgpuDeviceGetQueue(Handle); Self.Status = State::Ready;
}
bool Device::CreateBuffer(Buffer& Out, uint64_t Size, WGPUBufferUsage Usage) {
    if (Status != State::Ready || Out.Handle || !Size || !Usage) return false;
    auto Descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    Descriptor.size = Size; Descriptor.usage = Usage;
    Out.Handle = wgpuDeviceCreateBuffer(Handle, &Descriptor); Out.Size = Size; Out.Usage = Usage;
    return Out.Handle != nullptr;
}
bool Device::WriteBuffer(const Buffer& Destination, uint64_t Offset, const void* Data, size_t Bytes) {
    if (Status != State::Ready || !Destination.Handle || !(Destination.Usage & WGPUBufferUsage_CopyDst) || !Data || Offset % 4 || Bytes % 4 || Offset > Destination.Size || Bytes > Destination.Size - Offset) return false;
    wgpuQueueWriteBuffer(Queue, Destination.Handle, Offset, Data, Bytes); return true;
}
bool Device::Submit(WGPUCommandEncoder Encoder) {
    if (!Encoder) return false;
    for(size_t I=0;I<DebugGroups.size();++I)wgpuCommandEncoderPopDebugGroup(Encoder);
    auto Commands = wgpuCommandEncoderFinish(Encoder, nullptr);
    wgpuCommandEncoderRelease(Encoder);
    if (!Commands) return false;
    wgpuQueueSubmit(Queue, 1, &Commands); wgpuCommandBufferRelease(Commands); return true;
}
WGPUCommandEncoder Device::CreateEncoder() {
    auto Encoder=wgpuDeviceCreateCommandEncoder(Handle,nullptr);
    // These owned strings are null-terminated, as are the shader source strings.
    if(Encoder)for(const auto& Group:DebugGroups)webcudaCommandEncoderPushDebugGroup(Encoder,{Group.c_str(),WGPU_STRLEN});
    return Encoder;
}
bool Device::PushDebugGroup(const char* Label) {
    if(Status!=State::Ready || !Label || DebugGroups.size()>=128 || std::strlen(Label)>1024)return false;
    DebugGroups.emplace_back(Label);return true;
}
bool Device::PopDebugGroup() {
    if(Status!=State::Ready || DebugGroups.empty())return false;
    DebugGroups.pop_back();return true;
}
bool Device::CopyBuffer(const Buffer& Source, const Buffer& Destination, uint64_t Bytes) {
    return CopyBufferRegion(Source,0,Destination,0,Bytes);
}
bool Device::CopyBufferRegion(const Buffer& Source,uint64_t SourceOffset,const Buffer& Destination,uint64_t DestinationOffset,uint64_t Bytes) {
    if (Status != State::Ready || !Source.Handle || !Destination.Handle || Source.Handle == Destination.Handle || !(Source.Usage & WGPUBufferUsage_CopySrc) || !(Destination.Usage & WGPUBufferUsage_CopyDst) ||
        Bytes%4 || SourceOffset%4 || DestinationOffset%4 || SourceOffset>Source.Size || DestinationOffset>Destination.Size || Bytes>Source.Size-SourceOffset || Bytes>Destination.Size-DestinationOffset) return false;
    auto Encoder = CreateEncoder();
    wgpuCommandEncoderCopyBufferToBuffer(Encoder, Source.Handle, SourceOffset, Destination.Handle, DestinationOffset, Bytes);
    return Submit(Encoder);
}
bool Device::ReadBuffer(const Buffer& Source, size_t Bytes, Readback& Out) {
    if (Status != State::Ready || !Source.Handle || !(Source.Usage & WGPUBufferUsage_MapRead) || !Bytes || Bytes % 4 || Bytes > Source.Size || !Out.Bytes.empty()) return false;
    Out.Bytes.resize(Bytes); Out.Complete = false; Out.Succeeded = false; Out.Error.clear();
    wgpuBufferAddRef(Source.Handle);
    auto Callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    Callback.mode = WGPUCallbackMode_AllowSpontaneous;
    Callback.userdata1 = &Out; Callback.userdata2 = Source.Handle;
    Callback.callback = [](WGPUMapAsyncStatus Status, WGPUStringView Message, void* User, void* BufferPointer) {
        auto& Result = *static_cast<Readback*>(User); auto BufferHandle = static_cast<WGPUBuffer>(BufferPointer);
        if (Status == WGPUMapAsyncStatus_Success) {
            const void* Data = wgpuBufferGetConstMappedRange(BufferHandle, 0, Result.Bytes.size());
            if (Data) { std::memcpy(Result.Bytes.data(), Data, Result.Bytes.size()); Result.Succeeded = true; }
            else Result.Error = "Mapped range unavailable";
            wgpuBufferUnmap(BufferHandle);
        } else Result.Error = Text(Message);
        wgpuBufferRelease(BufferHandle); Result.Complete = true;
    };
    wgpuBufferMapAsync(Source.Handle, WGPUMapMode_Read, 0, Bytes, Callback); return true;
}
bool Device::CreateTexture2D(Texture& Out, uint32_t Width, uint32_t Height, uint32_t Layers, uint32_t Mips, WGPUTextureFormat Format, WGPUTextureUsage Usage) {
    constexpr WGPUTextureUsage Supported = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_RenderAttachment;
    if (Status != State::Ready || Out.Handle || !Width || !Height || !Layers || !Mips || !Usage || (Usage & ~Supported) ||
        Width > Limits.maxTextureDimension2D || Height > Limits.maxTextureDimension2D || Layers > Limits.maxTextureArrayLayers) return false;
    uint32_t MaxMips = 1;
    for (uint32_t Size = std::max(Width, Height); Size > 1; Size >>= 1) ++MaxMips;
    if (Mips > MaxMips) return false;
    uint32_t Bytes = 0;
    switch (Format) {
        case WGPUTextureFormat_R8Unorm: Bytes = 1; break;
        case WGPUTextureFormat_RGBA8Unorm: case WGPUTextureFormat_R32Uint: case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_RGBA8UnormSrgb:case WGPUTextureFormat_BGRA8Unorm:case WGPUTextureFormat_BGRA8UnormSrgb:Bytes=4;break;
        // Opaque depth storage is not accessible through the color copy/clear helpers.
        case WGPUTextureFormat_Depth32Float:case WGPUTextureFormat_Depth24PlusStencil8:Bytes=0;break;
        case WGPUTextureFormat_RGBA16Float: Bytes = 8; break;
        case WGPUTextureFormat_RGBA32Float: Bytes = 16; break;
        default: return false;
    }
    if(!Bytes && (Usage&~(WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding)))return false;
    if((Usage&WGPUTextureUsage_StorageBinding) && (Format==WGPUTextureFormat_BGRA8Unorm || Format==WGPUTextureFormat_BGRA8UnormSrgb || Format==WGPUTextureFormat_RGBA8UnormSrgb))return false;
    if ((Usage & WGPUTextureUsage_StorageBinding) && (Format == WGPUTextureFormat_R8Unorm || Format == WGPUTextureFormat_RG11B10Ufloat) && !TextureFormatsTier1) return false;
    if ((Usage & WGPUTextureUsage_RenderAttachment) && Format == WGPUTextureFormat_RG11B10Ufloat && !RG11B10Renderable) return false;
    auto Descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    Descriptor.dimension = WGPUTextureDimension_2D;
    Descriptor.size = {Width, Height, Layers}; Descriptor.format = Format;
    Descriptor.mipLevelCount = Mips; Descriptor.usage = Usage;
    Out.Handle = wgpuDeviceCreateTexture(Handle, &Descriptor);
    Out.Width = Width; Out.Height = Height; Out.Layers = Layers; Out.Mips = Mips;
    Out.BytesPerTexel = Bytes; Out.Format = Format; Out.Usage = Usage;
    return Out.Handle != nullptr;
}
bool Device::CreateTextureView(TextureView& Out, const Texture& Source, uint32_t FirstMip, uint32_t MipCount, uint32_t FirstLayer, uint32_t LayerCount, bool ArrayView,WGPUTextureAspect Aspect) {
    if (Status != State::Ready || Out.Handle || !Source.Handle || !MipCount || !LayerCount ||
        FirstMip >= Source.Mips || MipCount > Source.Mips - FirstMip || FirstLayer >= Source.Layers || LayerCount > Source.Layers - FirstLayer || (!ArrayView && LayerCount != 1)) return false;
    const bool Depth=Source.Format==WGPUTextureFormat_Depth32Float || Source.Format==WGPUTextureFormat_Depth24PlusStencil8;
    if(Aspect!=WGPUTextureAspect_All && !(Depth&&Aspect==WGPUTextureAspect_DepthOnly) && !(Source.Format==WGPUTextureFormat_Depth24PlusStencil8&&Aspect==WGPUTextureAspect_StencilOnly))return false;
    auto Descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;Descriptor.aspect=Aspect;
    Descriptor.format = Source.Format; Descriptor.dimension = ArrayView ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
    if(Source.Format==WGPUTextureFormat_Depth24PlusStencil8 && Aspect==WGPUTextureAspect_DepthOnly)Descriptor.format=WGPUTextureFormat_Depth24Plus;
    if(Source.Format==WGPUTextureFormat_Depth24PlusStencil8 && Aspect==WGPUTextureAspect_StencilOnly)Descriptor.format=WGPUTextureFormat_Stencil8;
    Descriptor.baseMipLevel = FirstMip; Descriptor.mipLevelCount = MipCount;
    Descriptor.baseArrayLayer = FirstLayer; Descriptor.arrayLayerCount = LayerCount;
    Out.Handle = wgpuTextureCreateView(Source.Handle, &Descriptor);
    Out.Format=Descriptor.format;Out.Usage=Source.Usage;Out.ArrayView=ArrayView;Out.Aspect=Aspect;
    return Out.Handle != nullptr;
}
bool Device::CreateRGBA8Texture(Texture& Out, uint32_t Width, uint32_t Height) {
    return CreateTexture2D(Out,Width,Height,1,1,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding);
}
bool Device::ClearTexture(const Texture& Target, WGPUColor Color, uint32_t Mip, uint32_t Layer) {
    return ClearTextureLayers(Target,Color,Mip,Layer,1);
}
bool Device::ClearTextureLayers(const Texture& Target,WGPUColor Color,uint32_t Mip,uint32_t FirstLayer,uint32_t LayerCount) {
    if(Status!=State::Ready || !Target.Handle || Mip>=Target.Mips || FirstLayer>=Target.Layers || !LayerCount || LayerCount>Target.Layers-FirstLayer)return false;
    if (!Target.BytesPerTexel || !(Target.Usage & WGPUTextureUsage_RenderAttachment) || !std::isfinite(Color.r) || !std::isfinite(Color.g) || !std::isfinite(Color.b) || !std::isfinite(Color.a)) return false;
    auto Encoder = CreateEncoder();
    if(!Encoder)return false;
    for(uint32_t Layer=FirstLayer;Layer<FirstLayer+LayerCount;++Layer) {
        TextureView View;
        if(!CreateTextureView(View,Target,Mip,1,Layer,1,false)){wgpuCommandEncoderRelease(Encoder);return false;}
        auto ColorAttachment=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        ColorAttachment.view=View.Handle;ColorAttachment.loadOp=WGPULoadOp_Clear;
        ColorAttachment.storeOp=WGPUStoreOp_Store;ColorAttachment.clearValue=Color;
        auto Descriptor=WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        Descriptor.colorAttachmentCount=1;Descriptor.colorAttachments=&ColorAttachment;
        auto Pass=wgpuCommandEncoderBeginRenderPass(Encoder,&Descriptor);
        if(!Pass){wgpuCommandEncoderRelease(Encoder);return false;}
        wgpuRenderPassEncoderEnd(Pass);wgpuRenderPassEncoderRelease(Pass);
    }
    return Submit(Encoder);
}
bool Device::CopyTextureToBuffer(const Texture& Source, const Buffer& Destination, uint32_t BytesPerRow, uint32_t Mip, uint32_t FirstLayer, uint32_t LayerCount) {
    if (Status != State::Ready || !Source.BytesPerTexel || !Source.Handle || !Destination.Handle || !(Source.Usage & WGPUTextureUsage_CopySrc) || !(Destination.Usage & WGPUBufferUsage_CopyDst) ||
        Mip >= Source.Mips || FirstLayer >= Source.Layers || !LayerCount || LayerCount > Source.Layers - FirstLayer) return false;
    const uint32_t Width = std::max(1u, Source.Width >> Mip), Height = std::max(1u, Source.Height >> Mip);
    const uint64_t RowBytes = uint64_t(Width) * Source.BytesPerTexel;
    const uint64_t Required = uint64_t(BytesPerRow) * (uint64_t(Height) * LayerCount - 1) + RowBytes;
    if (BytesPerRow % 256 || BytesPerRow < RowBytes || Required > Destination.Size) return false;
    auto From = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; From.texture = Source.Handle; From.mipLevel = Mip; From.origin.z = FirstLayer;
    auto To = WGPU_TEXEL_COPY_BUFFER_INFO_INIT; To.buffer = Destination.Handle; To.layout.bytesPerRow = BytesPerRow; To.layout.rowsPerImage = Height;
    WGPUExtent3D Extent = {Width, Height, LayerCount};
    auto Encoder = CreateEncoder();
    wgpuCommandEncoderCopyTextureToBuffer(Encoder, &From, &To, &Extent);
    return Submit(Encoder);
}
bool Device::CopyBufferToTexture(const Buffer& Source,const Texture& Destination,uint32_t BytesPerRow,uint32_t Mip,uint32_t FirstLayer,uint32_t LayerCount) {
    if(Status!=State::Ready || !Destination.BytesPerTexel || !Source.Handle || !Destination.Handle || !(Source.Usage&WGPUBufferUsage_CopySrc) || !(Destination.Usage&WGPUTextureUsage_CopyDst) ||
       Mip>=Destination.Mips || FirstLayer>=Destination.Layers || !LayerCount || LayerCount>Destination.Layers-FirstLayer)return false;
    const uint32_t Width=std::max(1u,Destination.Width>>Mip),Height=std::max(1u,Destination.Height>>Mip);
    const uint64_t RowBytes=uint64_t(Width)*Destination.BytesPerTexel;
    const uint64_t Required=uint64_t(BytesPerRow)*(uint64_t(Height)*LayerCount-1)+RowBytes;
    if(BytesPerRow%256 || BytesPerRow<RowBytes || Required>Source.Size)return false;
    auto From=WGPU_TEXEL_COPY_BUFFER_INFO_INIT;From.buffer=Source.Handle;From.layout.bytesPerRow=BytesPerRow;From.layout.rowsPerImage=Height;
    auto To=WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;To.texture=Destination.Handle;To.mipLevel=Mip;To.origin.z=FirstLayer;
    WGPUExtent3D Extent={Width,Height,LayerCount};auto Encoder=CreateEncoder();
    wgpuCommandEncoderCopyBufferToTexture(Encoder,&From,&To,&Extent);return Submit(Encoder);
}
bool Device::CreateComputePipeline(ComputePipeline& Out, const char* WGSL, const char* Entry) {
    if (Status != State::Ready || Out.Handle || !WGSL || !Entry) return false;
    auto Source = WGPU_SHADER_SOURCE_WGSL_INIT; Source.code = {WGSL, WGPU_STRLEN};
    auto ShaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT; ShaderDesc.nextInChain = &Source.chain;
    auto Shader = wgpuDeviceCreateShaderModule(Handle, &ShaderDesc);
    if (!Shader) return false;
    auto Desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    Desc.compute.module = Shader; Desc.compute.entryPoint = {Entry, WGPU_STRLEN};
    Out.Handle = wgpuDeviceCreateComputePipeline(Handle, &Desc);
    wgpuShaderModuleRelease(Shader); return Out.Handle != nullptr;
}
bool Device::CreateRasterShader(RasterShader& Out,const ShaderCode& Code) {
    if(Status!=State::Ready || Out.Handle || Code.Version!=3 || (Code.Stage!=2&&Code.Stage!=3) || Code.WGSL.empty() || Code.Entry.empty())return false;
    auto Source=WGPU_SHADER_SOURCE_WGSL_INIT;Source.code={Code.WGSL.c_str(),Code.WGSL.size()};
    auto Desc=WGPU_SHADER_MODULE_DESCRIPTOR_INIT;Desc.nextInChain=&Source.chain;
    Out.Handle=wgpuDeviceCreateShaderModule(Handle,&Desc);
    if(!Out.Handle)return false;
    Out.Code=Code;return true;
}
bool Device::CreateRenderPipeline(RenderPipeline& Out,const RasterShader& Vertex,const RasterShader& Pixel,const WGPURenderPipelineDescriptor& State) {
    if(Status!=State::Ready || Out.Handle || !Vertex.Handle || !Pixel.Handle || Vertex.Code.Stage!=2 || Pixel.Code.Stage!=3 || !State.fragment)return false;
    auto Desc=State;auto Fragment=*State.fragment;
    Desc.vertex.module=Vertex.Handle;Desc.vertex.entryPoint={Vertex.Code.Entry.c_str(),Vertex.Code.Entry.size()};
    Fragment.module=Pixel.Handle;Fragment.entryPoint={Pixel.Code.Entry.c_str(),Pixel.Code.Entry.size()};Desc.fragment=&Fragment;
    Out.Handle=wgpuDeviceCreateRenderPipeline(Handle,&Desc);
    if(!Out.Handle)return false;
    Out.Vertex=Vertex.Code;Out.Pixel=Pixel.Code;return true;
}
bool Device::Draw(const RenderPipeline& Pipeline,const WGPURenderPassDescriptor& PassDesc,const StageBindings& Vertex,const StageBindings& Pixel,
                  const VertexBufferBinding* Buffers,size_t BufferCount,const DrawArguments& Args,const DrawState& State) {
    RenderPass Pass;
    if(!BeginRenderPass(Pass,PassDesc) || !DrawInRenderPass(Pass,Pipeline,Vertex,Pixel,Buffers,BufferCount,Args,State))return false;
    return EndRenderPass(Pass);
}
bool Device::BeginRenderPass(RenderPass& Out,const WGPURenderPassDescriptor& Descriptor,uint32_t Width,uint32_t Height) {
    if(Status!=State::Ready || Out.Encoder || Out.Handle || bool(Width)!=bool(Height))return false;
    Out.Encoder=CreateEncoder();if(!Out.Encoder)return false;
    Out.Handle=wgpuCommandEncoderBeginRenderPass(Out.Encoder,&Descriptor);
    Out.Width=Width;Out.Height=Height;Out.DebugDepth=DebugGroups.size();return Out.Handle!=nullptr;
}
bool Device::EndRenderPass(RenderPass& Pass) {
    if(Status!=State::Ready || !Pass.Encoder || !Pass.Handle)return false;
    wgpuRenderPassEncoderEnd(Pass.Handle);wgpuRenderPassEncoderRelease(Pass.Handle);Pass.Handle=nullptr;
    // Use the encoder's captured debug depth; breadcrumbs can change between
    // Begin/End without changing which encoder-level markers were pushed.
    for(size_t I=0;I<Pass.DebugDepth;++I)wgpuCommandEncoderPopDebugGroup(Pass.Encoder);
    auto Commands=wgpuCommandEncoderFinish(Pass.Encoder,nullptr);wgpuCommandEncoderRelease(Pass.Encoder);Pass.Encoder=nullptr;
    if(!Commands)return false;
    wgpuQueueSubmit(Queue,1,&Commands);wgpuCommandBufferRelease(Commands);return true;
}
bool Device::DrawInRenderPass(RenderPass& RenderPass,const RenderPipeline& Pipeline,const StageBindings& Vertex,const StageBindings& Pixel,
                  const VertexBufferBinding* Buffers,size_t BufferCount,const DrawArguments& Args,const DrawState& State) {
    if(Status!=State::Ready || !RenderPass.Handle || !Pipeline.Handle || BufferCount>Limits.maxVertexBuffers || (BufferCount&&!Buffers))return false;
    if(!std::isfinite(State.BlendConstant.r) || !std::isfinite(State.BlendConstant.g) || !std::isfinite(State.BlendConstant.b) || !std::isfinite(State.BlendConstant.a))return false;
    if(State.HasViewport && (!std::isfinite(State.X) || !std::isfinite(State.Y) || !std::isfinite(State.Width) || !std::isfinite(State.Height) ||
       !std::isfinite(State.MinDepth) || !std::isfinite(State.MaxDepth) || State.X<0 || State.Y<0 || State.Width<0 || State.Height<0 ||
       State.MinDepth<0 || State.MaxDepth>1 || State.MinDepth>State.MaxDepth))return false;
    if(State.HasScissor && (uint64_t(State.ScissorX)+State.ScissorWidth>UINT32_MAX || uint64_t(State.ScissorY)+State.ScissorHeight>UINT32_MAX))return false;
    for(size_t I=0;I<BufferCount;++I) {
        const auto& B=Buffers[I];
        if(!B.Resource || !B.Resource->Handle || !(B.Resource->Usage&WGPUBufferUsage_Vertex) || B.Offset%4 || B.Offset>=B.Resource->Size ||
           (B.Bytes && (B.Bytes>B.Resource->Size-B.Offset || B.Bytes%4)))return false;
    }
    uint64_t IndexBytes=0;
    if(Args.Indices) {
        const uint32_t Stride=Args.IndexFormat==WGPUIndexFormat_Uint16?2:Args.IndexFormat==WGPUIndexFormat_Uint32?4:0;
        if(!Stride || !Args.Indices->Handle || !(Args.Indices->Usage&WGPUBufferUsage_Index) || Args.IndexOffset%Stride || Args.IndexOffset>=Args.Indices->Size)return false;
        IndexBytes=Args.IndexBytes?Args.IndexBytes:Args.Indices->Size-Args.IndexOffset;
        if(IndexBytes>Args.Indices->Size-Args.IndexOffset || IndexBytes%Stride || (uint64_t(Args.First)+Args.Count)*Stride>IndexBytes)return false;
    }else if(Args.BaseVertex || Args.IndexOffset || Args.IndexBytes || Args.IndexFormat!=WGPUIndexFormat_Undefined)return false;
    std::vector<ResourceBinding> Resolved[2];std::vector<WGPUBindGroupEntry> Entries[2];
    if(!ResolveShaderBindings(Pipeline.Vertex,Vertex,Resolved[0]) || !ResolveShaderBindings(Pipeline.Pixel,Pixel,Resolved[1]))return false;
    for(uint32_t I=0;I<2;++I)if(!MakeBindingEntries(Resolved[I].data(),Resolved[I].size(),Entries[I]))return false;
    WGPUBindGroup Groups[2]{};
    for(uint32_t I=0;I<2;++I) {
        // An unused stage has no group to bind. Group 1 remains the pixel group
        // even when the vertex shader has no resources.
        if(Entries[I].empty())continue;
        auto Layout=wgpuRenderPipelineGetBindGroupLayout(Pipeline.Handle,I);
        auto Desc=WGPU_BIND_GROUP_DESCRIPTOR_INIT;Desc.layout=Layout;Desc.entryCount=Entries[I].size();Desc.entries=Entries[I].data();
        Groups[I]=wgpuDeviceCreateBindGroup(Handle,&Desc);wgpuBindGroupLayoutRelease(Layout);
        if(!Groups[I]) {for(auto Group:Groups)if(Group)wgpuBindGroupRelease(Group);return false;}
    }
    auto Pass=RenderPass.Handle;
    wgpuRenderPassEncoderSetPipeline(Pass,Pipeline.Handle);
    if(State.HasViewport)wgpuRenderPassEncoderSetViewport(Pass,State.X,State.Y,State.Width,State.Height,State.MinDepth,State.MaxDepth);
    else if(RenderPass.Width)wgpuRenderPassEncoderSetViewport(Pass,0,0,float(RenderPass.Width),float(RenderPass.Height),0,1);
    if(State.HasScissor)wgpuRenderPassEncoderSetScissorRect(Pass,State.ScissorX,State.ScissorY,State.ScissorWidth,State.ScissorHeight);
    else if(RenderPass.Width)wgpuRenderPassEncoderSetScissorRect(Pass,0,0,RenderPass.Width,RenderPass.Height);
    wgpuRenderPassEncoderSetStencilReference(Pass,State.StencilReference);
    wgpuRenderPassEncoderSetBlendConstant(Pass,&State.BlendConstant);
    for(uint32_t I=0;I<2;++I)if(Groups[I])wgpuRenderPassEncoderSetBindGroup(Pass,I,Groups[I],0,nullptr);
    for(size_t I=0;I<BufferCount;++I) {
        const auto& B=Buffers[I];wgpuRenderPassEncoderSetVertexBuffer(Pass,uint32_t(I),B.Resource->Handle,B.Offset,B.Bytes?B.Bytes:B.Resource->Size-B.Offset);
    }
    if(Args.Indices) {
        wgpuRenderPassEncoderSetIndexBuffer(Pass,Args.Indices->Handle,Args.IndexFormat,Args.IndexOffset,IndexBytes);
        wgpuRenderPassEncoderDrawIndexed(Pass,Args.Count,Args.Instances,Args.First,Args.BaseVertex,Args.FirstInstance);
    }else wgpuRenderPassEncoderDraw(Pass,Args.Count,Args.Instances,Args.First,Args.FirstInstance);
    for(auto Group:Groups)if(Group)wgpuBindGroupRelease(Group);
    return true;
}
bool Device::Dispatch(const ComputePipeline& Pipeline, const BufferBinding* Bindings, size_t Count, uint32_t X, uint32_t Y, uint32_t Z) {
    if (!Bindings || !Count || Count>Limits.maxBindingsPerBindGroup) return false;
    std::vector<ResourceBinding> Resources;
    Resources.reserve(Count);
    for(size_t I=0;I<Count;++I) Resources.push_back({Bindings[I].Binding,Bindings[I].Resource});
    return Dispatch(Pipeline,Resources.data(),Count,X,Y,Z);
}
bool Device::DispatchShader(const ComputePipeline& Pipeline, const ShaderCode& Code, const ResourceBinding* Bindings, size_t Count,
                            const UniformResourceTable* Tables, size_t TableCount, uint32_t X, uint32_t Y, uint32_t Z) {
    return DispatchShaderCommands(Pipeline,Code,Bindings,Count,Tables,TableCount,X,Y,Z,nullptr,0);
}
bool Device::DispatchShaderIndirect(const ComputePipeline& Pipeline,const ShaderCode& Code,const ResourceBinding* Bindings,size_t Count,
                            const UniformResourceTable* Tables,size_t TableCount,const Buffer& Arguments,uint64_t Offset) {
    return DispatchShaderCommands(Pipeline,Code,Bindings,Count,Tables,TableCount,0,0,0,&Arguments,Offset);
}
bool Device::DispatchShaderCommands(const ComputePipeline& Pipeline,const ShaderCode& Code,const ResourceBinding* Bindings,size_t Count,
                            const UniformResourceTable* Tables,size_t TableCount,uint32_t X,uint32_t Y,uint32_t Z,const Buffer* Arguments,uint64_t Offset) {
    std::vector<ResourceBinding> Resolved;
    if(Code.Version!=2 || Code.Stage!=1 || !ResolveShaderBindings(Code,{Bindings,Count,Tables,TableCount},Resolved))return false;
    return DispatchCommands(Pipeline,Resolved.data(),Resolved.size(),X,Y,Z,Arguments,Offset);
}
bool Device::ResolveShaderBindings(const ShaderCode& Code,const StageBindings& Input,std::vector<ResourceBinding>& Resolved) {
    const auto* Bindings=Input.Resources;const size_t Count=Input.Count,TableCount=Input.TableCount;const auto* Tables=Input.Tables;
    if(Status!=State::Ready || Count>64 || TableCount>32 || (Count&&!Bindings) || (TableCount&&!Tables))return false;
    Resolved.clear();
    if(Count)Resolved.assign(Bindings,Bindings+Count);
    uint32_t SeenTables=0;
    for(size_t I=0;I<TableCount;++I) {
        const auto& T=Tables[I];
        if(T.Slot>=32 || T.Slot>=Code.ResourceTableLayoutHashes.size() || (SeenTables&(1u<<T.Slot)) ||
           T.LayoutHash!=Code.ResourceTableLayoutHashes[T.Slot] || (T.Count&&!T.Resources))return false;
        SeenTables|=1u<<T.Slot;
    }
    if((SeenTables&Code.ResourceTableBits)!=Code.ResourceTableBits)return false;
    for(const auto& Entry:Code.ResourceTableBindings) {
        const UniformResourceTable* Table=nullptr;
        for(size_t I=0;I<TableCount;++I)if(Tables[I].Slot==Entry.UniformSlot)Table=&Tables[I];
        if(!Table || Entry.ResourceIndex>=Table->Count)return false;
        auto Binding=Table->Resources[Entry.ResourceIndex];Binding.Binding=Entry.Binding;
        Resolved.push_back(Binding);
    }
    if(Resolved.size()!=Code.Resources.size())return false;
    for(const auto& R:Code.Resources) {
        const ResourceBinding* B=nullptr;
        for(const auto& Candidate:Resolved)if(Candidate.Binding==R.Binding){if(B)return false;B=&Candidate;}
        if(!B || int(B->BufferResource!=nullptr)+int(B->TextureResource!=nullptr)+int(B->SamplerResource!=nullptr)!=1)return false;
        if(R.Kind<=3) {
            if(!B->BufferResource)return false;
            const auto& Buffer=*B->BufferResource;
            if(B->BufferOffset>=Buffer.Size)return false;
            uint64_t Bytes=B->BufferBytes?B->BufferBytes:Buffer.Size-B->BufferOffset;
            if(Bytes<R.MinBytes || (R.Stride&&Bytes%R.Stride) || !(Buffer.Usage&((R.Kind==0||R.Kind==3)?WGPUBufferUsage_Uniform:WGPUBufferUsage_Storage)))return false;
        }else if(R.Kind==4 || R.Kind==5) {
            if(!B->TextureResource)return false;
            const auto& View=*B->TextureResource;
            if(View.ArrayView!=(R.Dimension==2))return false;
            if(R.Kind==4) {
                if(!(View.Usage&WGPUTextureUsage_TextureBinding) || (R.SampleType==2)!=(View.Format==WGPUTextureFormat_R32Uint) || R.SampleType==3)return false;
            }else {
                const auto Format=R.Format==1?WGPUTextureFormat_R8Unorm:R.Format==2?WGPUTextureFormat_R32Uint:WGPUTextureFormat_RG11B10Ufloat;
                if(!(View.Usage&WGPUTextureUsage_StorageBinding) || View.Format!=Format)return false;
            }
        }else if(R.Kind==6) { if(!B->SamplerResource)return false; }
        else return false;
    }
    return true;
}
bool Device::CreateSampler(Sampler& Out, WGPUAddressMode Address, WGPUFilterMode Filter, WGPUMipmapFilterMode MipFilter) {
    auto Desc=WGPU_SAMPLER_DESCRIPTOR_INIT;
    Desc.addressModeU=Desc.addressModeV=Desc.addressModeW=Address;
    Desc.magFilter=Desc.minFilter=Filter; Desc.mipmapFilter=MipFilter;
    return CreateSampler(Out,Desc);
}
bool Device::CreateSampler(Sampler& Out,const WGPUSamplerDescriptor& Desc) {
    auto AddressValid=[](WGPUAddressMode A){return A==WGPUAddressMode_ClampToEdge || A==WGPUAddressMode_Repeat || A==WGPUAddressMode_MirrorRepeat;};
    auto FilterValid=[](WGPUFilterMode F){return F==WGPUFilterMode_Nearest || F==WGPUFilterMode_Linear;};
    if(Status!=State::Ready || Out.Handle || Desc.nextInChain || !AddressValid(Desc.addressModeU) || !AddressValid(Desc.addressModeV) ||
       !AddressValid(Desc.addressModeW) || !FilterValid(Desc.minFilter) || !FilterValid(Desc.magFilter) ||
       (Desc.mipmapFilter!=WGPUMipmapFilterMode_Nearest && Desc.mipmapFilter!=WGPUMipmapFilterMode_Linear) ||
       !std::isfinite(Desc.lodMinClamp) || !std::isfinite(Desc.lodMaxClamp) || Desc.lodMinClamp<0 || Desc.lodMaxClamp<Desc.lodMinClamp ||
       Desc.maxAnisotropy<1 || Desc.maxAnisotropy>16 ||
       (Desc.maxAnisotropy>1 && (Desc.minFilter!=WGPUFilterMode_Linear || Desc.magFilter!=WGPUFilterMode_Linear || Desc.mipmapFilter!=WGPUMipmapFilterMode_Linear)) ||
       (Desc.compare!=WGPUCompareFunction_Undefined && Desc.compare!=WGPUCompareFunction_Less))return false;
    Out.Handle=wgpuDeviceCreateSampler(Handle,&Desc);
    return Out.Handle!=nullptr;
}
bool Device::Dispatch(const ComputePipeline& Pipeline, const ResourceBinding* Bindings, size_t Count, uint32_t X, uint32_t Y, uint32_t Z) {
    return DispatchCommands(Pipeline,Bindings,Count,X,Y,Z,nullptr,0);
}
bool Device::DispatchIndirect(const ComputePipeline& Pipeline,const ResourceBinding* Bindings,size_t Count,const Buffer& Arguments,uint64_t Offset) {
    return DispatchCommands(Pipeline,Bindings,Count,0,0,0,&Arguments,Offset);
}
bool Device::DispatchCommands(const ComputePipeline& Pipeline,const ResourceBinding* Bindings,size_t Count,uint32_t X,uint32_t Y,uint32_t Z,
                              const Buffer* Arguments,uint64_t Offset) {
    if (Status != State::Ready || !Pipeline.Handle || !Bindings || !Count || Count>Limits.maxBindingsPerBindGroup)return false;
    if(Arguments) {
        if(!Arguments->Handle || !(Arguments->Usage&WGPUBufferUsage_Indirect) || Offset%4 || Offset>Arguments->Size || Arguments->Size-Offset<12)return false;
    }else if(X>Limits.maxComputeWorkgroupsPerDimension || Y>Limits.maxComputeWorkgroupsPerDimension || Z>Limits.maxComputeWorkgroupsPerDimension)return false;
    std::vector<WGPUBindGroupEntry> Entries;
    if(!MakeBindingEntries(Bindings,Count,Entries))return false;
    auto Layout = wgpuComputePipelineGetBindGroupLayout(Pipeline.Handle, 0);
    auto Desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT; Desc.layout = Layout; Desc.entryCount = Count; Desc.entries = Entries.data();
    auto Group = wgpuDeviceCreateBindGroup(Handle, &Desc); wgpuBindGroupLayoutRelease(Layout);
    if (!Group) return false;
    auto Encoder = CreateEncoder();
    auto Pass = wgpuCommandEncoderBeginComputePass(Encoder, nullptr);
    wgpuComputePassEncoderSetPipeline(Pass, Pipeline.Handle);
    wgpuComputePassEncoderSetBindGroup(Pass, 0, Group, 0, nullptr);
    if(Arguments)wgpuComputePassEncoderDispatchWorkgroupsIndirect(Pass,Arguments->Handle,Offset);
    else wgpuComputePassEncoderDispatchWorkgroups(Pass, X, Y, Z);
    wgpuComputePassEncoderEnd(Pass); wgpuComputePassEncoderRelease(Pass); wgpuBindGroupRelease(Group);
    return Submit(Encoder);
}
bool Device::MakeBindingEntries(const ResourceBinding* Bindings,size_t Count,std::vector<WGPUBindGroupEntry>& Entries) {
    if(Count>Limits.maxBindingsPerBindGroup || (Count&&!Bindings))return false;
    Entries.resize(Count);
    for (size_t i=0;i<Count;++i) {
        const auto& B=Bindings[i];
        if (B.Binding>=Limits.maxBindingsPerBindGroup || int(B.BufferResource!=nullptr)+int(B.TextureResource!=nullptr)+int(B.SamplerResource!=nullptr)!=1) return false;
        for(size_t j=0;j<i;++j)if(B.Binding==Bindings[j].Binding)return false;
        Entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        auto& E=Entries[i]; E.binding=B.Binding;
        if(B.BufferResource) {
            const auto& R=*B.BufferResource;
            if(!R.Handle || !(R.Usage&(WGPUBufferUsage_Uniform|WGPUBufferUsage_Storage)) || B.BufferOffset>=R.Size) return false;
            const uint64_t Bytes=B.BufferBytes?B.BufferBytes:R.Size-B.BufferOffset;
            if(!Bytes || Bytes>R.Size-B.BufferOffset || Bytes%4) return false;
            if((R.Usage&WGPUBufferUsage_Uniform) && (B.BufferOffset%Limits.minUniformBufferOffsetAlignment || Bytes>Limits.maxUniformBufferBindingSize)) return false;
            if((R.Usage&WGPUBufferUsage_Storage) && (B.BufferOffset%Limits.minStorageBufferOffsetAlignment || Bytes>Limits.maxStorageBufferBindingSize)) return false;
            E.buffer=R.Handle;E.offset=B.BufferOffset;E.size=Bytes;
        } else {
            if(B.BufferOffset || B.BufferBytes) return false;
            if(B.TextureResource) { if(!B.TextureResource->Handle) return false; E.textureView=B.TextureResource->Handle; }
            else { if(!B.SamplerResource->Handle) return false; E.sampler=B.SamplerResource->Handle; }
        }
    }
    return true;
}
}
