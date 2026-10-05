#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITargetPlatformSettingsModule.h"
#include "Common/TargetPlatformSettingsBase.h"
#include "Engine/TextureLODSettings.h"
#include "Engine/StaticMesh.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RHIStrings.h"
#include "WebCudaCookProperties.h"

class FWebCudaSettings final : public TTargetPlatformSettingsBase<FWebCudaCookProperties> {
    const UTextureLODSettings* TextureLOD=nullptr;
    FStaticMeshLODSettings MeshLOD;
public:
    FWebCudaSettings() { MeshLOD.Initialize(this); }
    void GetAllPossibleShaderFormats(TArray<FName>& Formats) const override {
        const EShaderPlatform Platform=ShaderFormatToLegacyShaderPlatform(TEXT("SF_WEBCUDA"));
        if(Platform!=SP_NumPlatforms && FDataDrivenShaderPlatformInfo::IsValid(Platform)) Formats.AddUnique(TEXT("SF_WEBCUDA"));
    }
    void GetAllTargetedShaderFormats(TArray<FName>& Formats) const override { GetAllPossibleShaderFormats(Formats); }
    void GetShaderFormatModuleHints(TArray<FName>& Modules) const override { Modules.AddUnique(TEXT("ShaderFormatWebCuda")); }
    const UTextureLODSettings& GetTextureLODSettings() const override { check(TextureLOD); return *TextureLOD; }
    void RegisterTextureLODSettings(const UTextureLODSettings* Settings) override { TextureLOD=Settings; }
    const FStaticMeshLODSettings& GetStaticMeshLODSettings() const override { return MeshLOD; }
    void GetPossibleArchitectures(TArray<FString>& Architectures) const override { Architectures.Add(TEXT("wasm64")); }
};

// UE 5.8 enumerates this module separately to initialize RenderUtils' shader
// platform masks (including Lumen velocity depth). The merged target alone
// does not register its settings in that enumeration.
class FWebCudaTargetPlatformSettingsModule final : public ITargetPlatformSettingsModule {
    void GetTargetPlatformSettings(TArray<ITargetPlatformSettings*>& Platforms) override { Platforms.Add(new FWebCudaSettings()); }
};
IMPLEMENT_MODULE(FWebCudaTargetPlatformSettingsModule,WebCudaTargetPlatformSettings)
