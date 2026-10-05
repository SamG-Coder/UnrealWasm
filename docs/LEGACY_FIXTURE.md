# Legacy browser fixture notes

These are historical local-lab instructions. The standalone fixture is not an Unreal export. Generated artifacts, screenshots and dependencies referenced below are not distributed. Paths are reference-machine defaults and must be adapted.

# Unreal Browser Sample

A small starting fixture for an Unreal-to-browser project, created in `D:\Test\UnrealBrowserSample`.

**This is a native Unreal sample plus working browser GPU probes and an incomplete engine port. It is not yet a full Unreal WebAssembly exporter.** The installed stock UE 5.8.2 has no Web platform target. The new [WebCuda engine integration](engine/README.md) builds a Wasm64 dispatch bridge through custom UnrealBuildTool and runs one original Lumen compute pass on browser CUDA with exact native GPU parity. The larger Lumen integration shader now compiles through Unreal into a WCSH v2 container, passes original shader-object binding checks, and dispatches its unlit-material branch through C++ Wasm/WebGPU with 21 resource bindings and exact R8/packed-lighting texture checks in Edge and custom Chromium. Seven bindings resolve through Unreal's resource tables. Its lit-material path and full lighting have not been validated. `profiles.json` still records the full-game boundary.

The latest actual-scene attempt runs `/Game/Sample` successfully in native Unreal.
The full Wasm build remains incomplete and is being compiled through the custom
platform target. Startup/ICU compilation and cooking-target discovery have progressed
past the earlier failures. The actual scene cook now reaches original global shader
compilation, where the incomplete shader/resource backend prevents completion. See
the [attempt evidence and remaining blockers](engine/README.md#actual-demo-attempt-2026-10-05). The standalone
browser fixture below is not the output of that game build.

| Requested comparison | Current implementation |
|---|---|
| Native | Real C++ Unreal 5.8.2 project, DX12/SM6, Nanite cube asset, Lumen hardware ray tracing configured. Runs via the editor's `-game` executable, not a packaged Shipping build. |
| Web Assembly (Default Unreal) | Engine target unavailable. A real Wasm module supplies scene geometry/camera to a standalone WebGPU capability renderer. This contains no Unreal runtime. |
| Custom Version | The same Wasm fixture, with working native CUDA or OptiX execution through ChromiumRTXCuda. A separate custom platform/compiler integration now validates one original Lumen pass; Unreal's full RHI/runtime port remains incomplete. |

Browser Nanite, Lumen and DLSS are **not implemented**. The probes establish prerequisites; they are not substitutes for those engine systems.

The native baseline has been checked with 12 active Nanite scene proxies, a nonempty Nanite cluster visualization and Lumen's hardware-ray-tracing overview. Both standard Edge and custom Chromium have rendered the browser fixture successfully. Detailed measurements are in the local reports below.

## Open the sample

From PowerShell:

```powershell
cd D:\Test\UnrealBrowserSample
.\scripts\launch.ps1 Custom
```

This starts the local server at <http://127.0.0.1:8088> and opens your custom Chromium. Select **CUDA** or **RTX / OptiX**, then allow native GPU permission. The initial view uses standard WebGPU. The page includes a light slider, two resolutions, measurements, JSON export and the captured native Unreal frame.

```powershell
.\scripts\launch.ps1 WebAssembly # Ordinary Edge: standalone Wasm fixture + WebGPU
.\scripts\launch.ps1 Native      # Interactive native Unreal window
```

The browser server runs locally until its Node process is stopped. Its log is `artifacts/server.log`. The launcher uses isolated browser profiles under `artifacts/profiles/` and does not touch your usual browser profile.

## Rebuild

Dependencies currently used:

- Unreal: `D:\UnrealEngine\UE_5.8` (5.8.2), Visual Studio C++ toolchain and Windows SDK.
- LLVM: `C:\Program Files\LLVM\bin\clang++.exe`, including `wasm-ld`.
- CUDA WebShader: `D:\cuda-webshader` (external working tree, not copied or modified).
- Custom browser: `D:\ChromiumRTXCuda\src\out\RTXCuda\chrome.exe`.
- Node and Playwright from the CUDA WebShader project's `node_modules`.

```powershell
.\scripts\build-wasm.ps1
node scripts\build-browser.mjs
.\scripts\native.ps1 Build
.\scripts\native.ps1 Assets
.\scripts\native.ps1 Benchmark
node scripts\test-browser.mjs
```

`build-wasm.ps1 -Clang ...` and `native.ps1 -Engine ...` accept alternate installations. `WEBCUDA_ROOT`, `RTXCUDA_CHROME`, and `STANDARD_CHROME` override the browser test dependencies; `WEBCUDA_ROOT` is also used by the server and shader build script. The interactive launcher has local browser defaults in `scripts/launch.ps1`.

`Assets` creates the Nanite mesh, parameterized material and map inside this project. The scene is spawned by `ASampleGameMode`. It uses Unreal's basic cube as its starting mesh; no separate commercial game installation is needed. The Unreal installation itself is required for the native lane.

## What is shared

- `shared/Scene.h`: twelve boxes, dimensions in metres, material values, camera and light. Native Unreal reads this header directly.
- `shared/Scene.cpp`: exports the same data and non-indexed box triangles through actual Wasm functions. The browser copies them from Wasm linear memory. This module is a scene fixture, not a game-engine port.
- `web/fixture.cu`: identical camera, shading and analytic intersection code compiled to WGSL for WebGPU or native CUDA for ChromiumRTXCuda.
- `scripts/build-browser.mjs`: generates `web/generated/fixture-optix.cu` by replacing analytic intersection with OptiX triangle traversal. Materials and lighting calculations remain shared.
- GPU output stays on the GPU for normal display. Pixel readback occurs only in the explicit automated comparison tests.

The native renderer uses Unreal's materials, light transport, shadows and temporal rendering. The browser renderer uses simple direct lighting, hard shadows and a constant ambient term. Scene placement and camera match; image shading is intentionally not expected to match Unreal.

## Measurements and evidence

The page reports **completed-frame wall time**, not GPU timestamp duration or display FPS. Browser samples include dispatch, bridge synchronization when applicable, presentation submission and queue completion. Compilation and acceleration-structure construction are excluded. It warms 20 frames and records 60. Native results use game-frame wall intervals after 300 warmup frames, with 300 measured frames.

Do not divide native timing by browser timing to claim an Unreal speedup: these are different rendering workloads and timing boundaries. Even the browser timings are exploratory short samples, not a sustained performance study. Tiny scenes can be dominated by synchronization overhead.

- `artifacts/native/scene.png` and `report.json`: native capture, GPU and feature configuration.
- `artifacts/browser/report.json`: test results, raw timings, source hashes, custom browser executable hashes, CUDA WebShader revision and dirty-state flag.
- `artifacts/browser/{webgpu,cuda,optix,standard-edge,native-comparison}.png`: actual runtime screenshots.
- `artifacts/native-nanite/scene.png` and `artifacts/native-lumen/scene.png`: separate visualization captures, when generated below. Their timings are diagnostic and not benchmark results.

```powershell
.\scripts\native.ps1 Nanite # Cluster visualization
.\scripts\native.ps1 Lumen  # Lumen overview visualization
```

Automated checks cover real Wasm loading, nontrivial WebGPU output, light interaction, explicit denial of native permission, CUDA/WebGPU pixel agreement, native 64-bit atomic add and minimum, OptiX traversal agreement, resolution changes, renderer switching, native capture loading, and ordinary Edge without native GPU extensions. A very small tolerance around floating-point face/shadow boundaries is allowed and exact mismatch counts are retained in the report.

## Next engine milestone

The active engine port is documented in [engine/README.md](engine/README.md).
It now has a Wasm64 UBT target, platform adapters, a C++ WebGPU compute/resource
backend and a registered native `ShaderFormatWebCuda` module. The actual
`/Game/Sample` build and cook are attempted by `engine/try-demo.ps1` and
`engine/try-cook.ps1`. The cook reaches original global shader compilation, but
the full scene is not cooked or running in Wasm. Its next compiler work includes
complex constant layouts, texture/uniform-buffer bindings and raster stages;
the full Unreal RHI and presentation path also remain incomplete.

The earlier browser fixture described above remains separate from this work.
The engine port preserves original Lumen/Nanite shader algorithms and does not
use that fixture's lighting as an engine replacement. One original Lumen compute
pass has matching native and Wasm/WebGPU results through the new shader container.

Nanite needs its full shader and streaming pipeline; passing a 64-bit atomic probe only establishes one capability. Lumen needs its scene representation, tracing, caches and denoising; one compute pass does not establish working lighting. DLSS needs correct temporal inputs from the eventual renderer. All three full engine features remain pending in the browser.
