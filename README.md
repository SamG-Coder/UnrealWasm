# UnrealWasm

Experimental **Unreal Engine → Wasm64 → WebGPU** platform and rendering integration.

**Paused development snapshot. This is not a working full-game exporter yet.** Original Unreal shaders execute in browser GPU tests, but the full game has not linked, the complete scene has not cooked, and a browser-rendered Unreal scene has not been demonstrated.

The project aims to preserve Unreal's algorithms and shader sources, including Lumen, while implementing the missing browser platform, compiler and rendering integration. It is not an official Epic Games platform or a stock Unreal HTML5 exporter.

## Rendering path and naming

```text
Unreal C++ → Emscripten Wasm64 → C++ WebGPU backend → browser GPU
Unreal HLSL → Slang → SPIR-V → Tint → WGSL
```

**CUDA is not used by the current engine-rendering path.** Earlier CUDA/OptiX bridge experiments remain here, but the current raster and Lumen-integration tests use WebGPU.

The public project name is **UnrealWasm**. The internal target remains `WebCuda`, the shader format remains `SF_WEBCUDA`, and the sample remains `UnrealBrowserSample.uproject`. Keeping those identifiers avoids disrupting the in-progress integration.

## Current status

| Area | Evidence and limit |
| --- | --- |
| Native sample | Existing Unreal C++ sample and local asset-generation script. Native rendering uses Unreal. |
| Wasm platform | Custom UBT platform/toolchain, Wasm64 adapters, virtual filesystem and browser host. Full engine startup is not linked or executed. |
| Original compute shaders | Browser checks for original Lumen dispatch-argument setup and buffer kernels. One Lumen integration permutation runs its **unlit-material branch**; visible GI is unverified. |
| Original raster shaders | `FScreenVS` and `FScreenPS` export through Unreal's compiler and shader-object binding checks, then draw through C++ Wasm/WebGPU. |
| Earlier raster checkpoint | 45 cases / 138,240 exact bytes passed in both Edge and custom Chromium before the latest context changes. |
| Latest graphics work | Concrete graphics context, shared parameters and persistent render passes are work in progress. An intermediate context revision compiled with UBT Wasm flags; the latest backend passed 60 cases / 184,320 bytes in Edge. This latest revision has not been checked in custom Chromium or through a running Unreal RHI. |
| Actual scene build | Intentionally paused after action **739 of 6,258**. Intermediate files remain local; no completed game Wasm exists. |
| Full Lumen / Nanite / DLSS | Not demonstrated in the exported game. No full browser lighting or performance-parity claim. |

See [the paused checkpoint](docs/STATUS.md), [historical evidence](docs/evidence/README.md), and [the engineering log](engine/README.md). The status document takes precedence over historical log sections.

## Source layout

- `Source/WebGPURHI/`: C++ WebGPU resources, shader containers, pipeline state and command-context work.
- `Source/ShaderFormatWebCuda/`: Unreal shader compiler integration and original-shader probe commandlet.
- `Source/WebCudaTargetPlatform*/`: cooking-platform modules.
- `engine/Platforms/WebCuda/`: platform extensions, UBT toolchain, services and dependency rules.
- `engine/ShaderCompiler/`: shader lowering, reflection, ABI packing and validation.
- `engine/Tests/`: compiler, platform, browser and native-reference checks.
- `engine/Runtime/`: browser support and Wasm64 WebGPU ABI shim.
- `Source/UnrealBrowserSample/`, `shared/`, `scripts/create-assets.py`: native sample and scene inputs.
- `web/`: historical standalone GPU fixture, **not an Unreal export**.

## Getting started

This is a Windows/PowerShell research integration tested against **Unreal Engine 5.8.2**. A fresh checkout contains no engine, SDKs, shader captures, assets or prebuilt exporter. Full clean-machine reproduction has not been verified.

Read [setup notes](docs/SETUP.md) before running the scripts. Obtain your own licensed Unreal installation, Visual Studio C++ tools, Windows SDK and required compiler dependencies. Generate sample assets locally; `Content/` is excluded.

The optional native GPU experiments use [ChromiumRTXCuda](https://github.com/SamG-Coder/ChromiumRTXCuda) and [cuda-webshader](https://github.com/SamG-Coder/cuda-webshader). Ordinary WebGPU checks can run in a compatible standard browser.

## Validation

Public CI checks repository hygiene and source syntax only. It does **not** build Unreal, execute GPU tests, or establish a working exporter.

Local checks require generated inputs from your own Unreal installation. At this pause, `validate-raster-pipeline.py` still describes the earlier 45-case checkpoint. Updating it and rerunning the 60-case suite in both browsers is pending. Historical reports are not passing tests of later source revisions.

## Licensing

Independently authored code here is offered under the [MIT License](LICENSE). This does not cover Unreal Engine, Epic content, Oodle or external dependencies.

Engine source/binaries, generated engine headers/shaders, assets, SDKs and build outputs are excluded. See [external requirements](THIRD_PARTY.md) and [Epic's source-sharing guidance](https://www.unrealengine.com/ue-on-github?lang=en-US). This project is not affiliated with or endorsed by Epic Games.
