#pragma once
#include "RHIResources.h"
#include "RHI.h"
#include "WebGPUDevice.h"
#include "WebCudaShaderCode.h"
#include "RHIContext.h"
#include <memory>
#include <array>

struct FRHIShaderParameter;
struct FRHIShaderParameterResource;

// Initial concrete Unreal resource types. Unsupported descriptions are rejected
// by the factories instead of silently allocating a different resource.
class FWebGPUBuffer final : public FRHIBuffer
{
public:
    explicit FWebGPUBuffer(const FRHIBufferCreateDesc& Desc) : FRHIBuffer(Desc) {}
    UEWebGPU::Buffer Resource;
};
class FWebGPUTexture final : public FRHITexture
{
public:
    explicit FWebGPUTexture(const FRHITextureCreateDesc& Desc) : FRHITexture(Desc) {}
    void* GetNativeResource() const override { return Resource.Handle; }
    UEWebGPU::Texture Resource;
    UEWebGPU::TextureView DefaultView;
};
class FWebGPUBufferSRV final : public FRHIShaderResourceView
{
public:
    FWebGPUBufferSRV(FWebGPUBuffer* Buffer,const FRHIViewDesc& Desc) : FRHIShaderResourceView(Buffer,Desc) {}
    UEWebGPU::ResourceBinding Binding{};
};
class FWebGPUBufferUAV final : public FRHIUnorderedAccessView
{
public:
    FWebGPUBufferUAV(FWebGPUBuffer* Buffer,const FRHIViewDesc& Desc) : FRHIUnorderedAccessView(Buffer,Desc) {}
    UEWebGPU::ResourceBinding Binding{};
};
class FWebGPUSamplerState final : public FRHISamplerState
{
public:
    UEWebGPU::Sampler Resource;
};
class FWebGPUUniformBuffer final : public FRHIUniformBuffer
{
public:
    explicit FWebGPUUniformBuffer(const FRHIUniformBufferLayout* Layout) : FRHIUniformBuffer(Layout) {}
    bool Update(UEWebGPU::Device& Device,const void* Contents);
    const UEWebGPU::Buffer* GetConstants() const { return Constants.get(); }
    UEWebGPU::UniformResourceTable GetTable(uint32 Slot) const { return {Slot,GetLayout().GetHash(),Bindings.data(),Bindings.size()}; }
private:
    std::unique_ptr<UEWebGPU::Buffer> Constants;
    std::vector<UEWebGPU::ResourceBinding> Bindings;
};
class FWebGPUComputeShader final : public FRHIComputeShader
{
public:
    bool Initialize(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error);
    UEWebGPU::ShaderCode Code;
    UEWebGPU::ComputePipeline Pipeline;
};
class FWebGPUVertexShader final : public FRHIVertexShader
{
public:
    bool Initialize(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error);
    UEWebGPU::RasterShader Resource;
};
class FWebGPUPixelShader final : public FRHIPixelShader
{
public:
    bool Initialize(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error);
    UEWebGPU::RasterShader Resource;
};
class FWebGPUVertexDeclaration final : public FRHIVertexDeclaration
{
public:
    bool Initialize(const FVertexDeclarationElementList& InElements);
    bool GetInitializer(FVertexDeclarationElementList& Out) override {Out=Elements;return true;}
    std::vector<WGPUVertexBufferLayout> Layouts;
    std::vector<uint32> Streams;
private:
    FVertexDeclarationElementList Elements;
    std::vector<std::vector<WGPUVertexAttribute>> Attributes;
};
class FWebGPURasterizerState final : public FRHIRasterizerState
{
public:
    explicit FWebGPURasterizerState(const FRasterizerStateInitializerRHI& In):Initializer(In) {}
    bool GetInitializer(FRasterizerStateInitializerRHI& Out) override {Out=Initializer;return true;}
    const FRasterizerStateInitializerRHI Initializer;
};
class FWebGPUDepthStencilState final : public FRHIDepthStencilState
{
public:
    explicit FWebGPUDepthStencilState(const FDepthStencilStateInitializerRHI& In):Initializer(In) {}
    bool GetInitializer(FDepthStencilStateInitializerRHI& Out) override {Out=Initializer;return true;}
    const FDepthStencilStateInitializerRHI Initializer;
};
class FWebGPUBlendState final : public FRHIBlendState
{
public:
    explicit FWebGPUBlendState(const FBlendStateInitializerRHI& In):Initializer(In) {}
    bool GetInitializer(FBlendStateInitializerRHI& Out) override {Out=Initializer;return true;}
    const FBlendStateInitializerRHI Initializer;
};
class FWebGPUGraphicsPipelineState final : public FRHIGraphicsPipelineState
{
public:
    bool Initialize(UEWebGPU::Device& Device,const FGraphicsPipelineStateInitializer& In,FString& Error);
    FRHIGraphicsShader* GetShader(EShaderFrequency Frequency) const override {
        return Frequency==SF_Vertex?static_cast<FRHIGraphicsShader*>(Vertex.GetReference()):Frequency==SF_Pixel?Pixel.GetReference():nullptr;
    }
    TRefCountPtr<FWebGPUVertexShader> Vertex;
    TRefCountPtr<FWebGPUPixelShader> Pixel;
    TRefCountPtr<FWebGPUVertexDeclaration> Declaration;
    UEWebGPU::RenderPipeline Resource;
    EPrimitiveType Primitive=PT_TriangleList;
};
TRefCountPtr<FWebGPUVertexDeclaration> CreateWebGPUVertexDeclaration(const FVertexDeclarationElementList& Elements);
TRefCountPtr<FWebGPURasterizerState> CreateWebGPURasterizerState(const FRasterizerStateInitializerRHI& Initializer);
TRefCountPtr<FWebGPUDepthStencilState> CreateWebGPUDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer);
TRefCountPtr<FWebGPUBlendState> CreateWebGPUBlendState(const FBlendStateInitializerRHI& Initializer);
TRefCountPtr<FWebGPUGraphicsPipelineState> CreateWebGPUGraphicsPipelineState(UEWebGPU::Device& Device,const FGraphicsPipelineStateInitializer& Initializer,FString& Error);
class FWebGPUTextureSRV final : public FRHIShaderResourceView
{
public:
    FWebGPUTextureSRV(FWebGPUTexture* Texture, const FRHIViewDesc& Desc) : FRHIShaderResourceView(Texture,Desc) {}
    UEWebGPU::TextureView View;
};
class FWebGPUTextureUAV final : public FRHIUnorderedAccessView
{
public:
    FWebGPUTextureUAV(FWebGPUTexture* Texture, const FRHIViewDesc& Desc) : FRHIUnorderedAccessView(Texture,Desc) {}
    UEWebGPU::TextureView View;
};
TRefCountPtr<FWebGPUBuffer> CreateWebGPUBuffer(UEWebGPU::Device& Device, const FRHIBufferCreateDesc& Desc);
TRefCountPtr<FWebGPUTexture> CreateWebGPUTexture(UEWebGPU::Device& Device, const FRHITextureCreateDesc& Desc);
TRefCountPtr<FWebGPUTextureSRV> CreateWebGPUTextureSRV(UEWebGPU::Device& Device, FWebGPUTexture* Texture, const FRHIViewDesc& Desc);
TRefCountPtr<FWebGPUTextureUAV> CreateWebGPUTextureUAV(UEWebGPU::Device& Device, FWebGPUTexture* Texture, const FRHIViewDesc& Desc);
TRefCountPtr<FWebGPUBufferSRV> CreateWebGPUBufferSRV(UEWebGPU::Device& Device,FWebGPUBuffer* Buffer,const FRHIViewDesc& Desc);
TRefCountPtr<FWebGPUBufferUAV> CreateWebGPUBufferUAV(UEWebGPU::Device& Device,FWebGPUBuffer* Buffer,const FRHIViewDesc& Desc);
TRefCountPtr<FWebGPUSamplerState> CreateWebGPUSampler(UEWebGPU::Device& Device,const FSamplerStateInitializerRHI& Desc);
TRefCountPtr<FWebGPUUniformBuffer> CreateWebGPUUniformBuffer(UEWebGPU::Device& Device,const FRHIUniformBufferLayout* Layout,const void* Contents);
TRefCountPtr<FWebGPUComputeShader> CreateWebGPUComputeShader(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error);
TRefCountPtr<FWebGPUVertexShader> CreateWebGPUVertexShader(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error);
TRefCountPtr<FWebGPUPixelShader> CreateWebGPUPixelShader(UEWebGPU::Device& Device,const FRHICreateShaderDesc& Desc,FString& Error);
bool GetWebGPUResourceBinding(FRHIResource* Resource,UEWebGPU::ResourceBinding& Out);

