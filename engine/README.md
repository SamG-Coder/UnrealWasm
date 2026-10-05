# WebCuda engine integration

This is an incomplete Unreal 5.8.2 platform port. There is now a working UBT
Wasm64 target, an initial Core platform implementation, a C++ WebGPU resource and
compute backend, and limited shader compiler adapters. One original Lumen compute
pass has been validated through both WebGPU and the browser CUDA path.
**The Unreal game and full Lumen renderer do not run in Wasm yet.**

No replacement lighting implementation or visual demo is included here. Shader
algorithms come from Unreal's existing, preprocessed `LumenScreenProbeGather.usf`.
The original engine shader files are not edited. Generated backend inputs and
outputs are kept under ignored `engine/artifacts/` paths.

## Verified on this machine

| Component | Result |
|---|---|
| Custom UnrealBuildTool | Builds Epic's installed UBT source with the WebCuda platform extension; zero compiler warnings/errors in the final build. |
| `WebCudaBridge WebCuda Development` | UBT compiles and links a genuine Wasm64 module with Emscripten 6.0.11 and writes a platform build receipt. This target contains the dispatch bridge, not Unreal Core or the renderer. |
| `SetupAdaptiveProbeIndirectArgsCS` | Exported from the original UE-preprocessed shader; executed through Wasm64 command packets and ChromiumRTXCuda native CUDA. All 32 cases / 640 uint comparisons match the original HLSL on hardware Direct3D 11, including two untouched guard words per case. |
| `ScreenProbeIntegrateCS`, permutation 0 | Compiled from the original UE-preprocessed shader through Slang and NVCC to a 66,552-byte `sm_120` CUDA binary. **Compile-only: not executed, not browser-loadable, and no lighting parity claim.** |
| Original Win64 editor target | Compiled and linked the new WebCuda target-platform module. The existing native `/Game/Sample` subsequently ran through 630 benchmark frames and exited with code 0. This is not a full native engine source rebuild. |
| Browser startup | Added a single-canvas `GenericApplication`, window, cursor and queued input adapter. The shared Wasm64 browser host passes canvas, keyboard/character, mouse/wheel, focus-reset, frame scheduling and cleanup tests in Edge. Original `Launch.cpp` and `LaunchEngineLoop.cpp`, plus `WebCudaMain.cpp`, compile. Full Unreal startup is not linked or executed. |
| Native editor cooking discovery | An isolated host with rebuilt original `BuildSettings` recognizes `SF_WEBCUDA` at static enum 32. `ShaderFormatWebCuda` now loads and the actual `/Game/Sample` cook reaches original global shader compilation. The compute-only compiler/resource ABI cannot yet cook the full scene. |
| Engine shader compiler to Wasm | The native `IShaderFormat` probe emits the same 2,966-byte WCSH container exercised in Wasm/WebGPU: 32 original Lumen pass cases / 640 exact uint comparisons, plus 2,986 malformed-container/bounds rejections. This is one compute pass, not full lighting or scene startup. |
| Initial Core platform | GenericPlatform/Clang and selected existing POSIX implementations support Wasm64. Aligned allocation, logical virtual memory blocks, TLS, 64-bit atomics, monotonic time, UTF-16 strings and math execute in Edge. Initial runtime is single-threaded. |
| Browser files | WasmFS virtual directories are shared between page JavaScript and C++ POSIX IO. 4,096 bytes passed JS-to-C++; 2,048 bytes passed back after seek/truncate/rename. The engine's original Unix file implementation compiles for this target. Its full Unreal object graph is not linked in this harness; persistence is not enabled. |
| Chaos transform configuration | Original float and double vector-register constructors now compile with `-msimd128 -msse4.1`, and 64 forward/inverse transform cases pass 768 numeric comparisons in Edge. No Chaos algorithm/source edits. This is not a complete physics-solver validation. |
| C++ WebGPU backend | Wasm64 calls Emdawnwebgpu through `webgpu.h` in Edge and ChromiumRTXCuda. Buffer upload/copy/readback and three RGBA8 cases match 4,168 bytes. Another 54 cases per browser verify six texture formats, array layers and mip views against 73,728 exact storage/padding bytes. No native GPU bridge is involved. |
| Original Lumen pass through WebGPU | C++ creates the pipeline and dispatches Slang-generated WGSL for `SetupAdaptiveProbeIndirectArgsCS`. All 32 cases / 640 uint comparisons match original HLSL on hardware Direct3D, including guards. No replacement lighting shader. |
| Real engine-header compilation | UBT compiles the sample module/game mode, WebGPU module/device/RHI resource wrappers and platform support against real Unreal headers. `FRHIBuffer`/`FRHITexture` subclasses compile; their shared backend executes in the browser harness. They are not linked into a running Unreal renderer. |
| Oodle Data 2.9.16 | Recompiled all 73 supplied SDK C++ source files to Wasm64/SIMD128 without source edits. Kraken passed 27 cases in ordinary Edge: exact Windows-to-Wasm and Wasm-to-Windows decompression, guard bytes, and truncated-stream rejection. No native host bridge. |
| UHT / disabled ISPC | Fixed the source-root UHT configuration link and the toolchain's disabled-ISPC hooks. UHT generated 11,755 files and the full build reached C++ compilation. |
| Open-source dependency portability | ICU 68.2, FreeType 2.14.3 and AHEasing 1.3.2 compile and execute in Wasm64 under Node. Unicode conversion, invalid UTF-8 rejection, Roboto glyph rasterization and four easing functions passed. These versions are not yet integrated into Unreal. |
| ICU engine integration | ICU 64.1 now has matching generated headers, Wasm64 library, UE-compatible file callbacks and UBT module rules. All 11 original Unreal ICU integration source files compile. In Edge, actual ICU64 engine data passes resource lookup, number formatting, word breaking, collation, missing-data rejection and two balanced init/cleanup cycles. This is still a dependency harness, not an executed `FICUInternationalization` instance. |
| Slate dependencies | FreeType 2.14.1, HarfBuzz 2.4.0, libpng 1.6.44, zlib 1.3 and AHEasing 1.3.2 now build as Wasm64 archives and are selected by UBT. Eight original font, animation, PNG and compression consumer units compile. Edge tests pass glyph rasterization, Latin/Arabic shaping, PNG/compression round trips and easing checks. Full Slate UI and a rendered engine frame are not tested. |
| Full `UnrealBrowserSample WebCuda Development` | The first sample module and deeper `SampleGameMode.cpp` now compile. The reported virtual-memory/file/Chaos compile failures are resolved. Full Core services and dependency integration remain incomplete; the full engine has not been linked. No full-game binary is produced. |

The first shader prepares indirect screen-probe dispatch arguments; it does not
calculate visible GI. Testing it validates buffer mapping, constant layout,
Wasm pointer width, command submission, and actual GPU writes. It is not evidence
that Lumen lighting is working in the browser.

## Reproduce

From `D:\Test\UnrealBrowserSample` in PowerShell:

```powershell
# Attempt the actual existing game (diagnostic limit: 8 failing compile units):
.\engine\try-demo.ps1

# Build and execute the browser application host (not the full game):
python .\engine\Tests\build-browser-host.py
node .\engine\Tests\browser-host.mjs

# Compile essential units using the full game's generated UBT response files:
python .\engine\Tests\audit-demo-blockers.py

# Only needed if the local compiler SDK is missing:
.\engine\setup-compilers.ps1

# Builds custom UBT and the Wasm target, exports the captured shader,
# builds/runs the native HLSL reference, compares browser CUDA results,
# and compiles the larger texture-using pass offline.
.\engine\validate.ps1

# Also recapture shader inputs with the real native Unreal sample:
.\engine\validate.ps1 -CaptureShader

# Compile the actual platform/RHI objects and test C++ Wasm -> WebGPU in Edge.
# Requires the captured shader and native reference fixtures from validate.ps1.
.\engine\validate-webgpu.ps1

# Build and validate the actual Oodle SDK dependency:
python .\engine\Dependencies\build_oodle.py
.\engine\Tests\build-oodle-native.cmd
node .\engine\Tests\browser-oodle.mjs

# Validate the upstream open-source dependencies independently of Unreal:
python .\engine\Dependencies\validate_open_source.py

# Build the matching ICU dependency and validate the engine ABI/data path:
python .\engine\Dependencies\build_icu.py
python .\engine\Tests\build-icu-unreal.py
node .\engine\Tests\browser-icu.mjs
python .\engine\Tests\audit-icu.py

# Build matching Slate/font/image libraries and test their real UE consumers:
python .\engine\Dependencies\build_slate.py
python .\engine\Tests\build-slate-dependencies.py
node .\engine\Tests\browser-slate.mjs
python .\engine\Tests\audit-slate.py

# Focused first engine module compile; now succeeds (compile only, no game link):
.\engine\ubt.ps1 UnrealBrowserSample WebCuda Development `
  '-Project=D:\Test\UnrealBrowserSample\UnrealBrowserSample.uproject' `
  '-SingleFile=D:\Test\UnrealBrowserSample\Source\UnrealBrowserSample\UnrealBrowserSample.cpp' `
  -WaitMutex -NoUBA -NoBuildUHT -NoUBTMakefiles
