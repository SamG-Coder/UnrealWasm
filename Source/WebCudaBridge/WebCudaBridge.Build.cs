using UnrealBuildTool;
public class WebCudaBridge : ModuleRules {
    public WebCudaBridge(ReadOnlyTargetRules target) : base(target) {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseRTTI = false;
        bEnableExceptions = false;
    }
}
