using System;
using System.IO;
using UnrealBuildTool;

public class OodleDataCompression_WebCuda : OodleDataCompression
{
    private string WasmLibrary
    {
        get
        {
            string Library = Environment.GetEnvironmentVariable("WEBCUDA_OODLE_LIBRARY");
            if (string.IsNullOrEmpty(Library) || !Path.IsPathFullyQualified(Library) || !File.Exists(Library))
                throw new BuildException("Build the real Oodle Wasm SDK with engine/Dependencies/build_oodle.py and set WEBCUDA_OODLE_LIBRARY.");
            return Library;
        }
    }
    protected override string LibDirectory => Path.GetDirectoryName(WasmLibrary);
    protected override string ReleaseLibraryName => Path.GetFileName(WasmLibrary);
    protected override string DebugLibraryName => ReleaseLibraryName;
    public OodleDataCompression_WebCuda(ReadOnlyTargetRules Target) : base(Target) { }
}