struct FWebGPUPreparedParameters
{
    UEWebGPU::Buffer Root;
    std::vector<UEWebGPU::ResourceBinding> Resources;
    std::vector<UEWebGPU::UniformResourceTable> Tables;
    UEWebGPU::StageBindings GetBindings() const {return {Resources.data(),Resources.size(),Tables.data(),Tables.size()};}
};
// Shared parameter ABI for compute, vertex and pixel stages. Own the shader
// while retaining a pointer to its immutable decoded container.
class FWebGPUShaderParameterState
{
public:
    void SetShader(FRHIShader* InShader,const UEWebGPU::ShaderCode* InCode);
    bool SetParameters(TConstArrayView<uint8> Data,TConstArrayView<FRHIShaderParameter> Parameters,
        TConstArrayView<FRHIShaderParameterResource> ResourceParameters,TConstArrayView<FRHIShaderParameterResource> BindlessParameters);
    bool SetUniformBuffer(uint32 Slot,FWebGPUUniformBuffer* Buffer);
    bool Prepare(UEWebGPU::Device& Device,FWebGPUPreparedParameters& Out) const;
private:
    TRefCountPtr<FRHIShader> Shader;
    const UEWebGPU::ShaderCode* Code=nullptr;
    std::array<TRefCountPtr<FRHIResource>,64> Resources{};
    std::array<TRefCountPtr<FWebGPUUniformBuffer>,32> Uniforms{};
    std::vector<uint8_t> Constants,Written;
};
class FWebGPUComputeState : public FWebGPUShaderParameterState
{
public:
    void SetShader(FWebGPUComputeShader* InShader) {Shader=InShader;FWebGPUShaderParameterState::SetShader(InShader,InShader?&InShader->Code:nullptr);}
    bool Dispatch(UEWebGPU::Device& Device,uint32 X,uint32 Y,uint32 Z);
    bool DispatchIndirect(UEWebGPU::Device& Device,FWebGPUBuffer* Arguments,uint32 Offset);
private:
    bool DispatchCommands(UEWebGPU::Device& Device,uint32 X,uint32 Y,uint32 Z,FWebGPUBuffer* Arguments,uint32 Offset);
    TRefCountPtr<FWebGPUComputeShader> Shader;
};

