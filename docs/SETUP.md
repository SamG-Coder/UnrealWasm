# Setup notes

This snapshot is for developers working on the port. It is not a one-command game exporter.

## Reference toolchain

- Unreal Engine 5.8.2 on Windows, including the engine/dependency sources needed by the scripts.
- Visual Studio C++ tools and Windows SDK for native compiler/reference tools.
- Emscripten 6.0.11, installed locally under ignored `engine/tools/` by the setup script.
- Slang 2026.13.1 and SPIR-V tools from the tested Vulkan SDK 1.4.357.0.
- Tint built from a separately supplied Chromium/Dawn source tree.
- Node.js and Playwright for browser checks.
- A WebGPU-capable browser; optional CUDA/OptiX tests require ChromiumRTXCuda and suitable NVIDIA hardware.

## Entry points

Review the scripts first; source layouts and version assumptions may require adaptation.

```powershell
./engine/setup-compilers.ps1
./engine/build-tool.ps1 -Engine 'C:/Your/UnrealEngine/UE_5.8'
```

The second command creates an isolated source overlay and custom UBT. It does not build the full engine or produce a playable export. Scripts do not download Unreal.

`scripts/native.ps1` accepts `-Engine`; its `Assets` action invokes `scripts/create-assets.py` to generate the excluded sample content. [Legacy fixture notes](LEGACY_FIXTURE.md) preserve the historical sample instructions.

Original shader probes require the native shader-format module and isolated cooking host first. `engine/Tests/build-raster.ps1` and `browser-raster.mjs` then consume the generated local captures. They cannot run from the public source alone.

Several scripts retain reference-machine Windows paths. Main engine scripts accept `-Engine`; others require adapting defaults, including browser/Playwright locations. Portable bootstrap and clean-checkout reproduction remain unfinished. `engine/README.md` records the experimental sequence rather than a guaranteed setup procedure.

Never commit the ignored engine artifacts, SDKs, assets, binaries, intermediates or saved configurations. These can contain engine source, commercial material, machine-local data or credentials.
