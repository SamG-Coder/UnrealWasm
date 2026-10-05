#include "WebCudaShaderProbeCommandlet.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IShaderFormat.h"
#include "ShaderPreprocessTypes.h"
#include "ShaderCompiler.h"
#include "GlobalShader.h"
#include "Serialization/JsonSerializer.h"
#include "RHIStrings.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "WebCudaShaderCode.h"
#include "ShaderParameterMetadata.h"
#include "Misc/FeedbackContext.h"

static TSharedRef<FJsonObject> DescribeParameters(const FShaderParametersMetadata* Metadata)
{
    const auto Object=MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("struct"),Metadata->GetStructTypeName());
    Object->SetStringField(TEXT("shaderName"),Metadata->GetShaderVariableName()?Metadata->GetShaderVariableName():TEXT(""));
    Object->SetNumberField(TEXT("size"),Metadata->GetSize());
    if(Metadata->IsLayoutInitialized()) {
        Object->SetNumberField(TEXT("constantBufferSize"),Metadata->GetLayout().ConstantBufferSize);
        Object->SetNumberField(TEXT("layoutHash"),Metadata->GetLayout().GetHash());
        TArray<TSharedPtr<FJsonValue>> Resources;
        for(const auto& Resource:Metadata->GetLayout().Resources) {
            const auto Item=MakeShared<FJsonObject>();
            Item->SetNumberField(TEXT("offset"),Resource.MemberOffset);
            Item->SetNumberField(TEXT("baseType"),int32(Resource.MemberType));
            Resources.Add(MakeShared<FJsonValueObject>(Item));
        }
        Object->SetArrayField(TEXT("resources"),Resources);
    }
    TArray<TSharedPtr<FJsonValue>> Members;
    for(const auto& Member:Metadata->GetMembers()) {
        const auto Field=MakeShared<FJsonObject>();
        Field->SetStringField(TEXT("name"),Member.GetName());
        Field->SetStringField(TEXT("shaderType"),Member.GetShaderType());
        Field->SetNumberField(TEXT("offset"),Member.GetOffset());
        Field->SetNumberField(TEXT("baseType"),int32(Member.GetBaseType()));
        Field->SetNumberField(TEXT("rows"),Member.GetNumRows());
        Field->SetNumberField(TEXT("columns"),Member.GetNumColumns());
        Field->SetNumberField(TEXT("elements"),Member.GetNumElements());
        Field->SetBoolField(TEXT("native"),Member.IsVariableNativeType());
        Field->SetBoolField(TEXT("included"),Member.GetBaseType()==UBMT_INCLUDED_STRUCT);
        Field->SetBoolField(TEXT("nested"),Member.GetBaseType()==UBMT_NESTED_STRUCT);
        if(Member.IsVariableNativeType()) Field->SetNumberField(TEXT("size"),Member.GetMemberSize());
        if(Member.GetStructMetadata()) Field->SetObjectField(TEXT("structure"),DescribeParameters(Member.GetStructMetadata()));
        Members.Add(MakeShared<FJsonValueObject>(Field));
    }
    Object->SetArrayField(TEXT("members"),Members);
    return Object;
}