// Original Unreal compute interface for the supported resources and one queue.
// Unsupported commands must not silently succeed in a registered backend.
class FWebGPUComputeContext : public IRHIComputeContext
{
public:
    explicit FWebGPUComputeContext(UEWebGPU::Device& InDevice):Device(InDevice) {}
    ERHIPipeline GetPipeline() const override { return ERHIPipeline::Graphics; }
    void RHISetComputePipelineState(FRHIComputePipelineState* Pipeline) override;
    void RHIDispatchComputeShader(uint32 X,uint32 Y,uint32 Z) override;
    void RHIDispatchIndirectComputeShader(FRHIBuffer* Arguments,uint32 Offset) override;
    void RHISetShaderParameters(FRHIComputeShader* Shader,TConstArrayView<uint8> Data,TConstArrayView<FRHIShaderParameter> Parameters,
        TConstArrayView<FRHIShaderParameterResource> Resources,TConstArrayView<FRHIShaderParameterResource> Bindless) override;
    void RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& Bindings) override;
    void RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot,FRHIUniformBuffer* Buffer) override;
    void RHISetUniformBufferDynamicOffset(FUniformBufferStaticSlot Slot,uint32 Offset) override;
    void RHIClearUAVUint(FRHIUnorderedAccessView* UAV,const FUintVector4& Values) override;
    void RHIClearUAVFloat(FRHIUnorderedAccessView* UAV,const FVector4f& Values) override;
    void RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions) override;
    void RHIEndTransitions(TArrayView<const FRHITransition*> Transitions) override;
