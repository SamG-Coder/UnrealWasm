#pragma once
#include <webgpu/webgpu.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "WebCudaShaderCode.h"

// Shared by the Unreal module and its Wasm runtime tests. All GPU calls live in
// C++; Emdawnwebgpu supplies the browser ABI. No lighting/shader code is here.
namespace UEWebGPU {
struct ShaderCode;
struct Buffer {
    WGPUBuffer Handle = nullptr;
    uint64_t Size = 0;
    WGPUBufferUsage Usage = WGPUBufferUsage_None;
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() { if (Handle) wgpuBufferRelease(Handle); }
};
struct Texture {
    WGPUTexture Handle = nullptr;
    uint32_t Width = 0, Height = 0, Layers = 0, Mips = 0, BytesPerTexel = 0;
    WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
    WGPUTextureUsage Usage = WGPUTextureUsage_None;
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture() { if (Handle) wgpuTextureRelease(Handle); }
};
struct TextureView {
    WGPUTextureView Handle = nullptr;
    WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
    WGPUTextureUsage Usage = WGPUTextureUsage_None;
    bool ArrayView = false;
    WGPUTextureAspect Aspect = WGPUTextureAspect_All;
    TextureView() = default;
    TextureView(const TextureView&) = delete;
    TextureView& operator=(const TextureView&) = delete;
    ~TextureView() { if (Handle) wgpuTextureViewRelease(Handle); }
};
struct Readback {
    // The caller must keep this object alive until Complete is true.
    bool Complete = false, Succeeded = false;
    std::vector<uint8_t> Bytes;
    std::string Error;
};
struct ComputePipeline {
    WGPUComputePipeline Handle = nullptr;
    ComputePipeline() = default;
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;
    ~ComputePipeline() { if (Handle) wgpuComputePipelineRelease(Handle); }
};
struct RasterShader {
    WGPUShaderModule Handle = nullptr;
    ShaderCode Code;
    RasterShader() = default;
    RasterShader(const RasterShader&) = delete;
    RasterShader& operator=(const RasterShader&) = delete;
    ~RasterShader() { if(Handle) wgpuShaderModuleRelease(Handle); }
};
struct RenderPipeline {
    WGPURenderPipeline Handle = nullptr;
    ShaderCode Vertex, Pixel;
    RenderPipeline() = default;
    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;
    ~RenderPipeline() { if(Handle) wgpuRenderPipelineRelease(Handle); }
};
struct BufferBinding { uint32_t Binding; const Buffer* Resource; };
struct Sampler {
    WGPUSampler Handle = nullptr;
    Sampler() = default;
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    ~Sampler() { if (Handle) wgpuSamplerRelease(Handle); }
};
// Exactly one resource is required per binding. Zero BufferBytes means the
// remainder of the buffer; offsets and ranges are checked before submission.
struct ResourceBinding {
    uint32_t Binding;
    const Buffer* BufferResource = nullptr;
    const TextureView* TextureResource = nullptr;
    const Sampler* SamplerResource = nullptr;
    uint64_t BufferOffset = 0, BufferBytes = 0;
};
struct UniformResourceTable {
    uint32_t Slot, LayoutHash;
    const ResourceBinding* Resources;
    size_t Count;
};
struct StageBindings {
    const ResourceBinding* Resources = nullptr;
    size_t Count = 0;
    const UniformResourceTable* Tables = nullptr;
    size_t TableCount = 0;
};
struct VertexBufferBinding { const Buffer* Resource; uint64_t Offset = 0, Bytes = 0; };
struct DrawArguments {
    uint32_t Count = 0, Instances = 1, First = 0, FirstInstance = 0;
    const Buffer* Indices = nullptr;
    WGPUIndexFormat IndexFormat = WGPUIndexFormat_Undefined;
    uint64_t IndexOffset = 0, IndexBytes = 0;
    int32_t BaseVertex = 0;
};
struct DrawState {
    bool HasViewport=false,HasScissor=false;
    float X=0,Y=0,Width=0,Height=0,MinDepth=0,MaxDepth=1;
    uint32_t ScissorX=0,ScissorY=0,ScissorWidth=0,ScissorHeight=0,StencilReference=0;
    WGPUColor BlendConstant={0,0,0,0};
};
struct RenderPass {
    WGPUCommandEncoder Encoder=nullptr;
    WGPURenderPassEncoder Handle=nullptr;
    uint32_t Width=0,Height=0;
    size_t DebugDepth=0;
    RenderPass()=default;
    RenderPass(const RenderPass&)=delete;
    RenderPass& operator=(const RenderPass&)=delete;
    ~RenderPass(){if(Handle){wgpuRenderPassEncoderEnd(Handle);wgpuRenderPassEncoderRelease(Handle);}if(Encoder)wgpuCommandEncoderRelease(Encoder);}
};
class Device {
public:
    enum class State { Idle, Pending, Ready, Failed };
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    // Device and pending readbacks must outlive their asynchronous callbacks.
    ~Device();
    void Initialize();
    State GetState() const { return Status; }
    const std::string& GetError() const { return Error; }
    bool CreateBuffer(Buffer& Out, uint64_t Size, WGPUBufferUsage Usage);
    bool WriteBuffer(const Buffer& Destination, uint64_t Offset, const void* Data, size_t Bytes);
    bool CopyBuffer(const Buffer& Source, const Buffer& Destination, uint64_t Bytes);
    bool CopyBufferRegion(const Buffer& Source,uint64_t SourceOffset,const Buffer& Destination,uint64_t DestinationOffset,uint64_t Bytes);
    bool ReadBuffer(const Buffer& Source, size_t Bytes, Readback& Out);
    bool HasTextureFormatsTier1() const { return TextureFormatsTier1; }
    bool HasRG11B10Renderable() const { return RG11B10Renderable; }
    bool CreateTexture2D(Texture& Out, uint32_t Width, uint32_t Height, uint32_t Layers, uint32_t Mips, WGPUTextureFormat Format, WGPUTextureUsage Usage);
    bool CreateTextureView(TextureView& Out, const Texture& Source, uint32_t FirstMip, uint32_t MipCount, uint32_t FirstLayer, uint32_t LayerCount, bool ArrayView,WGPUTextureAspect Aspect=WGPUTextureAspect_All);
    bool CreateRGBA8Texture(Texture& Out, uint32_t Width, uint32_t Height);
    bool ClearTexture(const Texture& Target, WGPUColor Color, uint32_t Mip = 0, uint32_t Layer = 0);
    bool ClearTextureLayers(const Texture& Target,WGPUColor Color,uint32_t Mip,uint32_t FirstLayer,uint32_t LayerCount);
    bool CopyTextureToBuffer(const Texture& Source, const Buffer& Destination, uint32_t BytesPerRow, uint32_t Mip = 0, uint32_t FirstLayer = 0, uint32_t LayerCount = 1);
    bool CopyBufferToTexture(const Buffer& Source,const Texture& Destination,uint32_t BytesPerRow,uint32_t Mip=0,uint32_t FirstLayer=0,uint32_t LayerCount=1);
    bool CreateComputePipeline(ComputePipeline& Out, const char* WGSL, const char* Entry);
    bool CreateRasterShader(RasterShader& Out,const ShaderCode& Code);
    // Native WebGPU state descriptors carry the translated Unreal PSO/pass state.
    // Modules and entry points always come from the exported original shaders.
    bool CreateRenderPipeline(RenderPipeline& Out,const RasterShader& Vertex,const RasterShader& Pixel,const WGPURenderPipelineDescriptor& State);
    bool Draw(const RenderPipeline& Pipeline,const WGPURenderPassDescriptor& Pass,const StageBindings& Vertex,const StageBindings& Pixel,
              const VertexBufferBinding* Buffers,size_t BufferCount,const DrawArguments& Arguments,const DrawState& State={});
    bool BeginRenderPass(RenderPass& Out,const WGPURenderPassDescriptor& Descriptor,uint32_t Width=0,uint32_t Height=0);
    bool DrawInRenderPass(RenderPass& Pass,const RenderPipeline& Pipeline,const StageBindings& Vertex,const StageBindings& Pixel,
              const VertexBufferBinding* Buffers,size_t BufferCount,const DrawArguments& Arguments,const DrawState& State={});
    bool EndRenderPass(RenderPass& Pass);
    bool CreateSampler(Sampler& Out, WGPUAddressMode Address, WGPUFilterMode Filter, WGPUMipmapFilterMode MipFilter);
    bool CreateSampler(Sampler& Out, const WGPUSamplerDescriptor& Descriptor);
    uint32_t GetStorageBufferOffsetAlignment() const { return Limits.minStorageBufferOffsetAlignment; }
    bool PushDebugGroup(const char* Label);
    bool PopDebugGroup();
    bool Dispatch(const ComputePipeline& Pipeline, const BufferBinding* Bindings, size_t Count, uint32_t X, uint32_t Y, uint32_t Z);
    bool Dispatch(const ComputePipeline& Pipeline, const ResourceBinding* Bindings, size_t Count, uint32_t X, uint32_t Y, uint32_t Z);
    bool DispatchIndirect(const ComputePipeline& Pipeline,const ResourceBinding* Bindings,size_t Count,const Buffer& Arguments,uint64_t Offset);
    bool DispatchShader(const ComputePipeline& Pipeline, const ShaderCode& Code, const ResourceBinding* Bindings, size_t Count,
                        const UniformResourceTable* Tables, size_t TableCount, uint32_t X, uint32_t Y, uint32_t Z);
    bool DispatchShaderIndirect(const ComputePipeline& Pipeline,const ShaderCode& Code,const ResourceBinding* Bindings,size_t Count,
                        const UniformResourceTable* Tables,size_t TableCount,const Buffer& Arguments,uint64_t Offset);
private:
    bool ResolveShaderBindings(const ShaderCode& Code,const StageBindings& Input,std::vector<ResourceBinding>& Resolved);
    bool MakeBindingEntries(const ResourceBinding* Bindings,size_t Count,std::vector<WGPUBindGroupEntry>& Entries);
    bool DispatchCommands(const ComputePipeline& Pipeline,const ResourceBinding* Bindings,size_t Count,uint32_t X,uint32_t Y,uint32_t Z,
                          const Buffer* Arguments,uint64_t Offset);
    bool DispatchShaderCommands(const ComputePipeline& Pipeline,const ShaderCode& Code,const ResourceBinding* Bindings,size_t Count,
                        const UniformResourceTable* Tables,size_t TableCount,uint32_t X,uint32_t Y,uint32_t Z,const Buffer* Arguments,uint64_t Offset);
    static void AdapterReady(WGPURequestAdapterStatus, WGPUAdapter, WGPUStringView, void*, void*);
    static void DeviceReady(WGPURequestDeviceStatus, WGPUDevice, WGPUStringView, void*, void*);
    void Fail(WGPUStringView Message);
    bool Submit(WGPUCommandEncoder Encoder);
    WGPUCommandEncoder CreateEncoder();
    State Status = State::Idle;
    WGPUInstance Instance = nullptr;
    WGPUAdapter Adapter = nullptr;
    WGPUDevice Handle = nullptr;
    WGPUQueue Queue = nullptr;
    WGPULimits Limits = WGPU_LIMITS_INIT;
    bool TextureFormatsTier1 = false, RG11B10Renderable = false;
    std::string Error;
    std::vector<std::string> DebugGroups;
};
}
