using System.IO;
using System;
using UnrealBuildTool;
public class Core_WebCuda : Core
{
    public Core_WebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(Path.Combine(PlatformModuleDirectory, "Public"));
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Core", "Private"));
        PublicDefinitions.Add("TRACE_PRIVATE_THREAD_YIELD=1");
        var Headers = Environment.GetEnvironmentVariable("WEBCUDA_GENERATED_HEADERS");
        if (string.IsNullOrEmpty(Headers) || !File.Exists(Path.Combine(Headers, "GenericPlatform", "GenericPlatformProcess.h")))
            throw new BuildException("Run engine/prepare-platform-headers.ps1 and build using engine/ubt.ps1.");
        PublicIncludePaths.Add(Headers);
    }
}
