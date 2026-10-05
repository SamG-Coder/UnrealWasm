using UnrealBuildTool;
public class UnrealBrowserSampleEditorTarget : TargetRules {
    public UnrealBrowserSampleEditorTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("UnrealBrowserSample");
    }
}
