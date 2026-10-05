using UnrealBuildTool;

public class Launch_WebCuda : Launch
{
    public Launch_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDefinitions.Add("WEBCUDA_BROWSER_ENTRY=1");
    }
}
