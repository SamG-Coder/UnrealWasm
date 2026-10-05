using UnrealBuildTool;
public class WebCudaTargetPlatform : ModuleRules
{
    public WebCudaTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new[] {"Core", "CoreUObject", "Engine", "TargetPlatform", "DesktopPlatform", "RHI", "RenderCore", "WebCudaTargetPlatformSettings"});
    }
}
