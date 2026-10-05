using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool;

public partial struct UnrealTargetPlatform
{
    public static UnrealTargetPlatform WebCuda { get; } = FindOrAddByName("WebCuda");
}
public partial struct UnrealArch
{
    public static UnrealArch Wasm64 { get; } = FindOrAddByName("wasm64", bIsX64:false);
}

sealed class WebCudaPlatformSDK : UEBuildPlatformSDK
{
    public static string SdkRoot => Environment.GetEnvironmentVariable("WEBCUDA_EMSDK") ?? throw new BuildException("Set WEBCUDA_EMSDK or invoke engine/ubt.ps1 to locate the Emscripten SDK.");
    public static string Compiler => Path.Combine(SdkRoot,"upstream","emscripten","em++.py");
    public static string Python => Environment.GetEnvironmentVariable("EMSDK_PYTHON") ?? Path.Combine(SdkRoot,"python","3.13.3_64bit","python.exe");
    public static string Archiver => Path.Combine(SdkRoot,"upstream","bin","llvm-ar.exe");
    public WebCudaPlatformSDK(ILogger logger) : base(logger) {}
    public override string GetMainVersion() => "6.0.11";
    protected override void GetValidVersionRange(out string? minVersion,out string? maxVersion) { minVersion="6.0.11";maxVersion="6.0.11"; }
    protected override string? GetInstalledSDKVersion()
    {
        string version=Path.Combine(SdkRoot,"upstream","emscripten","emscripten-version.txt");
        if(!File.Exists(Compiler) || !File.Exists(Python) || !File.Exists(Archiver) || !File.Exists(version)) return null;
        return File.ReadAllText(version).Trim().Trim('"');
    }
    public override bool TryConvertVersionToInt(string? value,out ulong result,string? hint=null)
    {
        result=0;if(value==null)return false;var match=Regex.Match(value,@"^(\d+)(?:\.(\d+))?(?:\.(\d+))?");if(!match.Success)return false;
        result=(ulong.Parse(match.Groups[1].Value)<<32) | ((match.Groups[2].Success?ulong.Parse(match.Groups[2].Value):0)<<16) | (match.Groups[3].Success?ulong.Parse(match.Groups[3].Value):0);return true;
    }
}

sealed class WebCudaPlatform : UEBuildPlatform
{
    // UE's reserved static platform range is 32..47. Keep the runtime and
    // host cooker's generated BuildSettings table on the same enum value.
    public const int ShaderPlatformIndex = 32;
    public WebCudaPlatform(ILogger logger) : base(UnrealTargetPlatform.WebCuda,new WebCudaPlatformSDK(logger),new UnrealArchitectureConfig(UnrealArch.Wasm64),logger) {}
    public override void ModifyModuleRulesForOtherPlatform(string moduleName, ModuleRules rules, ReadOnlyTargetRules target)
    {
        // This hook runs for every platform, including WebCuda itself. Calling
        // it again from the active-platform hook would duplicate registration.
        AddPlatformStaticShaders(target, rules, new[] { new StaticShader { Name="WEBCUDA", Index=ShaderPlatformIndex } });
    }
    public override bool IsBuildProduct(string file,string[] prefixes,string[] suffixes) => file.EndsWith(".wasm",StringComparison.Ordinal) || file.EndsWith(".a",StringComparison.Ordinal) || file.EndsWith(".o",StringComparison.Ordinal);
    public override string GetBinaryExtension(UEBuildBinaryType type) => type==UEBuildBinaryType.StaticLibrary?".a":".wasm";
    public override string[] GetDebugInfoExtensions(ReadOnlyTargetRules target,UEBuildBinaryType type) => Array.Empty<string>();
    public override bool ShouldCompileMonolithicBinary(UnrealTargetPlatform platform) => true;
    public override bool CanUseXGE() => false;
    public override void ValidateTarget(TargetRules target)
    {
        target.bUsePCHFiles=false;
        target.bUseUnityBuild=false;
        target.bCompileISPC=false;
        target.bUseSharedPCHs=false;
        // Optional compression plugins are outside the current port. Core's
        // separate OodleDataCompression dependency uses our real Wasm SDK build.
        target.DisablePlugins.AddRange(new[]{"OodleNetwork", "OodleData", "OodleTexture"});
    }
    public override void SetUpEnvironment(ReadOnlyTargetRules target,CppCompileEnvironment compile,LinkEnvironment link)
    {
        compile.Definitions.AddRange(new[]{"PLATFORM_WEBCUDA=1","OVERRIDE_PLATFORM_HEADER_NAME=WebCuda"});
    }
    public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules target) => target.Configuration!=UnrealTargetConfiguration.Shipping;
    public override UEToolChain CreateToolChain(ReadOnlyTargetRules target) => new WebCudaToolChain(target,Logger);
    public override void Deploy(TargetReceipt receipt) { throw new BuildException("WebCuda browser packaging is not implemented yet. CPU compilation must complete before deployment."); }
}

sealed class WebCudaPlatformFactory : UEBuildPlatformFactory
{
    public override UnrealTargetPlatform TargetPlatform => UnrealTargetPlatform.WebCuda;
    public override void RegisterBuildPlatforms(ILogger logger) => UEBuildPlatform.RegisterBuildPlatform(new WebCudaPlatform(logger),logger);
}
