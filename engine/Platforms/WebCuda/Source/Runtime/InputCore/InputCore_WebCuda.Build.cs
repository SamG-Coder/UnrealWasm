using System.IO;
using UnrealBuildTool;
public class InputCore_WebCuda : InputCore
{
    public InputCore_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(Path.Combine(PlatformModuleDirectory,"Public"));
    }
}
