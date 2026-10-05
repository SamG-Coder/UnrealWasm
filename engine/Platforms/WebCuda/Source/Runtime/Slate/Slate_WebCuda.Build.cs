using UnrealBuildTool;
public class Slate_WebCuda : Slate
{
    public Slate_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.RemoveAll(Path => Path.Contains("AHEasing"));
        PrivateDependencyModuleNames.Add("WebCudaAHEasing");
    }
}
