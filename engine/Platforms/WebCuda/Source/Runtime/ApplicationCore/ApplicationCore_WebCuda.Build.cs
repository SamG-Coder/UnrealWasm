using System.IO;
using UnrealBuildTool;

public class ApplicationCore_WebCuda : ApplicationCore
{
    public ApplicationCore_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(Path.Combine(PlatformModuleDirectory, "Public"));
        PublicDefinitions.Add("WEBCUDA_BROWSER_APPLICATION=1");
    }
}