UWebCudaShaderProbeCommandlet::UWebCudaShaderProbeCommandlet() { IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true; }
int32 UWebCudaShaderProbeCommandlet::Main(const FString& Params) {
    FString SourcePath,Entry,OutputPath,ShaderTypeName;
    const bool bOriginalShader=FParse::Value(*Params,TEXT("ShaderType="),ShaderTypeName);
    if (!FParse::Value(*Params,TEXT("Output="),OutputPath)) return 1;
    if(!bOriginalShader && (!FParse::Value(*Params,TEXT("Source="),SourcePath) || !FParse::Value(*Params,TEXT("Entry="),Entry))) return 1;
    const IShaderFormat* Format=GetTargetPlatformManagerRef().FindShaderFormat(TEXT("SF_WEBCUDA"));
    const EShaderPlatform Platform=ShaderFormatToLegacyShaderPlatform(TEXT("SF_WEBCUDA"));
    if (!Format || Platform==SP_NumPlatforms) return 2;
    FShaderCompilerInput Input;
    FShaderPreprocessOutput Preprocessed;
    const FGlobalShaderType* OriginalType=nullptr;
    int32 Permutation=0;
    if(bOriginalShader) {
        if(!GShaderCompilingManager) { UE_LOG(LogTemp,Error,TEXT("Original shader preprocessing requires the compiler manager; use -noshaderworker, not -NoShaderCompile.")); return 8; }
        const FShaderType* Type=FindShaderTypeByName(FHashedName(*ShaderTypeName));
        const FGlobalShaderType* GlobalType=Type ? Type->AsGlobalShaderType() : nullptr;
        if(!GlobalType) { UE_LOG(LogTemp,Error,TEXT("Unknown original global shader %s"),*ShaderTypeName); return 3; }
        FParse::Value(*Params,TEXT("Permutation="),Permutation);
        if(Permutation<0 || Permutation>=GlobalType->GetPermutationCount() || !GlobalType->ShouldCompilePermutation(Platform,Permutation,EShaderPermutationFlags::None)) {
            UE_LOG(LogTemp,Error,TEXT("Original shader rejects permutation %d for this platform"),Permutation); return 9;
        }
        OriginalType=GlobalType;
        GlobalType->SetupCompileEnvironment(Platform,Permutation,EShaderPermutationFlags::None,Input.Environment);
        GlobalBeginCompileShader(TEXT("Global"),nullptr,Type,nullptr,Permutation,Type->GetShaderFilename(),Type->GetFunctionName(),FShaderTarget(Type->GetFrequency(),Platform),Input);
        FShaderCompilerEnvironment Environment=Input.Environment;
        if(Input.SharedEnvironment.IsValid()) Environment.Merge(*Input.SharedEnvironment);
        if(!Format->PreprocessShader(Input,Environment,Preprocessed)) {
            for(const auto& Error:Preprocessed.GetErrors()) UE_LOG(LogTemp,Error,TEXT("%s"),*Error.StrippedErrorMessage);
            return 7;
        }
        FFileHelper::SaveStringToFile(FString(Preprocessed.GetSourceViewWide()),*(OutputPath+TEXT(".preprocessed.hlsl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        const FString PreparedSource(Preprocessed.GetSourceViewWide());
        const auto Metadata=MakeShared<FJsonObject>();
        Metadata->SetStringField(TEXT("shaderType"),ShaderTypeName);
        Metadata->SetNumberField(TEXT("permutation"),Permutation);
        Metadata->SetStringField(TEXT("entry"),Input.EntryPointName);
        if(Input.RootParametersStructure) Metadata->SetObjectField(TEXT("root"),DescribeParameters(Input.RootParametersStructure));
        TArray<TSharedPtr<FJsonValue>> Uniforms;
        for(auto* Node=FShaderParametersMetadata::GetStructList();Node;Node=Node->GetNextLink()) {
            const auto* Struct=**Node;
            if(Struct->GetShaderVariableName() && PreparedSource.Contains(FString(TEXT("cbuffer "))+Struct->GetShaderVariableName()+TEXT(" ")))
                Uniforms.Add(MakeShared<FJsonValueObject>(DescribeParameters(Struct)));
        }
        Metadata->SetArrayField(TEXT("uniformBuffers"),Uniforms);
        FString MetadataJson;FJsonSerializer::Serialize(Metadata,TJsonWriterFactory<>::Create(&MetadataJson));
        if(!FFileHelper::SaveStringToFile(MetadataJson,*(OutputPath+TEXT(".metadata.json")))) return 6;
        if(FParse::Param(*Params,TEXT("MetadataOnly"))) return 0;
    } else {
        TArray<uint8> Source;
        if(!FFileHelper::LoadFileToArray(Source,*SourcePath)) return 3;
        Input.ShaderFormat=TEXT("SF_WEBCUDA"); Input.EntryPointName=Entry; Input.VirtualSourceFilePath=SourcePath;
        Input.Target=FShaderTarget(SF_Compute,Platform);
        Format->ModifyShaderCompilerInput(Input);
        Preprocessed.EditSource().Set(FAnsiStringView(reinterpret_cast<const ANSICHAR*>(Source.GetData()),Source.Num()));
    }
    FShaderCompilerOutput Output;
    Format->CompilePreprocessedShader(Input,Preprocessed,Output);
    for (const auto& Error:Output.Errors) UE_LOG(LogTemp,Error,TEXT("%s"),*Error.StrippedErrorMessage);
    if (!Output.bSucceeded) return 4;
    auto& Bytes=Output.ShaderCode.GetWriteAccess();
    UEWebGPU::ShaderCode Code; std::string Error;
    if (!UEWebGPU::ShaderCode::Decode(Bytes.GetData(),Bytes.Num(),Code,Error)) return 5;
    if (!FFileHelper::SaveArrayToFile(Bytes,*OutputPath)) return 6;
    const int32 ContainerBytes=Bytes.Num();
    if(OriginalType) {
        const int32 ErrorsBefore=GWarn->GetNumErrors();
        Output.GenerateOutputHash();
        // Construct the actual original shader class. Its constructor invokes
        // Unreal's parameter-struct binding and checks all required bindings.
        FShader* Shader=OriginalType->ConstructCompiled(FGlobalShaderType::CompiledShaderInitializerType(OriginalType,nullptr,Permutation,Output,0,nullptr,nullptr));
        Output.ParameterMap.VerifyBindingsAreComplete(OriginalType->GetName(),Output.Target,nullptr);
        DeleteObjectFromLayout(Shader);
        if(GWarn->GetNumErrors()!=ErrorsBefore) {
            UE_LOG(LogTemp,Error,TEXT("Original shader construction reported binding errors"));
            return 10;
        }
    }
    const auto Report=MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("shaderType"),ShaderTypeName);
    Report->SetStringField(TEXT("entry"),Input.EntryPointName);
    Report->SetBoolField(TEXT("originalEnginePreprocessing"),bOriginalShader);
    Report->SetBoolField(TEXT("stableRootParameters"),Input.ShouldUseStableConstantBuffer());
    Report->SetBoolField(TEXT("originalShaderConstructedAndBindingsVerified"),OriginalType!=nullptr);
    Report->SetNumberField(TEXT("containerVersion"),Code.Version);
    Report->SetNumberField(TEXT("stage"),Code.Stage);
    Report->SetNumberField(TEXT("bindGroup"),Code.BindGroup());
    Report->SetNumberField(TEXT("resourceCount"),Code.Resources.size());
    Report->SetNumberField(TEXT("resourceTableBits"),Code.ResourceTableBits);
    TArray<TSharedPtr<FJsonValue>> TableEntries;
    for(const auto& T:Code.ResourceTableBindings) {
        const auto Item=MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("uniformSlot"),T.UniformSlot);Item->SetNumberField(TEXT("resourceIndex"),T.ResourceIndex);
        Item->SetNumberField(TEXT("binding"),T.Binding);Item->SetNumberField(TEXT("kind"),T.Kind);
        TableEntries.Add(MakeShared<FJsonValueObject>(Item));
    }
    Report->SetArrayField(TEXT("resourceTable"),TableEntries);
    TArray<TSharedPtr<FJsonValue>> Allocations;
    for(const auto& Pair:Output.ParameterMap.GetParameterMap()) {
        const auto Allocation=MakeShared<FJsonObject>();
        Allocation->SetStringField(TEXT("name"),Pair.Key);
        Allocation->SetNumberField(TEXT("buffer"),Pair.Value.BufferIndex);
        Allocation->SetNumberField(TEXT("offset"),Pair.Value.BaseIndex);
        Allocation->SetNumberField(TEXT("size"),Pair.Value.Size);
        Allocations.Add(MakeShared<FJsonValueObject>(Allocation));
    }
    Report->SetArrayField(TEXT("allocations"),Allocations);
    FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json,*(OutputPath+TEXT(".json")));
    UE_LOG(LogTemp,Display,TEXT("WebCuda IShaderFormat probe passed: %d bytes, %d resources, %d constant mappings; full scene not cooked."),ContainerBytes,int32(Code.Resources.size()),int32(Code.Mappings.size()));
    return 0;
}
