using System.IO;
using UnrealBuildTool;

public class HTTP_WebCuda : HTTP
{
    protected override bool bPlatformSupportsLocalHttpServer => false;
    protected override bool bPlatformEventLoopEnabledByDefault => false;
    public HTTP_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(Path.Combine(PlatformModuleDirectory, "Public"));
    }
}
