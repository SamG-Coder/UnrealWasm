using UnrealBuildTool;
using System.IO;
public class ShaderFormatWebCuda : ModuleRules
{
    public ShaderFormatWebCuda(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "RenderCore", "RHI", "TargetPlatform", "ShaderCompilerCommon", "Json" });
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../WebGPURHI/Public"));
    }
}