#if WITH_RHI_BREADCRUMBS
    void RHIBeginBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb) override;
    void RHIEndBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb) override;
#endif
private:
    void ClearBufferUAV(FRHIUnorderedAccessView* UAV,uint32 Value);
    void ApplyStaticUniformBuffers();
    UEWebGPU::Device& Device;
    FWebGPUComputeState State;
    TRefCountPtr<FRHIComputePipelineState> Pipeline;
    std::array<TRefCountPtr<FWebGPUUniformBuffer>,MAX_UNIFORM_BUFFER_STATIC_SLOTS> StaticUniforms{};
#if WITH_RHI_BREADCRUMBS
    TArray<FRHIBreadcrumbNode*> Breadcrumbs;
#endif
};

// Graphics command context with retained stage bindings and one active pass.
class FWebGPUGraphicsContext final : public IRHICommandContext
{
public:
    explicit FWebGPUGraphicsContext(UEWebGPU::Device& InDevice):Device(InDevice),Compute(InDevice) {}
    ERHIPipeline GetPipeline() const override {return ERHIPipeline::Graphics;}
    void RHISetComputePipelineState(FRHIComputePipelineState* In) override {Compute.RHISetComputePipelineState(In);}
    void RHIDispatchComputeShader(uint32 X,uint32 Y,uint32 Z) override {OutsideRenderPass();Compute.RHIDispatchComputeShader(X,Y,Z);}
    void RHIDispatchIndirectComputeShader(FRHIBuffer* Buffer,uint32 Offset) override {OutsideRenderPass();Compute.RHIDispatchIndirectComputeShader(Buffer,Offset);}
    void RHISetShaderParameters(FRHIComputeShader* Shader,TConstArrayView<uint8> Data,TConstArrayView<FRHIShaderParameter> Parameters,
        TConstArrayView<FRHIShaderParameterResource> Resources,TConstArrayView<FRHIShaderParameterResource> Bindless) override {Compute.RHISetShaderParameters(Shader,Data,Parameters,Resources,Bindless);}
    void RHIClearUAVUint(FRHIUnorderedAccessView* View,const FUintVector4& Values) override {OutsideRenderPass();Compute.RHIClearUAVUint(View,Values);}
    void RHIClearUAVFloat(FRHIUnorderedAccessView* View,const FVector4f& Values) override {OutsideRenderPass();Compute.RHIClearUAVFloat(View,Values);}
    void RHIBeginTransitions(TArrayView<const FRHITransition*> In) override {OutsideRenderPass();Compute.RHIBeginTransitions(In);}
    void RHIEndTransitions(TArrayView<const FRHITransition*> In) override {OutsideRenderPass();Compute.RHIEndTransitions(In);}
    void RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& Bindings) override;
    void RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot,FRHIUniformBuffer* Buffer) override;
    void RHISetUniformBufferDynamicOffset(FUniformBufferStaticSlot Slot,uint32 Offset) override {Compute.RHISetUniformBufferDynamicOffset(Slot,Offset);}
#if WITH_RHI_BREADCRUMBS
    void RHIBeginBreadcrumbGPU(FRHIBreadcrumbNode* In) override {Compute.RHIBeginBreadcrumbGPU(In);}
    void RHIEndBreadcrumbGPU(FRHIBreadcrumbNode* In) override {Compute.RHIEndBreadcrumbGPU(In);}
