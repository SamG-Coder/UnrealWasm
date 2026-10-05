using UnrealBuildTool;
public class WebCudaTargetPlatformSettings : ModuleRules {
    public WebCudaTargetPlatformSettings(ReadOnlyTargetRules Target) : base(Target) {
        PublicDependencyModuleNames.AddRange(new[] { "Core", "TargetPlatform" });
        PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "Engine", "RHI", "RenderCore" });
    }
}
