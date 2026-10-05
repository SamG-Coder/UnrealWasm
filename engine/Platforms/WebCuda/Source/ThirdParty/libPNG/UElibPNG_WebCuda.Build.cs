using UnrealBuildTool;
public class UElibPNG_WebCuda : UElibPNG
{
    protected override string IncPNGPath => WebCudaSlateDependencies.Include("png");
    public UElibPNG_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicAdditionalLibraries.Clear();
        PublicAdditionalLibraries.Add(WebCudaSlateDependencies.Library("png"));
        AddEngineThirdPartyPrivateStaticDependencies(Target,"zlib");
    }
}