#endif
    void RHISetStreamSource(uint32 Slot,FRHIBuffer* Buffer,uint32 Offset) override;
    void RHISetViewport(float MinX,float MinY,float MinZ,float MaxX,float MaxY,float MaxZ) override;
    void RHISetMultipleViewports(uint32 Count,const FViewportBounds* Bounds) override;
    void RHISetScissorRect(bool Enable,uint32 MinX,uint32 MinY,uint32 MaxX,uint32 MaxY) override;
    void RHISetStencilRef(uint32 Ref) override {DynamicState.StencilReference=Ref;}
    void RHISetBlendFactor(const FLinearColor& Value) override {DynamicState.BlendConstant={Value.R,Value.G,Value.B,Value.A};}
    void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* In,uint32 StencilRef,bool ApplyAdditional) override;
#if PLATFORM_USE_FALLBACK_PSO
    void RHISetGraphicsPipelineState(const FGraphicsPipelineStateInitializer& In,uint32 StencilRef,bool ApplyAdditional) override;
#endif
    void RHISetShaderParameters(FRHIGraphicsShader* Shader,TConstArrayView<uint8> Data,TConstArrayView<FRHIShaderParameter> Parameters,
        TConstArrayView<FRHIShaderParameterResource> Resources,TConstArrayView<FRHIShaderParameterResource> Bindless) override;
    void RHIDrawPrimitive(uint32 First,uint32 Primitives,uint32 Instances) override;
    void RHIDrawIndexedPrimitive(FRHIBuffer* Indices,int32 BaseVertex,uint32 FirstInstance,uint32 NumVertices,uint32 StartIndex,uint32 Primitives,uint32 Instances) override;
    void RHIDrawPrimitiveIndirect(FRHIBuffer*,uint32) override;
    void RHIDrawIndexedPrimitiveIndirect(FRHIBuffer*,FRHIBuffer*,uint32) override;
    void RHIBeginRenderPass(const FRHIRenderPassInfo& Info,const TCHAR* Name) override;
    void RHIEndRenderPass() override;
    void RHIBeginRenderQuery(FRHIRenderQuery*) override;
    void RHIEndRenderQuery(FRHIRenderQuery*) override;
    void RHISetDepthBounds(float,float) override;
    void RHICopyTexture(FRHITexture*,FRHITexture*,const FRHICopyTextureInfo&) override;
    void RHICopyBufferRegion(FRHIBuffer* Destination,uint64 DestinationOffset,FRHIBuffer* Source,uint64 SourceOffset,uint64 Bytes) override;
private:
    void OutsideRenderPass() const;
    void ApplyStaticUniformBuffers();
    uint32 VertexCount(uint32 Primitives) const;
    void Draw(const UEWebGPU::DrawArguments& Arguments);
    UEWebGPU::Device& Device;
    FWebGPUComputeContext Compute;
    TRefCountPtr<FWebGPUGraphicsPipelineState> Pipeline;
    FWebGPUShaderParameterState VertexParameters,PixelParameters;
    std::array<TRefCountPtr<FWebGPUUniformBuffer>,MAX_UNIFORM_BUFFER_STATIC_SLOTS> StaticUniforms{};
    std::array<TRefCountPtr<FWebGPUBuffer>,16> Streams{};
    std::array<uint32,16> StreamOffsets{};
    UEWebGPU::DrawState DynamicState;
    std::unique_ptr<UEWebGPU::RenderPass> Pass;
    TArray<TRefCountPtr<FWebGPUTexture>> Attachments;
    std::vector<std::unique_ptr<UEWebGPU::TextureView>> AttachmentViews;
};

// Storage contract to install when the dynamic RHI is initialized. Only a
// single WebGPU queue is implemented; async-queue transfers/aliasing reject.
struct FWebGPUTransitionData
{
    TArray<TRefCountPtr<FRHIResource>> Resources;
    mutable uint8 Phase=0;
};
bool CreateWebGPUTransition(FRHITransition* Transition,const FRHITransitionCreateInfo& Info);
void ReleaseWebGPUTransition(FRHITransition* Transition);
