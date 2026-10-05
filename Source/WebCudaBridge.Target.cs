using UnrealBuildTool;
[SupportedPlatforms("WebCuda")]
public class WebCudaBridgeTarget : TargetRules {
    public WebCudaBridgeTarget(TargetInfo target) : base(target) {
        Type = TargetType.Program;
        LinkType = TargetLinkType.Monolithic;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        LaunchModuleName = "WebCudaBridge";
        bCompileAgainstEngine = false;
        bCompileAgainstCoreUObject = false;
        bCompileAgainstApplicationCore = false;
        bBuildDeveloperTools = false;
        bBuildWithEditorOnlyData = false;
        bCompileICU = false;
        bCompileCEF3 = false;
        bUseLoggingInShipping = false;
        bUseChecksInShipping = false;
        bEnableTrace = false;
    }
}
