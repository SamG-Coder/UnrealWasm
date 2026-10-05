using UnrealBuildTool;
public class UnrealBrowserSampleTarget : TargetRules {
    public UnrealBrowserSampleTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("UnrealBrowserSample");
        if (Target.Platform.ToString() == "WebCuda") {
            ExtraModuleNames.Add("WebGPURHI");
            // This static-mesh sample does not use optional engine plugins
            // (animation codecs, media players, online services, etc.). Keep
            // the browser build focused on its real engine/runtime dependencies.
            bAllowEnginePluginsEnabledByDefault = false;
        }
    }
}
