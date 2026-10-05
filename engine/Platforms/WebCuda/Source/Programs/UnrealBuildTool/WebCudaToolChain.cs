using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool;

// Early CPU toolchain. It deliberately does not supply a dummy graphics backend
// or claim the installed engine's Windows archives are WebAssembly libraries.
sealed class WebCudaToolChain : UEToolChain
{
    readonly bool UseWebGPU;
    readonly FileReference? WebGPUBridge;
    public WebCudaToolChain(ReadOnlyTargetRules target,ILogger logger) : base(logger) {
        UseWebGPU=target.ExtraModuleNames.Contains("WebGPURHI");
        if(UseWebGPU) {
            if(target.ProjectFile==null)throw new BuildException("WebGPURHI requires a project-owned browser ABI library.");
            WebGPUBridge=FileReference.Combine(target.ProjectFile.Directory,"engine","Runtime","webgpu-wasm64.js");
            if(!FileReference.Exists(WebGPUBridge))throw new BuildException("Missing WebGPU Wasm64 ABI library: {0}",WebGPUBridge);
        }
    }
    static string Quote(string value) => "\""+value.Replace("\\","/",StringComparison.Ordinal).Replace("\"","\\\"",StringComparison.Ordinal)+"\"";
    static string CompilerFlags()
    {
        return "-m64 -msimd128 -msse4.1 -fvisibility=hidden";
    }
    protected override CPPOutput CompileCPPFiles(CppCompileEnvironment env,IEnumerable<FileItem> inputs,DirectoryReference outputDir,string module,IActionGraphBuilder graph)
    {
        CPPOutput result=new();
        foreach(FileItem input in inputs)
        {
            var output=FileItem.GetItemByFileReference(FileReference.Combine(outputDir,Path.GetFileName(input.AbsolutePath)+".o"));
            var args=new List<string>{CompilerFlags(),"-std=c++20","-c",Quote(input.AbsolutePath),"-o",Quote(output.AbsolutePath)};
            if(module=="WebGPURHI")args.Add("--use-port=emdawnwebgpu");
            args.Add(env.bOptimizeCode?"-O2":"-O0");
            args.Add(env.bUseRTTI?"-frtti":"-fno-rtti");
            args.Add(env.bEnableExceptions?"-fexceptions":"-fno-exceptions");
            if(env.bCreateDebugInfo)args.Add("-g");
            if(!String.IsNullOrWhiteSpace(env.AdditionalArguments))args.Add(env.AdditionalArguments);
            var dependencies=FileItem.GetItemByFileReference(new FileReference(output.AbsolutePath+".d"));
            args.Add("-MD -MF "+Quote(dependencies.AbsolutePath));
            // The adapted generic header must resolve identically through all
            // include chains; a differently named copy defeats pragma once.
            var headers=Environment.GetEnvironmentVariable("WEBCUDA_GENERATED_HEADERS");
            if(!String.IsNullOrEmpty(headers))args.Add("-I"+Quote(headers));
            foreach(var include in env.UserIncludePaths)args.Add("-I"+Quote(include.FullName));
            foreach(var include in env.SystemIncludePaths)args.Add("-isystem "+Quote(include.FullName));
            foreach(var define in env.Definitions)args.Add("-D"+Quote(define));
            foreach(var include in env.ForceIncludeFiles)args.Add("-include "+Quote(include.AbsolutePath));
            var response=graph.CreateIntermediateTextFile(new FileReference(output.AbsolutePath+".rsp"),args);
            var action=graph.CreateAction(ActionType.Compile);
            action.CommandPath=new FileReference(WebCudaPlatformSDK.Python);
            action.CommandArguments=Quote(WebCudaPlatformSDK.Compiler)+" @"+Quote(response.AbsolutePath);
            action.WorkingDirectory=Unreal.EngineSourceDirectory;
            action.CommandDescription="Compile Wasm64";action.StatusDescription=Path.GetFileName(input.AbsolutePath);
            action.bCanExecuteRemotely=false;action.PrerequisiteItems.Add(input);action.PrerequisiteItems.Add(response);action.ProducedItems.Add(output);
            action.DependencyListFile=dependencies;action.ProducedItems.Add(dependencies);
            action.PrerequisiteItems.UnionWith(env.ForceIncludeFiles);
            result.ObjectFiles.Add(output);
        }
        return result;
    }
    protected override CPPOutput CompileISPCFiles(CppCompileEnvironment env,IEnumerable<FileItem> inputs,DirectoryReference outputDir,IActionGraphBuilder graph)
    {
        // UBT invokes this hook even when ISPC compilation is disabled. Match
        // Epic's ISPCToolChain behavior so existing C++ fallback paths can build.
        if(!env.bCompileISPC)return new CPPOutput();
        throw new BuildException("ISPC is not supported by WebCuda. Disable ISPC for this target.");
    }
    protected override CPPOutput GenerateISPCHeaders(CppCompileEnvironment env,IEnumerable<FileItem> inputs,DirectoryReference outputDir,IActionGraphBuilder graph)
    {
        if(!env.bCompileISPC)return new CPPOutput();
        throw new BuildException("ISPC is not supported by WebCuda.");
    }
    public override FileItem? LinkFiles(LinkEnvironment env,bool importOnly,IActionGraphBuilder graph)
    {
        if(importOnly)return null;
        var output=FileItem.GetItemByFileReference(env.OutputFilePath);
        bool archive=env.bIsBuildingLibrary;
        var args=new List<string>();
        if(archive){args.Add("rcs");args.Add(Quote(output.AbsolutePath));}
        else {
            args.Add(CompilerFlags());
            // Explicitly preserve Emscripten's current 2 GiB growth ceiling.
            // This also reserves the high pointer tag bit used by AssetRegistry.
            args.Add("-sMAXIMUM_MEMORY=2147483648");
            if(UseWebGPU)args.Add("--use-port=emdawnwebgpu -sWASMFS=1 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS,callMain -sEXPORTED_FUNCTIONS=_main -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1");
            else args.Add("--no-entry -sSTANDALONE_WASM=1");
            if(UseWebGPU)args.Add("--js-library "+Quote(WebGPUBridge!.FullName));
            args.Add("-sERROR_ON_UNDEFINED_SYMBOLS=1");args.Add("-Wl,--export-dynamic");
            args.Add("-o "+Quote(UseWebGPU?Path.ChangeExtension(output.AbsolutePath,".mjs"):output.AbsolutePath));
        }
        foreach(var input in env.InputFiles)args.Add(Quote(input.AbsolutePath));
        foreach(var library in env.Libraries)args.Add(Quote(library.FullName));
        if(!String.IsNullOrWhiteSpace(env.AdditionalArguments))args.Add(env.AdditionalArguments);
        var response=graph.CreateIntermediateTextFile(FileReference.Combine(env.IntermediateDirectory!,Path.GetFileName(output.AbsolutePath)+".rsp"),args);
        var action=graph.CreateAction(ActionType.Link);
        action.CommandPath=new FileReference(archive?WebCudaPlatformSDK.Archiver:WebCudaPlatformSDK.Python);
        action.CommandArguments=(archive?"":Quote(WebCudaPlatformSDK.Compiler)+" ")+"@"+Quote(response.AbsolutePath);action.WorkingDirectory=Unreal.EngineSourceDirectory;
        action.CommandDescription=archive?"Archive Wasm64":"Link Wasm64";action.StatusDescription=Path.GetFileName(output.AbsolutePath);
        action.bCanExecuteRemotely=false;action.PrerequisiteItems.UnionWith(env.InputFiles);action.PrerequisiteItems.Add(response);action.ProducedItems.Add(output);
        if(UseWebGPU && !archive)action.PrerequisiteItems.Add(FileItem.GetItemByFileReference(WebGPUBridge!));
        if(UseWebGPU && !archive)action.ProducedItems.Add(FileItem.GetItemByFileReference(new FileReference(Path.ChangeExtension(output.AbsolutePath,".mjs"))));
        return output;
    }
}
