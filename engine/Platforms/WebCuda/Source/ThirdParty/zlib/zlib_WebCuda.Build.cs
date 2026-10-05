using UnrealBuildTool;
public class zlib_WebCuda : zlib
{
    public zlib_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicSystemIncludePaths.Clear();
        PublicSystemIncludePaths.Add(WebCudaSlateDependencies.Include("zlib"));
        PublicSystemIncludePaths.Add(WebCudaSlateDependencies.Include("zlib/minizip"));
        PublicAdditionalLibraries.Clear();
        PublicAdditionalLibraries.Add(WebCudaSlateDependencies.Library("z"));
    }
}
