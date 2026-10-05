using UnrealBuildTool;
using System;
using System.IO;
public class WebGPURHI : ModuleRules
{
    public WebGPURHI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "RHI" });
        PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "RHICore" });
        string Sdk = Environment.GetEnvironmentVariable("WEBCUDA_EMSDK") ?? throw new BuildException("Build using engine/ubt.ps1");
        string Headers = Path.Combine(Sdk, "upstream/emscripten/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include");
        if (!File.Exists(Path.Combine(Headers, "webgpu/webgpu.h"))) throw new BuildException("Build the emdawnwebgpu port with embuilder --wasm64 first.");
        PublicSystemIncludePaths.Add(Headers);
    }
}
