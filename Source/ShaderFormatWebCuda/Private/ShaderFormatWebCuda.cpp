#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormatModule.h"
#include "ShaderCompilerCommon.h"
#include "ShaderPreprocessTypes.h"
#include "ShaderParameterParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonSerializer.h"
#include "WebCudaShaderCode.h"

class FShaderFormatWebCuda final : public UE::ShaderCompilerCommon::FBaseShaderFormat
{
public:
    uint32 GetVersion(FName) const override {
        static const uint32 Version=[] {
            uint32 Hash=4; TArray<uint8> Data;
            const FString Paths[]={FPaths::ProjectDir()/TEXT("engine/ShaderCompiler/compile_webcuda.py"),
                FPaths::ProjectDir()/TEXT("engine/ShaderCompiler/compile_texture_spirv.py"),
                FPaths::ProjectDir()/TEXT("engine/ShaderCompiler/compile_raster.py"),
                FPaths::ProjectDir()/TEXT("engine/ShaderCompiler/texture_container.py"),
                FPaths::ProjectDir()/TEXT("engine/ShaderCompiler/wgsl_layout.py"),
                FPaths::ProjectDir()/TEXT("engine/ShaderCompiler/spirv_access.py"),
                FPaths::ProjectDir()/TEXT("Config/WebCudaStorageFormats.json"),
                FPaths::ProjectDir()/TEXT("Source/WebGPURHI/Public/WebCudaShaderCode.h"),
                FPaths::ProjectDir()/TEXT("engine/artifacts/tint-cli/Release/tint.exe"),
                TEXT("C:/VulkanSDK/1.4.357.0/Bin/spirv-opt.exe"),TEXT("C:/VulkanSDK/1.4.357.0/Bin/spirv-val.exe"),
                TEXT("C:/VulkanSDK/1.4.357.0/Bin/slangc.exe"),TEXT("C:/VulkanSDK/1.4.357.0/Bin/slang-compiler.dll")};
            for(const FString& Path:Paths) {
                if(FFileHelper::LoadFileToArray(Data,*Path)) Hash=FCrc::MemCrc32(Data.GetData(),Data.Num(),Hash);
                else Hash=HashCombine(Hash,GetTypeHash(Path));
            }
            return Hash;
        }();
        return Version;
    }
    void GetSupportedFormats(TArray<FName>& Formats) const override { Formats.Add(TEXT("SF_WEBCUDA")); }
    // Use UE's existing HLSL platform includes. The backend validates the
    // resulting resources and intrinsics; this does not assert D3D support.
    const TCHAR* GetPlatformIncludeDirectory() const override { return TEXT("D3D"); }
    void ModifyShaderCompilerInput(FShaderCompilerInput& Input) const override {
        Input.Environment.CompilerFlags.Add(CFLAG_RemoveDeadCode);
        Input.Environment.SetDefine(TEXT("COMPILER_HLSL"),1);
        Input.Environment.SetDefine(TEXT("COMPILER_DXC"),1);
        Input.Environment.SetDefine(TEXT("COMPILER_FXC"),0);
        Input.Environment.SetDefine(TEXT("SM6_PROFILE"),1);
        Input.Environment.SetDefine(TEXT("SM5_PROFILE"),0);
        Input.Environment.SetDefine(TEXT("ES3_1_PROFILE"),0);
        Input.Environment.SetDefine(TEXT("__SHADER_TARGET_MAJOR"),6);
        Input.Environment.SetDefine(TEXT("__SHADER_TARGET_MINOR"),6);
    }
    void CompilePreprocessedShader(const FShaderCompilerInput& Input, const FShaderPreprocessOutput& Preprocessed, FShaderCompilerOutput& Output) const override {
        Output.Target=Input.Target;
        Output.bSucceeded=false;
        auto Fail=[&](const FString& Message) { Output.Errors.Emplace(*Input.VirtualSourceFilePath,TEXT("0"),*Message); };
        const auto Frequency=Input.Target.GetFrequency();
        if(Frequency!=SF_Compute && Frequency!=SF_Vertex && Frequency!=SF_Pixel) {Fail(TEXT("WebCuda supports compute, vertex and pixel shader stages only."));return;}
        const uint32 ExpectedStage=Frequency==SF_Compute?1:Frequency==SF_Vertex?2:3;
        if(Input.IsBindlessEnabled()) { Fail(TEXT("WebCuda bindless descriptor ABI is not implemented")); return; }
        struct FParameterConfiguration final : FShaderParameterParser::FPlatformConfiguration {
            FParameterConfiguration():FPlatformConfiguration(TEXTVIEW("cbuffer")) {}
            FString GetStableConstantBufferRegisterString(EShaderFrequency) const override { return TEXT(" : register(b0)"); }
        } Configuration;
        FShaderParameterParser ParameterParser(Configuration);
        FString ParsedSource(Preprocessed.GetSourceViewWide());
        if(!ParameterParser.ParseAndModify(Input,Output.Errors,ParsedSource)) return;
        if(ParameterParser.DidModifyShader()) Output.ModifiedShaderSource=ParsedSource;
        const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        const FString Python=Root/TEXT("engine/tools/emsdk/python/3.13.3_64bit/python.exe");
        const FString Compiler=Root/TEXT("engine/ShaderCompiler/compile_webcuda.py");
        const FString Directory=Root/TEXT("engine/artifacts/shader-format/jobs")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
        if (!IFileManager::Get().MakeDirectory(*Directory,true)) { Fail(TEXT("Cannot create WebCuda shader work directory")); return; }
        const FString SourcePath=Directory/TEXT("source.hlsl");
        if (!FFileHelper::SaveStringToFile(ParsedSource,*SourcePath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) {
            Fail(TEXT("Cannot write preprocessed WebCuda shader")); return;
        }
        const auto Metadata=MakeShared<FJsonObject>();
        Metadata->SetStringField(TEXT("virtualSource"),Input.VirtualSourceFilePath);
        Metadata->SetStringField(TEXT("entry"),Input.EntryPointName);
        Metadata->SetBoolField(TEXT("stableRootParameters"),Input.ShouldUseStableConstantBuffer());
        Metadata->SetBoolField(TEXT("parameterParserModifiedSource"),ParameterParser.DidModifyShader());
        FString MetadataJson; FJsonSerializer::Serialize(Metadata,TJsonWriterFactory<>::Create(&MetadataJson));
        FFileHelper::SaveStringToFile(MetadataJson,*(Directory/TEXT("input.json")));
        // No shell is used. Paths are generated locally and the entry point is
        // checked before putting it into the Windows argument string.
        if (Input.EntryPointName.IsEmpty()) { Fail(TEXT("Missing shader entry point")); return; }
        for (const TCHAR C:Input.EntryPointName) if (!(FChar::IsAlnum(C)||C==TEXT('_'))) { Fail(TEXT("Invalid shader entry point")); return; }
        FString Arguments=FString::Printf(TEXT("\"%s\" --source \"%s\" --entry %s --output \"%s\""),*Compiler,*SourcePath,*Input.EntryPointName,*Directory);
        Arguments+=FString::Printf(TEXT(" --stage %s"),Frequency==SF_Compute?TEXT("compute"):Frequency==SF_Vertex?TEXT("vertex"):TEXT("fragment"));
        // HLSL element types do not specify storage texture formats. This
        // project profile is an explicit contract, retained in WCSH v2 for
        // runtime view validation. It covers the default Lumen lighting format.
        const FString FormatsConfig=Root/TEXT("Config/WebCudaStorageFormats.json");
        if(IFileManager::Get().FileExists(*FormatsConfig)) {
            FString Text;TSharedPtr<FJsonObject> Config;const TArray<TSharedPtr<FJsonValue>>* Shaders=nullptr;
            int32 Version=0;
            if(!FFileHelper::LoadFileToString(Text,*FormatsConfig) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Config) ||
               !Config.IsValid() || !Config->TryGetNumberField(TEXT("version"),Version) || Version!=1 || !Config->TryGetArrayField(TEXT("shaders"),Shaders)) {
                Fail(TEXT("Invalid WebCuda storage-format profile"));return;
            }
            bool Matched=false;
            for(const auto& Value:*Shaders) {
                const TSharedPtr<FJsonObject>* Item=nullptr;FString Source,Entry;
                if(!Value->TryGetObject(Item) || !(*Item)->TryGetStringField(TEXT("source"),Source) || !(*Item)->TryGetStringField(TEXT("entry"),Entry)) {Fail(TEXT("Invalid storage-format profile entry"));return;}
                if(Source!=Input.VirtualSourceFilePath || Entry!=Input.EntryPointName)continue;
                const TSharedPtr<FJsonObject>* Formats=nullptr;
                if(Matched || !(*Item)->TryGetObjectField(TEXT("formats"),Formats)) {Fail(TEXT("Duplicate or invalid storage-format profile"));return;}
                Matched=true;FString FormatJson;
                FJsonSerializer::Serialize((*Formats).ToSharedRef(),TJsonWriterFactory<>::Create(&FormatJson));
                const FString FormatFile=Directory/TEXT("storage-formats.json");
                if(!FFileHelper::SaveStringToFile(FormatJson,*FormatFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) {Fail(TEXT("Cannot write storage-format profile"));return;}
                Arguments+=FString::Printf(TEXT(" --formats \"%s\""),*FormatFile);
            }
        }
        int32 ExitCode=-1; FString Stdout,Stderr;
        if (!FPlatformProcess::ExecProcess(*Python,*Arguments,&ExitCode,&Stdout,&Stderr) || ExitCode!=0) {
            Fail(FString::Printf(TEXT("WebCuda compiler failed (%d): %s %s [artifacts: %s]"),ExitCode,*Stdout,*Stderr,*Directory)); return;
        }
        TArray<uint8> Bytes; FString Json; TSharedPtr<FJsonObject> Manifest;
        if (!FFileHelper::LoadFileToArray(Bytes,*(Directory/TEXT("shader.wcs"))) ||
            !FFileHelper::LoadFileToString(Json,*(Directory/TEXT("manifest.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Manifest) || !Manifest.IsValid()) {
            Fail(TEXT("WebCuda compiler did not produce a shader and reflection manifest")); return;
        }
        UEWebGPU::ShaderCode Code; std::string Error;
        if (!UEWebGPU::ShaderCode::Decode(Bytes.GetData(),Bytes.Num(),Code,Error)) { Fail(UTF8_TO_TCHAR(Error.c_str())); return; }
        if(Code.Stage!=ExpectedStage){Fail(TEXT("WebCuda returned the wrong shader stage"));return;}
        FString ConstantMode;
        if(!Manifest->TryGetStringField(TEXT("constantMode"),ConstantMode)) { Fail(TEXT("Missing constant-buffer reflection mode")); return; }
        if(Input.ShouldUseStableConstantBuffer() && ConstantMode==TEXT("globals")) {
            Fail(TEXT("Shader has live global constants outside Unreal's root parameter structure")); return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Parameters=nullptr;
        if (!Manifest->TryGetArrayField(TEXT("parameters"),Parameters)) { Fail(TEXT("Missing WebCuda parameter map")); return; }
        int32 RootSize=0;
        TBitArray<> UsedUniformBuffers(false,32);
        UsedUniformBuffers[0]=Code.ConstantBytes!=0;
        for (const auto& Value:*Parameters) {
            const TSharedPtr<FJsonObject>* P=nullptr;
            FString Name,Kind; int32 Buffer=0,Offset=0,Size=0;
            if (!Value->TryGetObject(P) || !(*P)->TryGetStringField(TEXT("name"),Name) || !(*P)->TryGetStringField(TEXT("kind"),Kind) ||
                !(*P)->TryGetNumberField(TEXT("buffer"),Buffer) || !(*P)->TryGetNumberField(TEXT("offset"),Offset) || !(*P)->TryGetNumberField(TEXT("size"),Size) ||
                Name.IsEmpty() || Buffer<0 || Buffer>=32 || Offset<0 || Size<0 || (!Size&&Kind!=TEXT("UniformBuffer")) || int64(Offset)+Size>65535) { Fail(TEXT("Invalid WebCuda parameter reflection")); return; }
            EShaderParameterType Type;
            if (Kind==TEXT("LooseData")) Type=EShaderParameterType::LooseData;
            else if (Kind==TEXT("SRV")) Type=EShaderParameterType::SRV;
            else if (Kind==TEXT("UAV")) Type=EShaderParameterType::UAV;
            else if (Kind==TEXT("Texture")) Type=EShaderParameterType::SRV;
            else if (Kind==TEXT("Sampler")) Type=EShaderParameterType::Sampler;
            else if (Kind==TEXT("UniformBuffer")) { Type=EShaderParameterType::UniformBuffer;UsedUniformBuffers[Buffer]=true; }
            else { Fail(TEXT("Unimplemented WebCuda parameter type")); return; }
            if(Type==EShaderParameterType::LooseData && Input.ShouldUseStableConstantBuffer()) {
                const auto* Parsed=ParameterParser.FindParameterInfosUnsafe(Name);
                if(Buffer!=FShaderParametersMetadata::kRootCBufferBindingIndex || !Parsed || !Parsed->IsFound() || Parsed->ConstantBufferOffset!=Offset) {
                    Fail(FString::Printf(TEXT("WebCuda root parameter %s does not preserve Unreal's buffer slot/offset"),*Name)); return;
                }
                HandleReflectedRootConstantBufferMember(Input,ParameterParser,Name,Offset,Size,Output);
                RootSize=FMath::Max(RootSize,Offset+Size);
            } else {
                Output.ParameterMap.AddParameterAllocation(Name,uint16(Buffer),uint16(Offset),uint16(Size),Type);
            }
        }
        if(RootSize) HandleReflectedRootConstantBuffer(RootSize,Output);
        if(!Output.Errors.IsEmpty()) return;
        if(Code.Version>=2) {
            FShaderCompilerEnvironment Environment=Input.Environment;
            if(Input.SharedEnvironment.IsValid())Environment.Merge(*Input.SharedEnvironment);
            FShaderCompilerResourceTable Table;
            if(!BuildResourceTableMapping(Environment.ResourceTableMap,Environment.UniformBufferMap,UsedUniformBuffers,Output.ParameterMap,Table)) {Fail(TEXT("Unreal resource-table mapping failed"));return;}
            if(!Table.ResourceCollectionMap.IsEmpty()) {Fail(TEXT("WebCuda resource collections are not supported"));return;}
            Code.ResourceTableBits=Table.ResourceTableBits;
            for(uint32 Hash:Table.ResourceTableLayoutHashes)Code.ResourceTableLayoutHashes.push_back(Hash);
            auto AddTable=[&](const TArray<uint32>& Map,uint32 Kind){
                for(uint32 Token:Map)Code.ResourceTableBindings.push_back({FRHIResourceTableEntry::GetUniformBufferIndex(Token),FRHIResourceTableEntry::GetResourceIndex(Token),FRHIResourceTableEntry::GetBindIndex(Token),Kind});
            };
            AddTable(Table.TextureMap,4);AddTable(Table.ShaderResourceViewMap,1);AddTable(Table.UnorderedAccessViewMap,2);AddTable(Table.SamplerMap,6);
            FShaderResourceTable RuntimeTable;
            UE::ShaderCompilerCommon::BuildShaderResourceTable(Table,RuntimeTable,true);
            auto Matches=[&](const TArray<uint32>& Expected,uint32 Kind){
                const auto Actual=Code.MakeResourceTableTokens(Kind);
                return Actual.size()==Expected.Num() && (!Actual.size() || !FMemory::Memcmp(Actual.data(),Expected.GetData(),Actual.size()*sizeof(uint32)));
            };
            if(!Matches(RuntimeTable.TextureMap,4) || !Matches(RuntimeTable.ShaderResourceViewMap,1) || !Matches(RuntimeTable.UnorderedAccessViewMap,2) || !Matches(RuntimeTable.SamplerMap,6)) {Fail(TEXT("WebCuda runtime resource tokens differ from Unreal"));return;}
            CullGlobalUniformBuffers(Environment.UniformBufferMap,Output.ParameterMap);
            std::vector<uint8_t> Packed;
            if(!Code.EncodeV2(Packed,Error)) {Fail(UTF8_TO_TCHAR(Error.c_str()));return;}
            Bytes.Reset();Bytes.Append(Packed.data(),int32(Packed.size()));
        }
        Output.ShaderCode.GetWriteAccess()=MoveTemp(Bytes);
        Output.bSucceeded=true;
        ParameterParser.ValidateShaderParameterTypes(Input,Output);
    }
};
class FShaderFormatWebCudaModule final : public IShaderFormatModule {
    FShaderFormatWebCuda Format;
public:
    IShaderFormat* GetShaderFormat() override { return &Format; }
};
IMPLEMENT_MODULE(FShaderFormatWebCudaModule,ShaderFormatWebCuda)
