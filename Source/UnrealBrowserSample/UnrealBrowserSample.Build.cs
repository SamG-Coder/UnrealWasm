using UnrealBuildTool;
public class UnrealBrowserSample : ModuleRules {
    public UnrealBrowserSample(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","InputCore","Json","RenderCore","RHI"});
    }
}