```

Dependencies currently pinned or located by the scripts:

- UE 5.8.2 at `D:\UnrealEngine\UE_5.8`, including its bundled .NET 10 runtime.
- Emscripten 6.0.11 in `engine/tools/emsdk`, installed from the official SDK.
- Its Emdawnwebgpu port, pinned by that SDK to `v20260423.175430`.
- Slang `2026.13.1-1-g84792eb15` from `C:\VulkanSDK\1.4.357.0\Bin\slangc.exe`.
- CUDA 13.3 and Visual Studio 18 Community / MSVC 14.51.36231 for the offline CUDA test.
- ChromiumRTXCuda at `D:\ChromiumRTXCuda\src\out\RTXCuda\chrome.exe`.
- CUDA WebShader runtime and Playwright at `D:\cuda-webshader`.

The CUDA test runner grants native GPU permission only in an isolated test browser
profile and closes that browser after testing. The WebGPU runner uses ordinary
Edge and needs no custom native GPU permission. Neither creates a new game scene.

## Files and boundaries

- `Platforms/WebCuda/Source/Programs/UnrealBuildTool/`: platform registration,
  SDK validation, compile/archive/link actions, include dependencies, Wasm64
  output and target metadata. Browser packaging remains explicitly unsupported.
- `BuildTool/UnrealBuildTool.WebCuda.csproj`: original UBT source plus the
  extension, referencing the installed UBT's dependencies.
- `Platforms/WebCuda/Source/Runtime/Core/`: initial platform headers and actual
  memory/trace services. Unreal's `TCHAR` is UTF-16 while Emscripten's native
  `wchar_t` remains 32-bit; no incompatible `-fshort-wchar` libc ABI override.
  Threading is not enabled and browser child-process handles are explicitly invalid.
  Virtual memory blocks are aligned heap allocations with logical commit tracking;
  they do not support sparse address reservations, protected guard pages or OS
  decommit. `CanOverallocateVirtualMemory()` is false. WasmFS backs POSIX files.
  The Unix file source is included without edits, with one translation-unit-local
  glibc `__time_t` spelling mapped to musl `time_t`. Native mapped-file APIs return
  the engine's explicit unsupported result instead of copying whole assets.
- `prepare-platform-headers.ps1`: generates a narrowly adapted copy of the original
  GenericPlatformProcess header for its otherwise unsupported Wasm yield branch.
  Source hashes are recorded; the installed header is not edited.
  The generated header keeps its canonical `GenericPlatform/GenericPlatformProcess.h`
  path and precedes engine includes, so all include chains use one definition.
- `../Source/WebGPURHI/`: actual C++ WebGPU device, buffers, six exact texture formats with mip/array views,
  compute pipelines/dispatch and asynchronous readback, plus initial Unreal RHI
  resource wrappers. Unsupported resource descriptions are rejected. This is
  **not yet a registered `FDynamicRHI` factory**, complete command context or
  presentation backend. The module's device is retained for callback lifetime.
- `ShaderCompiler/export_wgsl.py`: verifies the original captured Lumen shader,
  lowers typed uint buffers to structured uint buffers for this restricted pass,
  invokes Slang and generates constant-layout mapping from the emitted WGSL.
  The original algorithm and shader source are preserved; generated WGSL is not
  hand-edited. This is not the engine shader-platform/cooking integration.
- `prepare-source-overlay.ps1`: a separate source-build root. Custom platform
  registration is visible only to this root; the installed UBT and its platform
  registry remain unchanged. Large engine source/content folders are junctions.
  This overlay is not a complete, downloaded Epic source checkout.
- `../Source/WebCudaBridge/`: 48-byte versioned command packet, 64-bit Wasm
  addresses, bounded constant/resource staging. It has no engine dependency and
  **is not an implementation of `FDynamicRHI`**.
- `Runtime/dispatch.js`: packet validation, resource handle lookup, constant
  snapshots, artifact SHA-256 checking and native CUDA submission.
- `ShaderCompiler/export_shader.py`: verified uint-buffer compute profile.
  Typed uint UAVs map to structured uint arrays; reflected Unreal constant
  offsets are repacked into the generated CUDA struct; generated shader helper
  calls receive per-dispatch parameters rather than shared mutable globals.
  Texture/raster/ray-tracing resources are rejected by this profile.
- `ShaderCompiler/compile_texture_shader.py`: separate, offline compiler
  experiment for the larger shader. It normalizes complete-type forwards and
  zero-offset texture loads, and supplies an RGB-to-RGBA32F storage adapter.
  Its resource formats and CUDA handles still need RHI integration and runtime
  parity validation. It is deliberately rejected by the browser packet profile.
- `Dependencies/build_oodle.py`: extracts the installed Oodle source ZIP into
  ignored artifacts and compiles its existing Wasm platform implementation.
  Produces `liboo2corewasm64.a` for UBT and `oodle.wasm` for browser validation.
- `Platforms/WebCuda/Source/Runtime/OodleDataCompression/`: extends the engine's
  existing module rules to select that Wasm archive. Original engine rules and
  Oodle codec source remain unchanged.
- `Tests/oodle-wrapper.cpp`: test C ABI plus Oodle's allocator hook. The SDK's
  default Wasm allocator intentionally returns null, so the harness supplies
  Emscripten `posix_memalign`/`free`. Unreal already installs its own allocators
  in `Core/Private/Compression/OodleDataCompression.cpp`; the actual Unreal
  allocator path still requires the Core platform port.

## Evidence

- `artifacts/webgpu/validation.json`: passing ordinary Edge C++ Wasm64 resource,
  original-Lumen-pass and real platform-service execution, with hashes and
  explicit `fullUnrealRenderer: false` / `fullLighting: false` boundaries.
- `artifacts/webgpu-engine-compilation.json`: real UBT object paths,
  hashes and compiler logs. Single-file UBT output can mention an output binary
  even without linking; this report checks object existence, not a game binary.
- `artifacts/sample-game-mode-compile.log`: historical game-mode platform/Chaos
  errors; superseded by `artifacts/webgpu-SampleGameMode.cpp.log`.
- `artifacts/platform-services/validation.json`: memory semantics, JavaScript/C++
  WasmFS interoperability, original Chaos transforms and allocation timings.
- `artifacts/platform-services/services.wat`: disassembled actual Wasm; the build
  requires both `f32x4.mul` and `f64x2.mul`, rather than inferring SIMD from flags.
- `artifacts/platform-headers/manifest.json`: provenance for the generated
  GenericPlatformProcess adaptation.
- `artifacts/validation-build.log`: custom UBT and Wasm build plus validation.
- `artifacts/lumen-validation.json`: browser CUDA results and source/artifact
  hashes, exact native comparisons, rejection tests, and Wasm64 verification.
- `artifacts/exported/SetupAdaptiveProbeIndirectArgsCS.json`: original shader
  hash, Slang version, reflection offsets, CUDA hash and resource ABI.
- `artifacts/texture-compile/ScreenProbeIntegrateCS.json`: offline compile
  provenance and an explicit `executed: false` field.
- `artifacts/full-game-build.log`: historical UHT/ISPC failure before the fixes.
- `artifacts/dependency-audit-build-tool.log`: custom UBT rebuild passed without
  warnings or errors after the toolchain changes.
- `artifacts/dependency-audit-game.log`: UHT success and subsequent Core-header
  failures. Stopped after repeated identical errors instead of running all
  10,960 actions.
- `artifacts/dependency-audit-single-file.log`: historical focused compilation
  confirming the formerly missing WebCuda platform header (exit code 6), now
  superseded by the passing first-module WebGPU compilation report.
- `artifacts/open-source-ports/validation.json`: actual Wasm64 execution results
  for ICU, FreeType and AHEasing, with versions and explicit integration limits.
- `artifacts/oodle/build.json`: source archive hash, compiler flags, source count
  and Wasm/static-library hashes.
- `artifacts/oodle/validation.json`: ordinary Edge cross-architecture codec
  results, covering zero, repeated and deterministic random data from 1 byte to
  1 MiB, including either side of the 256 KiB block boundary.
- `artifacts/oodle/windows-library-attempt.log`: a direct attempt to link the
  original Win64 archive fails because its objects are neither Wasm nor LLVM
  bitcode. The successful result uses source recompilation, not DLL conversion.
- `artifacts/native-regression-build.log`: original native target check.

## Actual demo attempt (2026-10-05)

The existing `/Game/Sample` map, `FixtureCube` and `FixtureMaterial` assets were
used. No replacement scene or renderer was created. The native editor game runtime
loaded the scene, ran through frame 630 and exited successfully. The fresh capture
and report are `artifacts/demo-attempt/native/scene.png` and `report.json`: 12 active
Nanite proxies, DX12 SM6, Lumen/hardware tracing enabled. This was a native run of
the existing game DLL, not a new full engine build or a browser render.

The full Wasm game target initially generated 10,957 actions and stopped after
8 failing units in the default ACL animation plugin (unsupported floating-point
exception control intrinsics). The WebCuda sample target now disables default
engine plugins, which this static-mesh scene does not use. Native target settings,
scene assets and Lumen settings are unchanged. The second graph contained 6,627
actions and completed 15 compile actions without reported errors before the bulk
build was deliberately stopped after separate essential-unit checks exposed
startup/ICU blockers. It was not a completed build; `buildExitCode: -1` records
that stop. No `UnrealBrowserSample.wasm` was produced.

Using the full game's actual response files, the audit additionally compiled
`ApplicationCore.cpp`, `Launch.cpp`, `DynamicRHI.cpp` and `UnrealMemory.cpp` into
isolated Wasm objects. `LaunchEngineLoop.cpp` initially failed at the splash header.
The ApplicationCore extension selects Unreal's existing optional splash
implementation. The browser application, window/input and event-loop integration
added subsequently resolves the missing application header; the original launch
loop and browser entry point now compile. Full engine startup remains unexecuted.

`ICUInternationalization.cpp` originally failed at `u_setDataFileFunctions`: Unreal expects
Epic's ICU extension, declared in the installed 64.1 headers. The unknown-platform
module rules request absent 53.1 headers, and the Emscripten headers found instead
do not supply that extension. The standalone upstream ICU test does not resolve
this engine integration. The subsequent ICU 64.1 build described below supplies
matching headers, a Wasm library with the required data access integration, and
uses the installed engine's matching locale data. This compile failure is resolved.

The first native-editor cook of `/Game/Sample` for `WebCuda` exited with code 1:
`Invalid target platform specified (WebCuda)`. UBT platform registration is separate
from the editor's target-platform/shader-format cooking modules. The target-platform
module now loads; the isolated host's cook passes shader-platform registration and reaches the registered compiler. Its initial resource/stage ABI remains insufficient for a complete scene cook.
Complete shader cooking, RHI startup and packaging remain absent. No browser scene
launch is attempted without a game binary and cooked content.

Evidence: `artifacts/demo-attempt/{initial-build.log,build.log,result.json}`, the
`essential-units/results.json` audit, `startup-recheck.log`, `cook-result.json` and
`cook.log`. `Tests/audit-demo-blockers.py` reproduces the essential-unit compilation
from generated UBT response files. These targeted checks do not prove the rest of
the 6,627-action build succeeds. `try-demo.ps1` restores its temporary diagnostic
build configuration after success, failure or an interrupted child build.

## Work still required for an actual Lumen export

1. Port Unreal Core/platform services and required dependencies to Wasm. The
   Oodle Wasm archive is now available and its Kraken API validated separately.
   The initial Core platform now compiles the first sample module, with selected
   services validated in Wasm. The deeper game-mode compile now passes, including
   the previously failing Chaos constructors. Worker/threading support, persistent
   or streamed asset storage and additional platform interfaces remain. Dependency module
   rules, matching library/header versions and locale data also remain. Passing the
   standalone SDK tests does not establish that Unreal Core works in Wasm.
2. Extend the registered engine shader compiler beyond its initial compute/root
   constant/uint-buffer ABI. Native editor in-process compilation now works;
   ShaderCompileWorker deployment/ABI and complete cooking/packaging remain.
3. Complete the actual Unreal RHI: register an `FDynamicRHI` factory, add command
   contexts, presentation, textures/samplers/formats, buffer views,
   render targets, raster passes, synchronization, indirect dispatch, draw
   commands and render-graph resource transitions. The compiled resource wrappers
   and browser-tested C++ buffer/texture/compute backend are an initial subset.
4. Bind and execute the texture-using Lumen passes with actual Unreal G-buffer,
   depth, surface-cache and probe resources. Validate against native captures
   before enabling capability flags. Hardware tracing adds acceleration
   structures and the full shader/ray interface.
5. Cook/export the existing sample and compare native and browser images/frame
   times. Nanite and DLSS integration have not been implemented by this work.

The current authenticated GitHub account received HTTP 404 for EpicGames/UnrealEngine.
That prevents fetching the private full engine repository through this account;
obtaining it would not by itself implement the missing platform/rendering port.
Oodle itself does not need that checkout: its source is already supplied in
`Engine/Source/Runtime/OodleDataCompression/Sdks/2.9.16/src/` in this installation.
The earlier Oodle blocker assessment missed that archive.

## Dependency failure audit (2026-10-05)

| Item | Root cause | Resolution / remaining work |
|---|---|---|
| UHT delegate parsing | Our source-root overlay omitted `Engine/Programs/UnrealHeaderTool/Config`. This contains `DelegateParameterCountStrings`, including `_OneParam` and `_TwoParams`. | Fixed by linking that installed configuration directory. UHT remains a native build-time tool; no Wasm replacement is needed. |
| ISPC rejection | UBT calls ISPC hooks for modules containing `.ispc` inputs even when `bCompileISPC=false`. Our hooks threw unconditionally. | Fixed to match Epic's existing early return when disabled. Existing C++ paths can now compile; no blanket functional/performance parity claim. An enabled ISPC request still fails explicitly. The installed ISPC 1.24 binary does not list a Wasm target. |
| ICU missing `icu4c-53_1/include` and `u_setDataFileFunctions` | Unknown platforms fell back to 53.1; Emscripten's upstream 68.2 omitted Unreal's callback API. | Resolved with ICU64 module rules and a 435-unit ICU 64.1 Wasm build using matching header/symbol versions and a file callback backend. Engine ICU units compile; real locale-data services pass in Edge. Full engine execution remains unverified. |
| FreeType missing `FreeType2-2.6/Include` | Unknown platforms fall back to 2.6; the installation contains 2.14.1. | Resolved for WebCuda with matching 2.14.1 source/headers, PNG and zlib libraries, native module extensions and browser tests. HarfBuzz 2.4.0 shaping is enabled and linked to ICU64 rather than silently disabled by the unknown-platform fallback. |
| Missing AHEasing 1.3.2 | The installed distribution has its metadata but not the source directory used by Slate and RigVM. | Pinned upstream 1.3.2 source builds unmodified. Slate now gets its include/archive dependency via `WebCudaAHEasing`; its original consumer compiles. `AHFloat` is explicitly double on both sides, matching upstream Wasm64 LP64 behavior. The optional RigVM plugin is not part of this sample target and has not received this integration. |
| Messaging include warnings | Legacy relative include paths are resolved under the project, rather than the engine module. | Use module-relative absolute paths in platform module rules. No replacement messaging library is needed. |
| Missing WebCuda Core headers | A new platform name needs matching platform interfaces, even though the generic engine source is present. | Initial headers and memory/time/TLS/atomic services are implemented and browser-tested. Logical virtual memory and Unix-to-WasmFS file interfaces now compile; the sample game mode passes. Other platform services and full engine linking remain. |
| Chaos scalar/vector constructor mismatch | Chaos constructs vector-register transforms, but the original WebCuda platform disabled vector intrinsics. | Select original Unreal SSE4.1 math through Emscripten's SIMD128 compatibility layer. Disable AVX/FMA and unsupported vector control registers. The original Chaos float/double transforms compile and run. |

Upstream references: [Emscripten ICU port](https://github.com/emscripten-core/emscripten/blob/main/tools/ports/icu.py),
[Emscripten FreeType port](https://github.com/emscripten-core/emscripten/blob/main/tools/ports/freetype.py),
[AHEasing](https://github.com/warrenm/AHEasing).

## Memory, file and Chaos platform decisions

Keep the `WebCuda` platform identity with Emscripten/musl POSIX services. Do not
select Linux or Windows wholesale: their virtual-memory, process and CPU
assumptions do not describe a browser.

`FPlatformVirtualMemoryBlock` uses one aligned linear-memory allocation and one
byte of commit metadata per 64 KiB page. Commit clears only newly committed runs;
repeated commit preserves existing bytes. Decommit marks pages logically unused,
and recommit zeroes them. Free returns the whole block to the Wasm allocator,
but Wasm's linear memory cannot shrink. Access outside a logical commit is not
hardware-protected. Keep the existing ANSI/POSIX allocator and budget reservations
explicitly instead of enabling a desktop allocator that reserves vast ranges.

The local benchmark compares 64 allocations of 16 MiB using only the first 64 KiB:
the adapter commits that page, while Emscripten's anonymous mmap clears the whole
allocation. The first run's median was 0.030 ms versus 13.640 ms across seven warm
samples. This demonstrates avoided initialization in this narrow workload, not an
engine-wide speedup, and very small timings are sensitive to timer resolution.
Current measurements are recorded in the validation JSON.

WasmFS is selected with `-sWASMFS=1 -sFORCE_FILESYSTEM=1` and exported `FS` access.
HTML/JavaScript can create `/Content` and populate it before engine startup; C++
uses ordinary open/read/write/seek/stat/directory operations on the same tree.
The current memory backend is transient. Persistent `/Saved` storage should use
WasmFS OPFS after worker/async runtime integration; large cooked assets should
use range-backed streaming/cache rather than preloading the entire game into RAM.
The exact OPFS/FetchFS backend scheduling requirements must be respected; they
are not enabled in this initial main-thread target.

Chaos retains its original math code. `-msimd128 -msse4.1` maps its existing
128-bit intrinsics onto Wasm; double4 uses two double2 vectors. This does not
make the target an x86 CPU. AVX/FMA remain disabled; Wasm does not support changing
the native floating-point control register/flush-to-zero mode. Solver stability,
contacts, constraints and multithreaded performance still require real workloads.
The small math harness disables Unreal logging/assert diagnostics only for its
standalone link; the actual Development game-mode compilation retains them.

References: [Emscripten SIMD](https://emscripten.org/docs/porting/simd.html),
[WasmFS and JavaScript filesystem access](https://emscripten.org/docs/api_reference/Filesystem-API.html),
[Emscripten mmap implementation](https://github.com/emscripten-core/emscripten/blob/main/system/lib/libc/emscripten_mmap.c),
[WasmFS backend APIs](https://github.com/emscripten-core/emscripten/blob/main/system/include/emscripten/wasmfs.h).

## Browser startup and native cooking integration (2026-10-05)

`ApplicationCore_WebCuda` supplies an actual canvas application using Emscripten
HTML5 events. Browser callbacks queue events; Unreal consumes them through its
existing generic message handler. Losing focus resets held input, and mouse-up
is captured on window so dragging outside the canvas releases the button.
`InputCore_WebCuda` maps browser key codes to Unreal's named keys, including
left/right modifier distinctions. The original `InputCoreTypes.cpp` compiles
against this mapping. Actual in-game input still needs a linked engine run.
The initial host uses logical CSS pixels at 1:1 canvas resolution, one window,
and a main-thread frame loop. Pointer lock, fullscreen, IME, touch, gamepads,
automatic page-layout resize and worker rendering remain unimplemented.

`WebCudaMain.cpp` calls the original `GuardedMain` with Unreal's embedded flag,
then drives the original `EngineTick` through requestAnimationFrame and calls
`EngineExit` on shutdown. This avoids entering Unreal's blocking native tick
loop. Game linking now exports `_main` and `FS,callMain`, with automatic entry
disabled so a future package loader can mount the cooked files first. The small
standalone bridge still uses `--no-entry`. There is no runnable game loader or
complete engine binary yet; compile-only entry verification cannot establish
that all engine initialization operations are browser-safe or asynchronous.

The native `WebCudaTargetPlatform` module implements the installed UE5.8
settings/controls interfaces. Its merged platform is owned by Unreal's base
module, while settings/controls are owned by the derived module. Native shutdown
was tested after correcting a duplicate ownership bug. Ordinary native startup
does not expose an invalid shader format to the RHI. A WebCuda cook explicitly
fails if `SF_WEBCUDA`, its metadata, or its compiler is missing; it does not select
a native shader format or emit shaderless packages.

The shader-platform name mapping lives in Unreal's generated
`BuildSettings` static shader table, consumed by `FStaticShaderPlatformNames`.
UBT provides `AddPlatformStaticShaders` for source-built platform integrations.
The installed editor does not contain a WebCuda entry; a project target-platform
DLL alone cannot add that compiled table entry. The isolated host now rebuilds
the original BuildSettings module with the table entry through that UBT hook.
A real ShaderCompileWorker backend/resource ABI remains needed.
The previously captured single Lumen compute pass is not a general shader backend.

Current evidence is in `artifacts/browser-host/validation.json`,
`artifacts/startup-*.log`, `artifacts/cooker-module-build.log`,
`artifacts/cooker-webcuda-stdout.log`, `artifacts/startup-native/`, and
`artifacts/demo-attempt/essential-units/results.json`. The latest essential-unit
audit passes all 10 units after the ICU integration below. Earlier bulk attempts
were stopped intentionally at independently confirmed startup/ICU blockers; those
stops were not linker failures or completed game builds. The actual game build
has now been restarted with the ICU archive in its linker response file.

## Matching ICU 64.1 integration

`Dependencies/build_icu.py` builds 435 source files from the official ICU 64.1
release archive, verified by a pinned SHA-256. It adds the same callback
declarations and versioned symbol name found in Unreal's installed headers and
replaces the OS-specific file-mapping implementation with
`Dependencies/icu-file-adapter.cpp`. Internationalization algorithms are unchanged.
Generated sources and headers live under `artifacts/icu64`; installed Unreal files
are not edited. This is a platform adapter implementation, not Epic's original
unavailable ICU patch. The upstream license remains with the extracted source.

The adapter captures each mapping's owner and close callback, routes file opens
through Unreal's callback, rejects callback replacement while mappings remain,
and closes each buffer through its matching owner. No native mmap or fallback
outside Unreal's file manager is used. The callback ABI is ICU64's: lengths are
unknown, as in Unreal's existing ICU64 interface; it does not claim ICU78's newer
length-validation contract.

`ICU_WebCuda.Build.cs` selects `WITH_ICU_V64=1`, matching headers and
`libicu64-wasm64.a`. `ubt.ps1` supplies its artifact root. The full-game linker
response includes that archive. The stock Emscripten ICU68 port is not linked
into the game. WasmFS hosts the loose `icudt64l` files; the test embeds the engine's
English subset and checks initialization, POSIX invariant resources, formatting,
word boundaries, collation, missing data, callback lifetime and allocator cleanup.
Production cooked-data staging still belongs to the unfinished packaging layer.

Evidence: `artifacts/icu64/build.json`, `validation.json`, `engine-compile.log`,
`engine-units/results.json`. Eleven original UE ICU translation units compile;
the engine's `FICUInternationalization` object itself has not been linked/executed.
The standalone browser test uses the identical callback signatures and exercises
the actual library/data backend. Passing it does not establish full engine boot.

## Slate font/image dependency integration

`Dependencies/build_slate.py` builds source archives with pinned SHA-256 hashes
for FreeType 2.14.1, HarfBuzz 2.4.0, libpng 1.6.44 and zlib 1.3, matching the
installed engine's selected dependency versions. AHEasing 1.3.2 files are pinned
individually to their upstream commit and hashes. These are upstream libraries;
Epic's native-only optimization patches are not replicated, and native pixel or
performance parity is not claimed. Algorithms and original engine consumers are
unchanged. Source licenses are retained in the extracted artifact trees.

FreeType builds 42 source units with PNG and system-zlib support; zlib includes
the classic minizip implementation. HarfBuzz builds 46 units with its OpenType
and fallback shapers, FreeType integration, ICU64 Unicode functions and pthread
mutex interface. `HAVE_ROUND` selects the actual system math function, avoiding an
old fallback macro colliding with modern C++ headers. The upstream diagnostic
configuration switch disables its blanket warning-to-error pragmas on modern
Clang; warnings remain visible in the logs. The typed-pointer finalizer paths
are exercised in the browser test. This is still the single-threaded runtime;
these flags do not establish worker support.

The platform module extensions select the new include roots and archives.
`WITH_FREETYPE=1`, `WITH_HARFBUZZ=1` and `WITH_HARFBUZZ_V24=1` are verified in
the generated SlateCore definitions. The full game link response contains all
five archives. Slate's AHEasing consumer now resolves through a real external
module, with an explicit double ABI on both sides.

Browser evidence (`artifacts/slate-dependencies/validation.json`) covers 12
glyph bitmaps at three sizes, ligature shaping of `office` into five glyphs,
right-to-left Arabic shaping into three glyphs using the engine's fonts,
FreeType/HarfBuzz destruction callbacks, a 24-byte RGBA PNG round trip and invalid
signature rejection, an 8192-byte zlib round trip, and 130 easing comparisons.
The 8-unit `Tests/audit-slate.py` report verifies real Unreal consumers with the
actual UBT arguments. Neither test establishes full Slate UI rendering.

The prior bulk build was deliberately stopped after 101 scheduled actions to
regenerate its graph with these dependencies. Its log/result are preserved as
`artifacts/slate-dependencies/prior-build.*`. The restarted build retained that
incremental work and had 6534 remaining actions. `try-demo.ps1` now writes its
running status before invoking UBT and accepts a parallelism parameter (default
4); always verify the process/session rather than relying on the status file alone.

## Shader-platform registration in the actual cooker

`WebCudaPlatform.ModifyModuleRulesForOtherPlatform` calls UE's
`AddPlatformStaticShaders` once for both the Wasm target and native tools. It
generates the `WEBCUDA`/`WebCuda` entry at reserved static index 32 and the RHI
constant header. No editor preview slot or runtime table mutation is used.
The installed distribution currently has no static entries; the native ABI test
rejects a rebuild that drops an existing entry or duplicates this slot.

`prepare-cook-host.ps1` creates a separate host under `artifacts/cook-host`.
Source and content are shared inputs. Plugin intermediate directories are local.
Native executable/DLL inputs are hard linked, while mutable manifests and all
BuildSettings files are independent copies. **Do not run an unrestricted native
build in this host**: only the explicitly listed BuildSettings compile/resource/
link actions are allowed. Early host generation used a broad Plugins junction
and wrote generated plugin build metadata into installed Intermediate folders;
the current layout prevents that. Installed source and native binaries were not
edited. The ongoing Wasm source overlay retains its older plugin layout.

`build-cook-host.ps1` obtains actual UBT arguments with `-SkipBuild`, compiles the
original engine module and generated table with MSVC, and validates it against
the installed DLL. All 24 exported names match. Twenty-two no-argument metadata
functions were called in both DLLs: version, changelists, branch, allocator
reservation and ABI settings match; build timestamps and debug source paths
are expected to differ. This is a narrowly rebuilt host module, not a complete
native engine source rebuild. ShaderCompileWorker's BuildSettings module has
not yet been rebuilt.

Both native host and Wasm UBT platform now enable automatic WebCuda discovery.
The host receives the platform SDK JSON and its custom UBT's native dependency
directory on PATH; `try-cook.ps1` supplies these launch requirements plus
`WEBCUDA_EMSDK`. The old custom-module hook was removed: it used a module name
where UE 5.8 expects a platform name and skipped settings-module discovery.
`WebCudaTargetPlatformSettings` now participates in normal enumeration, allowing
RenderUtils to read the existing velocity-depth setting required by Lumen.
Both roots use the same shader enum and metadata. The `SP_` prefix in
`[ShaderPlatform SP_WEBCUDA]` is required by Unreal's static-extension parser.

The SM6, Lumen and Nanite metadata select the requested original shader
permutations. They do **not** establish working runtime support. In the actual
`/Game/Sample` cook, the log reports `enum=32, format=SF_WEBCUDA, Lumen=1,
Nanite=1` and reaches original global shader compilation. The older missing-format
failure is preserved under `artifacts/cook-host`; current cook evidence is under
`artifacts/shader-format`. No shaderless package, Windows shader substitution or
scene render is reported.

Host generation also exposed a UE5.8 FileHashCache race. The custom UBT compiles
a generated source copy that snapshots ConcurrentDictionary entries atomically
and writes that snapshot's count. `prepare-ubt-overrides.ps1` verifies both exact
patch sites; installed UBT source is unchanged. Repeated argument generation
passes with the fix. Windows SDK RC is invoked with arguments expanded using
Windows argv rules because its response reader retained path quotes locally.

## Further failures from building the actual scene

The next bulk attempt reached action 273/6534 and failed with four distinct
platform gaps. Its original log/result are preserved under
`artifacts/scene-platform-fixes/prior-build.*`.

- AssetRegistry requires `KernelAddressBit`. WebCuda reserves bit 63 for its
  existing cooked-value representation; the toolchain explicitly pins the
  existing Emscripten 2 GiB heap growth ceiling below that bit.
- AtomicQueue now uses a compiler fence for its Wasm spin hint in a generated
  copy of the supplied header. Queue algorithms are unchanged. This does not
  yield to JavaScript or implement multithreaded execution.
- The platform defines the same four signed 32-bit rectangle coordinates used
  by Unreal's Unix/Android cursor ABI, allowing the generic software cursor to
  compile without modifying its implementation.
- The HTTP platform header exposes Unreal's original URL/header utilities. Its
  initially missing request constructor is now implemented by the asynchronous
  Fetch transport described below. Local HTTP-server support is not advertised.

`Tests/audit-scene-platform.py` recompiles five original engine files using the
regenerated game's UBT response files. AssetDataMap, GenericPlatformSoftwareCursor,
AnalyticsProviderETEventCache, IAnalyticsProviderET and GenericPlatformHttp all
pass. This proves those compilation failures are resolved, not HTTP transport,
full AssetRegistry execution or scene rendering. The resumed game graph now
also includes the shader-platform table entry. Check the current process and
`artifacts/demo-attempt/result.json` for the live full-build state.
The common platform-header edits invalidate dependent objects, so the resumed
graph schedules 6639 actions; it is not 273 actions nearer to completion.

## Browser HTTP transport

`WebCudaHttpTransport.cpp` uses browser Fetch with AbortController. C++ owns job
handles and consumes results on the game thread. Input bytes are copied before
returning to Unreal; decoded response chunks are bounded to 64 MiB by the engine
adapter and copied into current Wasm memory when consumed. Job IDs are not reused,
and cancellation/forgetting prevents late promises from reviving completed jobs.
The backend explicitly declares its Emscripten string-helper link dependencies.

`WebCudaPlatformHttp.cpp` supplies concrete Unreal request/response objects and
a platform HTTP manager. It preserves original URL parsing, default headers,
configured domain filtering, binary/UTF-8 payloads, status and failure reasons,
response metadata, effective redirect URLs, total/activity timeouts and download
progress. HTTP error status codes such as 404 are completed responses, distinct
from network failure. A completed request is removed outside the manager's
active-request iteration, with shared ownership retained through callbacks.
Completion callbacks can reuse the request; metadata callbacks cannot start a
replacement operation partway through dispatch.

This backend is explicitly asynchronous and single-threaded. Streamed uploads,
HTTP-thread callbacks and synchronous waiting are unsupported, and return failure
rather than blocking the browser. Receive archives are filled at completion,
not incrementally; response bodies remain buffered up to the cap. Browser CORS,
forbidden-header and credential rules apply. Browser scheduling determines
priority/concurrency; upload-progress timing is not measured. Synchronous manager
flush cancels outstanding operations, then drains deferred completion callbacks.
`WebCudaMain.cpp` passes `-nothreading` so services that directly consult
`FGenericPlatformProcess` use Unreal's existing single-thread fallback consistently
with this platform's `SupportsMultithreading()==false`.

`Tests/build-browser-http.py` builds the identical transport implementation for
the browser test. `Tests/browser-http.mjs` passes in Edge 154: exact 8192-byte
binary POST with custom headers; 404 response semantics; redirect and UTF-8;
total and activity timeout; cancellation and late-completion cleanup; incremental
download across Wasm memory growth; response-size rejection; dropped connection;
invalid URL/header; HEAD; 12 concurrent requests and 20 abandoned jobs.
All transport jobs are released and no uncaught page errors occur.

`Tests/audit-http.py` compiles the adapter, transport and launch entry using each
file's exact generated game response file. The game link response includes both
new HTTP objects. These are distinct evidence boundaries: the transport executed
in the browser; the Unreal request/manager objects compiled but have not executed
inside the complete Wasm engine. Reports are in `artifacts/browser-http/`.

The bulk build was deliberately stopped to add the new translation units to
UBT's graph. Its prior log/result are preserved in that artifact directory. The
restart retains incremental work, with 6437 actions remaining instead of 6639.

## Registered compute shader format and runtime container

`Source/ShaderFormatWebCuda` is a native editor module implementing Unreal's
`IShaderFormat` through `FBaseShaderFormat`. It uses Unreal preprocessing,
uniform-buffer cleanup and dead-code removal, then invokes the existing Slang
backend. Original shader algorithms are not replaced. Source hashes are checked
in the reference test. Current support is deliberately bounded to compute,
scalar/vector root or legacy global constants and structured uint/uint2/uint4 buffers; the existing typed
RWBuffer<uint> lowering retains uint load/store semantics. Other resource layouts
and shader stages fail compilation. Unused resource declarations are not rejected
before reflection. Raster, textures, samplers, uniform-buffer objects, matrices,
wave/atomic capabilities and the complete resource-table ABI remain work.

The compiler produces WGSL, reflection and a versioned WCSH binary containing
workgroup dimensions, resource bindings and HLSL-to-WGSL constant-field offsets.
The native module validates that container and fills Unreal's parameter map and
shader-code output. Its cache version includes the compiler script and Slang
binaries. The shared C++ `WebCudaShaderCode.h` reader validates lengths, versions,
binding uniqueness, mapping bounds/overlap and entry identifiers before creating
a WebGPU compute pipeline. This is not yet an `FRHIComputeShader` implementation
or the full DynamicRHI. Unreal optional shader-code trailers are outside WCSH.

`Tests/build-webgpu.ps1` now exercises the container reader rather than embedding
a separate hard-coded shader layout. `Tests/browser-webgpu.mjs` passes 32 original
Lumen cases / 640 exact values against original HLSL on hardware Direct3D, plus
2,986 malformed-container/constant-buffer checks. `Tests/probe-shader-format.ps1`
loads the real native module in the isolated editor and verifies byte-for-byte
identity with that browser-tested container. This probe consumes previously
captured UE-preprocessed source. `Tests/probe-original-shader.ps1` additionally
starts from registered original Unreal shader types, invokes their compilation
environment and full UE preprocessing, runs UE's original root-parameter parser,
constructs the original shader object and checks binding completeness. The original
Lumen indirect-argument shader produces exactly the browser-tested container.
Unreal supplies 124 bytes for that shader's root parameters, omitting final cbuffer
padding; the runtime accepts that upload and checks every copied field's bounds.
Evidence is `artifacts/shader-format/root-parameter-validation.json` and
`artifacts/webgpu/validation.json`.

Original `FRDGMemcpyCS` permutations 1, 5 and 9 now compile and bind in Unreal.
Browser execution checks the scalar root/loose-global paths in 42 cases / 10,752
word comparisons and uint2/uint4 structured buffers in another 42 cases / 32,256
word comparisons. The cases cover empty/partial workgroups, offsets and untouched
guard values. uint3/struct/atomic buffer layouts are still rejected; they need
separate stride/layout handling. No replacement compute or lighting shaders were
authored for these checks.

Reproduce the current native-to-browser parameter/resource checks after building
the native editor module and isolated cook host:

```powershell
./engine/Tests/probe-original-shader.ps1
./engine/Tests/probe-original-shader.ps1 -ShaderType FRDGMemcpyCS -Permutation 1 -Name original-memcpy -LegacyOutputName legacy-native
./engine/Tests/probe-original-shader.ps1 -ShaderType FRDGMemcpyCS -Permutation 5 -Name original-memcpy-uint2
./engine/Tests/probe-original-shader.ps1 -ShaderType FRDGMemcpyCS -Permutation 9 -Name original-memcpy-uint4
./engine/Tests/build-webgpu.ps1
node engine/Tests/browser-webgpu.mjs
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/Tests/validate-root-parameters.py
```

The `-LegacyOutputName` step also retains `legacy-native.wcs.json` from a
captured-source commandlet run on the same original preprocessing output.

Run the actual scene cook with `./engine/try-cook.ps1`. It uses `-noshaderworker`
explicitly because the editor module is currently deployed for in-process
compilation only. It no longer stops at a missing shader-format module or the
Lumen velocity-depth ensure. Compilation diagnostics are retained per job under
`artifacts/shader-format/jobs`, with the full cook log and terminal/live status
beside that directory. Reaching this stage is not evidence of a cooked map or a
running Wasm game.

The resumed full scene C++ build also exposed four invalid dependent-template
calls in the bundled AtomicQueue header. The generated compatibility copy adds
empty `<>` argument lists, preserving deduction and the original algorithms.
Both original ARLoader.cpp and AssetRegistryHelpers.cpp now compile with the
actual UBT Wasm response files (`Tests/audit-atomic-queue.py`). Header preparation
now avoids rewriting unchanged headers so repeat preparation does not invalidate
all dependent objects. Check the live `artifacts/demo-attempt/result.json` and
process before restarting a build. Native and Wasm UBT runs share the project's
build-rules DLL; new module-rule changes must be built serially to avoid its lock.

The subsequent actual build exposed a missing audio settings-section accessor
and missing semaphore platform header in AudioLink and BuildPatchServices.
`FWebCudaPlatformProperties::GetRuntimeSettingsClassName()` now identifies the
WebCuda INI section read by AudioMixer. All three original failing translation
units pass with their real UBT Wasm flags (`Tests/audit-build-services.py`).

The default non-pthread Emscripten library has no-op semaphore wait/release calls
and no `sem_timedwait` implementation. `WebCudaSemaphore.h` therefore maintains
real permit counts for the current single-thread export. Empty `TryAcquire`
returns false immediately, including with a finite timeout; timed blocking is
not supported. An exhausted blocking `Acquire` and invalid counts fail explicitly.
Enabling pthread compilation is rejected until a worker implementation is added.
Browser platform tests verify permit exhaustion/replenishment plus three failure
cases, alongside the existing memory, WasmFS and original Chaos transform tests.
This does not establish threaded Chaos or a running full solver.

The 2026-10-05 scene-cook attempt was stopped after reproducing original global
shader failures, rather than waiting for the same incomplete ABI to fail every
permutation. `artifacts/shader-format/cook-result.json` explicitly records
`stopped-after-compiler-errors`; it is not a successful or completed cook.
`Tests/summarize-shader-jobs.py` groups the retained diagnostics (including any
standalone probe invoked during the same interval). That attempt exposed gaps in
typed texture/buffer resources, complex constant layouts, uniform-buffer objects,
unsupported HLSL-to-WGSL constructs and raster stages. Subsequent sections record
the original-engine/browser checks added for those areas; the complete global
shader cook and scene renderer remain unfinished.

The direct Slang-to-WGSL path for the fully prepared original `FScreenProbeIntegrateCS` permutation 0 fails
on `RWTexture2DArray<unorm float> RWLightIsMoving`. Its original RDG allocation is
`PF_R8`; simply dropping the normalized qualifier would change storage behavior.
An isolated format-specialization audit confirmed that installed Slang 2026.13.1
accepts `[format("r8")]` but emits warning E31105 and substitutes `rgba32float`.
The exporter now treats that warning as a failed compile, even when Slang exits
successfully. `Tests/audit-storage-formats.py` reproduces the rejection using the
original integration shader's retained preprocessing output; it produces no WCSH
container. The lighting outputs also depend on `Lumen::GetLightingDataFormat()`.
Their format must be carried from resource views into a compatible pipeline/backend.
The compiler path below now handles these explicit formats; its cooked resource
container and full RHI binding integration remain unfinished. Slang's format annotation and
write-only texture behavior are documented in the
[format reference](https://docs.shader-slang.org/en/stable/external/core-module-reference/attributes/format.html)
and [WGSL target guide](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/a2-03-wgsl-target-specific.html).

The shared C++ texture backend now allocates the following formats without
substitution. The corresponding Unreal texture factory and SRV/UAV view wrappers
compile against the original RHI headers. Their shared WebGPU implementation
executes in the browser harness; the Unreal resource objects themselves are not
yet instantiated by a running engine renderer.

| Unreal format | WebGPU format |
|---|---|
| `PF_R8` | `r8unorm` |
| `PF_R8G8B8A8` | `rgba8unorm` |
| `PF_R32_UINT` | `r32uint` |
| `PF_FloatRGBA` | `rgba16float` |
| `PF_A32B32G32R32F` | `rgba32float` |
| `PF_FloatR11G11B10` | `rg11b10ufloat` |

Allocation checks the actual device limits, mip count, array range, usage and
format. R8 and packed R11G11B10 storage allocation requires an advertised and
requested `texture-formats-tier1` feature; packed R11G11B10 render attachment
usage additionally requires `rg11b10ufloat-renderable`. Unsupported descriptions
are rejected before submitting commands. SRV/UAV wrappers use Unreal's original
`GetViewInfo()` to resolve mip/layer ranges and reject format reinterpretation,
sRGB, alternate planes, cube/3D dimensions and unsupported usage.

The 2026-10-05 checks passed in Edge 154.0.4258.53 and ChromiumRTXCuda
157.0.8086.0. Each ran 54 cases across three mips and three layer ranges, comparing
73,728 exact bytes including row padding. R8 quantization and half/full/packed
float encodings are checked with independently calculated expected values.
These are clear/copy/readback operations, not shader texture writes, a native
CUDA bridge test or full Lumen execution. The direct Slang WGSL format limitation
above remains; the alternative compiler path below avoids that substitution.
Both browsers also pass the existing original Lumen and copy kernels.

```powershell
./engine/Tests/build-webgpu.ps1
node engine/Tests/browser-webgpu.mjs
node engine/Tests/browser-webgpu.mjs --custom
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/Tests/audit-webgpu-resources.py
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/Tests/validate-texture-resources.py
```

The joined, source-hash-checked evidence is
`artifacts/webgpu-engine-resources/validation.json`; it records the distinction
between Unreal factory compilation and browser backend execution explicitly.

`ShaderCompiler/compile_texture_spirv.py` now compiles the original Lumen
integration shader via Slang SPIR-V 1.3 and the Tint compiler from the user's
ChromiumRTXCuda source tree. `build-tint.ps1` builds an isolated native CLI under
`artifacts/tint-cli`; it does not modify the browser or its compiler sources.
Tint's reader validates Vulkan 1.1/SPIR-V 1.3, so Slang's default SPIR-V 1.5 output
cannot be passed directly. SPIR-V validation runs before and after access lowering.

The compiler accepts explicit resource-view formats, specializing only the
global texture declarations. The current fixture uses Unreal's default
`r.Lumen.LightingDataFormat=0`: R11G11B10 for diffuse/specular and R8 for moving
light. It does not infer these formats from a float HLSL type or force every
texture to the same representation. Different native view formats require a
corresponding compiler specialization. The supported format strings are in
[Slang's texture format reference](https://shader-slang.org/stdlib-reference/types/0texture-01/index.html).

`spirv_access.py` follows direct storage-image variable/load/write uses and adds
`NonReadable` only when every use qualifies as a write. Unknown or read uses
retain their original access. For this shader it adds three annotations while
preserving all original SPIR-V instructions exactly. It never edits generated
WGSL or replaces shader calculations. The pass checks instruction preservation
and idempotence. A subsequent standard `spirv-opt -O` pass removes unreachable
branches and unused resources; SPIR-V validation runs after both steps. Only
the access-annotation pass preserves every instruction byte-for-byte. The
optimizer transforms instructions normally; neither step replaces algorithms.

The resulting `ScreenProbeIntegrateCS` pipeline is accepted by both browsers,
including through the C++ Wasm `Device::CreateComputePipeline` path. The device
requests the adapter's supported storage-texture limit. The initial unoptimized
output retained six storage textures, including three unreachable UAV reads.
After optimization the shader needs three storage textures. The separate browser
compiler probe requests the emitted requirement and captures compilation and
pipeline-validation messages. Both browsers returned no errors.

The C++ Wasm backend now dispatches this original shader with 21 buffer,
texture and sampler bindings in both Edge and ChromiumRTXCuda. The test selects
the shader's existing **unlit-material branch**, which writes zero to its three
lighting targets. Targets begin with nonzero sentinels. Readback verifies 10,752
bytes per browser, including exact R8 and packed R11G11B10 writes, untouched
pixels outside the 7-by-5 region, an untouched second layer and row padding.
This validates binding and shader texture writes; it does **not** exercise the
lit-material integration algorithm or establish lighting parity.

`WebCudaShaderProbe -MetadataOnly` captures original Unreal parameter metadata.
`validate-integrate-layout.py` compares 287 native fields across the root,
View and BlueNoise blocks with Slang reflection and emitted WGSL layout,
including 31 matrices and 15 arrays. The 107 bindless placeholder slots are
verified unused by this permutation and remain zero in the upload; textures
are bound separately. HLSL compilation explicitly selects row-major, matching
Unreal's D3D `-Zpr` flag. Slang transposes its matrix representation for SPIR-V,
so SPIR-V decoration names alone cannot determine the HLSL matrix convention.
This static check does not provide numerical camera-transform parity.

The shared `Device` supports mixed resource bindings, checked buffer ranges,
duplicate-binding rejection and point/linear samplers. `texture_container.py`
emits WCSH v2, preserving v1 support for the earlier compute kernels. Its
descriptors include resource kind, texture dimension/format/access, buffer
size/stride, root constant mappings and uniform resource tables. Decode rejects
invalid descriptors, mappings, table entries and truncated data; encode/decode
round-trip bytes are checked in Wasm.

The native `ShaderFormatWebCuda` invokes Unreal's `BuildResourceTableMapping`
and serializes seven entries from SceneTexturesStruct and BlueNoise, together
with their layout hashes. The original `FScreenProbeIntegrateCS` constructor and
binding verification pass using the resulting parameter map. This required
removing unreachable resource reads through the standard optimizer, not adding
those resources to Unreal's parameter structs. The commandlet also fails if
shader construction logs a handled ensure instead of reporting it as success.

`Device::DispatchShader` resolves those tables, validates their layout hashes,
and checks texture formats and dimensions against the cooked descriptors. The
browser test reads the actual engine-produced WCSH container, independently
builds uniform resource arrays from Unreal's exported metadata, and verifies
rejection of bad hashes, missing tables and mismatched formats before dispatch.
`Config/WebCudaStorageFormats.json` provides the explicit default Lumen format
contract for this source/entry; different resource formats require corresponding
specialization and cannot silently pass the runtime checks.

Engine FRHI uniform-buffer objects now have a concrete factory, with the compile
and runtime boundaries described below. Complete command contexts, other shader
stages, full scene cook and scene submission still need integration. No complete
Lumen frame has run in Wasm. The probe's resource allocation remains a test
harness, not a cooked scene or an engine RHI command context.

Reproduce this retained original-shader compilation and pipeline check:

```powershell
./engine/build-tint.ps1
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/ShaderCompiler/compile_texture_spirv.py --source engine/artifacts/shader-format/jobs/E722740247C59189C24F3F8D688D39A7/source.hlsl --entry ScreenProbeIntegrateCS --formats engine/Tests/lumen-integrate-texture-formats.json --output engine/artifacts/shader-format/spirv-format-probe/compiled
./engine/Tests/probe-original-shader.ps1 -ShaderType FScreenProbeIntegrateCS -Permutation 0 -Name lumen-integrate-layout -MetadataOnly
./engine/Tests/probe-original-shader.ps1 -ShaderType FScreenProbeIntegrateCS -Permutation 0 -Name original-lumen-integrate-v2
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/Tests/validate-integrate-layout.py
node engine/Tests/probe-tint-wgsl.mjs --source=engine/artifacts/shader-format/spirv-format-probe/compiled/shader.wgsl
node engine/Tests/probe-tint-wgsl.mjs --custom --source=engine/artifacts/shader-format/spirv-format-probe/compiled/shader.wgsl
./engine/Tests/build-webgpu.ps1
node engine/Tests/browser-webgpu.mjs
node engine/Tests/browser-webgpu.mjs --custom
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/Tests/validate-integrate-pipeline.py
```

`artifacts/shader-format/spirv-format-probe/pipeline-validation.json` joins source
hashes, parameter layouts, access annotations and browser/Wasm dispatch results. The WebGPU
test build now requires this compiled integration-shader fixture in addition to
the original indirect-argument and copy-shader fixtures.

The actual scene C++ build later stopped at action 1,630 with additional Core
port errors. `audit-core-build.py` now successfully compiles the five affected
translation units: AllocationGroup, LLM, App, AutomationTest and
CompactBinaryWriterTest. The platform supplies the wasm64 architecture name,
constexpr cooked-data property and Unreal's Unix-compatible signed-size type.
Generated header overlays preserve the explicit noinline allocator contract
and enable the existing LP64 size_t serialization overload. Installed engine
sources are unchanged. The prior log is saved under
`artifacts/demo-attempt/history/core-build-20261005T081216Z`; the actual game build
has resumed with the affected platform headers requiring recompilation.

### Unreal resource factories

`WebGPURHIResources` now implements concrete FRHI buffer SRVs/UAVs, texture
views, samplers, uniform buffers and compute shader objects. These compile using
the actual sample target's Wasm64 UBT response files, including its RenderCore
and RHICore dependencies. `audit-webgpu-resources.py` records the source and
public-header hashes with the compiler results. The factories are not yet
instantiated through a running Unreal command context.

Uniform buffers retain Unreal's resource references and layout hashes separately
from GPU constants. Uploads preserve the native constant offsets, zero resource
pointer slots and trailing padding, and allocate a fresh GPU buffer on update
so an earlier submission keeps its original data. Pooling these allocations is
future performance work. Buffer views preserve offset and size and reject
unsupported types or WebGPU alignment violations. Samplers preserve supported
filtering, per-axis addressing, comparison and LOD ranges; unsupported Unreal
features such as mip bias and border color are rejected.

Compute shader creation validates Unreal's optional-data trailer, decodes only
the actual WCSH payload, rebuilds its resource-table token streams and initializes
the original RHICore static uniform slots. The native compiler probe compares
those token streams with Unreal's `BuildShaderResourceTable` output. This is
compile/metadata validation of the FRHI path, not evidence of scene execution.

`FWebGPUComputeState` accepts Unreal's original `FRHIShaderParameter` and
`FRHIShaderParameterResource` batches, retains resource references, preserves
partial constant updates and validates whole batches before committing them.
At dispatch it checks that all required constant bytes were supplied, packs the
original compiler mappings, resolves uniform tables and calls the shared C++
backend. Unsupported bindless/resource-collection parameters are rejected.
This adapter compiles against the actual target, but has not executed through
Unreal and is not a registered `FDynamicRHI`.

`FWebGPUComputeContext` now derives from Unreal's `IRHIComputeContext` and
implements pipeline selection, batched parameters, static uniform-buffer slots,
direct/indirect dispatch, buffer/texture UAV clears, single-queue transitions and
GPU debug groups. A compile-time assertion verifies it is concrete. No context
has been registered or run through Unreal.
The dispatch methods reject unsupported commands with a fatal diagnostic rather
than silently skipping engine work. Dynamic uniform offsets other than zero
are not implemented yet.

Transition creation checks resource types, declared GPU usages, subresource
ranges and unsupported flags before retaining resources. Begin/end validates
ordering and duplicate entries. Only Graphics-to-Graphics transitions on the
single WebGPU queue are accepted. Queue transfers, transient aliasing, reserved
memory commits and unsupported access modes reject. Since every current GPU
operation is submitted immediately on that queue, Dawn supplies the barriers
between passes without a CPU wait. The FRHI transition factory and command path
are compile-checked only; their runtime integration is not yet verified. The
future dynamic RHI must install the documented private transition storage size
and alignment before calling this factory.

GPU breadcrumbs wrap submitted command encoders in nested WebGPU debug groups.
They do not supply GPU timestamps or GPU crash progress markers. Both browsers
execute the existing shader/resource suite inside two nested groups and verify
depth, label and stack-underflow rejection. This exposed missing Wasm64 type
metadata in Emdawn's by-value `WGPUStringView` debug-group entry point. The
project-owned `Runtime/webgpu-wasm64.js` provides a typed forwarding function
that delegates to the original Emdawn implementation; no third-party source or
generated output is patched. Browser reports include its source hash.

The UBT toolchain now adds that library to WebGPU links and tracks it as an
action prerequisite. The revised build tool compiled in the isolated
`artifacts/ubt-bridge-audit` directory. The already running scene build keeps its
existing UBT process; its pending link response was augmented with the same
argument, with the original response retained in the audit directory. The
changed build tool must be deployed with `build-tool.ps1` before the next fresh
UBT invocation, after the current process exits. Actual scene linking has not
yet occurred.

Indirect dispatch uses WebGPU's GPU argument buffer directly. The runtime tests
execute Unreal's original copy kernel to generate the three dispatch dimensions
at a nonzero buffer offset, then submit a second invocation using those GPU
values without CPU readback. Both browsers pass 21 cases / 5,376 exact uint
comparisons, including zero workgroups and four invalid argument-buffer range
or usage checks. Direct zero-workgroup dispatch is also accepted, matching
Dawn's semantics; oversized dimensions remain rejected.

Buffer UAV clears call Unreal's existing `ClearUAVShader_T` helper through its
recursive compute command list, selecting the original
`FClearReplacementCS_Buffer_Uint_Bounds` shader. No clear shader was authored.
The native exporter verified original preprocessing and FShader bindings for
that type. Its engine-produced WCSH executes in both browsers across 72 cases /
27,648 exact uint comparisons, including buffer subviews, partial ranges, empty
ranges, raw float bit patterns and guards. The tests execute the shared C++ GPU
backend; the recursive engine command path has only been compile-checked.

Texture UAV clears use WebGPU render-pass attachment clears, preserving the
resource format and selected mip/layer range. Supported UAV textures include
RenderAttachment usage internally so the backend can clear them without a new
shader. Array-layer clears share one command encoder and queue submission.
The six currently supported formats are R8, RGBA8, R32Uint, RGBA16Float,
RGBA32Float and RG11B10Float. Integer/float API mismatches and non-finite values
reject; additional formats still require implementation.

Both browsers pass 324 clear cases with 1,990,656 exact bytes compared each,
covering all mips, all contiguous array-layer ranges, unchanged subresources and
row padding. R32Uint values include 0x80000001 and 0xffffffff. An initial R8
test of 0.5 observed 127 instead of the expected 128, exposing a quantization-tie
assumption in the test. Exact UNORM tests now use unambiguous n/255 inputs;
native-versus-WebGPU parity at quantization ties has not been established. These
runtime tests exercise the shared backend; the FRHI clear calls themselves
remain compile-checked only.

The shared C++ runtime tests execute in both Edge and ChromiumRTXCuda. They
read back two uniform upload versions (128 exact bytes), checking that the old
version survives the replacement upload, pointer bytes are zero and padding is
zero. Seven invalid packing inputs are rejected without changing the output.
Three sampler descriptions are created; nine invalid descriptions and duplicate
creation are rejected. Existing original-shader and texture tests also pass.
The browser reports explicitly record `unrealFactoryExecuted: false`.

The sample build was intentionally restarted once after adding the required
RenderCore/RHICore module dependencies. That attempt's logs and reason are
retained under `artifacts/demo-attempt/history/rhi-dependencies-20261005`.
Use the current `artifacts/demo-attempt/result.json` and `build.log` for live
status rather than treating the retained attempt as the latest build result.

### Original raster shader compiler path

`ShaderCompiler/compile_raster.py` lowers original, Unreal-preprocessed vertex
and pixel HLSL through Slang SPIR-V, `spirv-opt -O`, validation and Tint. Slang's
direct WGSL target rejected the original FScreenPS `SV_Target0` semantic; the
SPIR-V/Tint path accepts the original source unchanged. Vertex resources retain
group 0 and pixel resources move to group 1, preserving stage-local slot indices.
Only SPIR-V DescriptorSet annotations change in this binding pass; its remaining
instructions are checked unchanged and the resulting module is revalidated.

The original `FScreenVS` and `FScreenPS` now also pass the native ShaderFormat
module, original `FShader` construction and Unreal parameter binding checks.
WCSH v3 preserves the extended v2 resource-table layout, identifies vertex/pixel
stages explicitly and requires zero compute workgroup dimensions. Compute keeps
v1/v2. Re-exporting original `FScreenProbeIntegrateCS` after this integration
produced the same v2 container bytes (SHA-256
`ebc7b19e6af1e20968103cd0edb88d65a804840638481c7dc9624102958c9b2b`).

`FWebGPUVertexShader` and `FWebGPUPixelShader` decode the original Unreal code
trailer, install resource-table tokens/static uniform slots and create shader
modules. Both compile with the real scene's UBT Wasm64 flags. Their RHI factories
are not yet called by a registered dynamic RHI.

The shared C++ device now creates raster modules/pipelines and submits direct or
indexed draws. Both stages use the same validated resource/table resolution as
compute, with vertex bindings in group 0 and pixel bindings in group 1. Native
WebGPU descriptors carry pipeline/pass state. The draw API also accepts viewport,
scissor, stencil reference and blend constant state. It does not yet implement
Unreal's graphics command context, render-pass batching or the complete graphics
pipeline cache.

`Tests/raster-draw.cpp` executes the two engine-produced containers through Wasm64
in Edge and ChromiumRTXCuda. Each browser passes 45 cases: direct draws, uint16
and uint32 indexed draws, full/partial rectangles, horizontal UV flips, viewport,
scissor, depth pass/rejection, stencil pass/rejection, color write masks, blend
constants and both culling windings. The
tests use a patterned sampled texture, nonzero vertex/index offsets and indexed
base vertex. Each compares 3,072 readback bytes, including untouched background
and row padding, for 138,240 exact bytes per browser. Invalid stage headers,
swapped pipeline stages, missing resources and invalid buffer/index ranges are
rejected. No shader source or lighting algorithm was authored for these tests.

`artifacts/raster-probe/validation.json` joins source/compiler/container hashes,
native shader-object checks, original SPIR-V instruction preservation, C++
compilation and both browsers' actual draw results. These are offscreen raster
tests; they do not establish a full Unreal frame or rendered Lumen lighting.
The existing compute, original Lumen unlit-branch, buffer, texture and platform
tests also pass after sharing their binding validation with raster.

The Unreal resource adapter now includes concrete vertex declarations,
rasterizer/depth-stencil/blend states and graphics pipeline state objects.
`FGraphicsPipelineStateInitializer` supplies the original shaders, vertex layout,
primitive topology, attachment formats and fixed-function state. Vertex streams
are mapped into dense WebGPU slots while preserving original stream indices,
attribute locations, strides and instance/vertex step mode. The state translation
follows Unreal's D3D12 conventions for CCW front faces and back-face stencil
inheritance. When separate back-face stencil is disabled, both faces use the
front state. Min/max blending uses WebGPU's required one/one factors because
those operations ignore Unreal's blend factors.

Depth targets map `PF_ShadowDepth` to `depth32float` and `PF_DepthStencil` to
`depth24plus-stencil8`. The latter has implementation-defined depth precision;
byte-for-byte native depth storage parity is not claimed. Depth-only sample views
and whole depth/stencil attachment views are distinct. Color upload/readback and
color clear helpers reject opaque depth storage. Color allocation also recognizes
BGRA8 and RGBA8/BGRA8 sRGB formats, with consistent SRV interpretation; those
additional color formats have not yet received pixel-output checks. Depth-view
sampling through original Unreal shaders remains untested.

The pipeline wrapper currently accepts vertex/pixel stages, triangle/line/point
lists and single-sample attachments. It explicitly rejects mesh/geometry stages,
depth-only vertex pipelines, strip topology, wireframe, depth clamp, nonzero
constant depth bias, dual-source blending, alpha-to-coverage, nontrivial VRS,
multiview and subpasses. Nonzero constant depth bias needs a depth-format-specific
conversion of Unreal's units. The pipeline wrapper and depth transition checks
are verified by compilation with actual UBT Wasm flags; the 45 browser draw cases
exercise the shared C++ backend, not the Unreal factories or command context.

```powershell
./engine/Tests/probe-original-shader.ps1 -ShaderType FScreenVS -Name original-screen-vs
./engine/Tests/probe-original-shader.ps1 -ShaderType FScreenPS -Name original-screen-ps
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/ShaderCompiler/compile_raster.py --source engine/artifacts/shader-format/original-screen-vs.wcs.preprocessed.hlsl --entry Main --stage vertex --output engine/artifacts/raster-probe/vertex
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/ShaderCompiler/compile_raster.py --source engine/artifacts/shader-format/original-screen-ps.wcs.preprocessed.hlsl --entry Main --stage fragment --output engine/artifacts/raster-probe/fragment
node engine/Tests/probe-raster-pipeline.mjs
node engine/Tests/probe-raster-pipeline.mjs --custom
./engine/Tests/build-raster.ps1
node engine/Tests/browser-raster.mjs
node engine/Tests/browser-raster.mjs --custom
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/Tests/audit-webgpu-resources.py
& engine/tools/emsdk/python/3.13.3_64bit/python.exe engine/Tests/validate-raster-pipeline.py
```
