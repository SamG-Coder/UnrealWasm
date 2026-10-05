using UnrealBuildTool;
public class HarfBuzz_WebCuda : HarfBuzz
{
    protected override string HarfBuzzVersion => "harfbuzz-2.4.0";
    protected override string IncHarfBuzzRootPath => WebCudaSlateDependencies.Include("harfbuzz");
    public HarfBuzz_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        if(!Target.bCompileFreeType || !Target.bCompileICU)return;
        PublicAdditionalLibraries.Clear();
        PublicAdditionalLibraries.Add(WebCudaSlateDependencies.Library("harfbuzz"));
        AddEngineThirdPartyPrivateStaticDependencies(Target,"ICU","FreeType2");
    }
}
