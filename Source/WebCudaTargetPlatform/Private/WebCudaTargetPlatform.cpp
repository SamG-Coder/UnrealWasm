#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITargetPlatformModule.h"
#include "Common/TargetPlatformBase.h"
#include "Common/TargetPlatformSettingsBase.h"
#include "Common/TargetPlatformControlsBase.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "Engine/TextureLODSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "RHIStrings.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatformSettingsModule.h"
#include "WebCudaCookProperties.h"

class FWebCudaControls final : public TTargetPlatformControlsBase<FWebCudaCookProperties>
{
public:
    explicit FWebCudaControls(ITargetPlatformSettings* Settings):TTargetPlatformControlsBase(Settings){}
    void EnableDeviceCheck(bool) override {}
    void GetAllDevices(TArray<ITargetDevicePtr>&) const override {}
    ITargetDevicePtr GetDefaultDevice() const override {return nullptr;}
    ITargetDevicePtr GetDevice(const FTargetDeviceId&) override {return nullptr;}
    bool GenerateStreamingInstallManifest(const TMultiMap<FString,int32>&,const TSet<int32>&) const override {return false;}
    bool PackageBuild(const FString&) override {return false;}
    bool CanSupportRemoteShaderCompile() const override {return false;}
    bool IsSdkInstalled(bool,FString& Documentation) const override {
        Documentation=TEXT("https://emscripten.org/docs/getting_started/downloads.html");
        return FPaths::FileExists(FPaths::ProjectDir()/TEXT("engine/tools/emsdk/upstream/emscripten/em++.py"));
    }
    void GetTextureFormats(const UTexture* Texture,TArray<TArray<FName>>& Formats) const override {
        // Avoid claiming GPU compressed-format support before the RHI validates it.
        TArray<FName> Layers; for(int32 I=0;I<Texture->Source.GetNumLayers();++I)Layers.Add(TEXT("BGRA8"));Formats.Add(MoveTemp(Layers));
    }
    void InitializeForCook() override {
        // A cook must not silently emit Windows bytecode or shaderless packages.
        if(ShaderFormatToLegacyShaderPlatform(TEXT("SF_WEBCUDA"))==SP_NumPlatforms)
            UE_LOG(LogTemp,Fatal,TEXT("WebCuda target loaded, but SF_WEBCUDA is not registered with the engine shader-platform ABI. Cook refused; no fallback shader platform is selected."));
        const EShaderPlatform ShaderPlatform=ShaderFormatToLegacyShaderPlatform(TEXT("SF_WEBCUDA"));
        if(!FDataDrivenShaderPlatformInfo::IsValid(ShaderPlatform))
            UE_LOG(LogTemp,Fatal,TEXT("WebCuda shader enum is registered, but its data-driven shader-platform metadata is missing. Cook refused."));
        UE_LOG(LogTemp,Display,TEXT("WebCuda shader ABI registered: enum=%u, format=SF_WEBCUDA, Lumen=%d, Nanite=%d"),
            uint32(ShaderPlatform),FDataDrivenShaderPlatformInfo::GetSupportsLumenGI(ShaderPlatform),FDataDrivenShaderPlatformInfo::GetSupportsNanite(ShaderPlatform));
        if(!GetTargetPlatformManagerRef().FindShaderFormat(TEXT("SF_WEBCUDA")))
            UE_LOG(LogTemp,Fatal,TEXT("WebCuda shader ABI and metadata are registered, but ShaderFormatWebCuda has no compiler implementation. Cook refused; original Lumen/Nanite permutations have not been compiled."));
    }
};
class FWebCudaTargetPlatformModule final : public ITargetPlatformModule
{
    ITargetPlatformSettings* Settings=nullptr;
    TUniquePtr<FWebCudaControls> Controls;
    // ITargetPlatformModule owns and deletes the returned platforms.
    FTargetPlatformMerged* Platform=nullptr;
public:
    void GetTargetPlatforms(TArray<ITargetPlatform*>& Platforms) override {
        if(!Platform){
            auto& SettingsModule=FModuleManager::LoadModuleChecked<ITargetPlatformSettingsModule>(TEXT("WebCudaTargetPlatformSettings"));
            const auto Available=SettingsModule.GetTargetPlatformSettings();
            check(Available.Num()==1);
            Settings=Available[0]; Controls=MakeUnique<FWebCudaControls>(Settings); Platform=new FTargetPlatformMerged(Settings,Controls.Get());
        }
        Platforms.Add(Platform);
    }
};
IMPLEMENT_MODULE(FWebCudaTargetPlatformModule,WebCudaTargetPlatform)
