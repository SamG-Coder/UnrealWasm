using System;
using System.IO;
using UnrealBuildTool;

public class ICU_WebCuda : ICU
{
    protected override string ICUVersion => ICU64VersionString;
    public ICU_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        var Root=Environment.GetEnvironmentVariable("WEBCUDA_ICU_ROOT");
        if(string.IsNullOrEmpty(Root) || !File.Exists(Path.Combine(Root,"libicu64-wasm64.a")))
            throw new BuildException("Build ICU with engine/Dependencies/build_icu.py, then use engine/ubt.ps1.");
        PublicSystemIncludePaths.Clear();
        PublicSystemIncludePaths.Add(Path.Combine(Root,"include"));
        PublicAdditionalLibraries.Clear();
        PublicAdditionalLibraries.Add(Path.Combine(Root,"libicu64-wasm64.a"));
    }
}
