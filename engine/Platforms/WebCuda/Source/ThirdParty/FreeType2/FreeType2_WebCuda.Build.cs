using System;
using System.IO;
using UnrealBuildTool;

public static class WebCudaSlateDependencies
{
    public static string Root {
        get {
            var PathRoot=Environment.GetEnvironmentVariable("WEBCUDA_SLATE_ROOT");
            if(string.IsNullOrEmpty(PathRoot))throw new BuildException("Use engine/ubt.ps1 and build engine/Dependencies/build_slate.py first.");
            return PathRoot;
        }
    }
    public static string Library(string Name) {
        var FileName=Path.Combine(Root,"lib"+Name+"-wasm64.a");
        if(!File.Exists(FileName))throw new BuildException("Missing Slate dependency: "+FileName+". Run engine/Dependencies/build_slate.py.");
        return FileName;
    }
    public static string Include(string Name) => Path.Combine(Root,"include",Name);
}
public class FreeType2_WebCuda : FreeType2
{
    protected override string FreeType2Version => "FreeType2-2.14.1";
    protected override string FreeType2IncPath => WebCudaSlateDependencies.Include("freetype2");
    public FreeType2_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicAdditionalLibraries.Clear();
        PublicAdditionalLibraries.Add(WebCudaSlateDependencies.Library("freetype"));
    }
}
