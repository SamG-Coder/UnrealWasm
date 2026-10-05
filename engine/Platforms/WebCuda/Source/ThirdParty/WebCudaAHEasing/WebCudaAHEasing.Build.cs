using UnrealBuildTool;
public class WebCudaAHEasing : ModuleRules
{
    public WebCudaAHEasing(ReadOnlyTargetRules Target) : base(Target)
    {
        Type=ModuleType.External;
        PublicSystemIncludePaths.Add(WebCudaSlateDependencies.Include(""));
        PublicDefinitions.Add("AH_EASING_USE_DBL_PRECIS=1");
        PublicAdditionalLibraries.Add(WebCudaSlateDependencies.Library("aheasing"));
    }
}
