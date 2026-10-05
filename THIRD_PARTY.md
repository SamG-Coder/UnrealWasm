# External dependencies and excluded material

This repository distributes custom integration code and build/test instructions. It does not bundle Unreal Engine or SDK/dependency source archives.

| Component | Use and distribution |
| --- | --- |
| Unreal Engine | Engine runtime, editor, headers, shaders and sample asset generation. Not included; obtain from Epic under its applicable license. |
| Oodle Data | Compression experiments. SDK source and binaries are excluded; local scripts require separately supplied material and its license. |
| Emscripten / LLVM | Wasm64 C++ compilation and WebGPU bindings. Download/build separately; upstream licenses apply. |
| Slang / SPIR-V Tools / Tint | Shader lowering and validation. Obtain/build separately; upstream licenses apply. |
| ICU, FreeType, HarfBuzz, libpng, zlib, AHEasing | Dependency integration. Source archives/libraries are excluded; preserve upstream notices when obtaining/building them. |
| ChromiumRTXCuda / cuda-webshader | Optional native GPU bridge and historical fixtures. Separate repositories, not bundled. |

Generated engine overlays, preprocessed HLSL, engine-derived WGSL/SPIR-V, shader containers, engine tools, Oodle outputs, Unreal assets and captured images are excluded. Lookup/replacement strings in integration scripts locate changes in the user's local engine; generated copies remain local.

The MIT license does not relicense these dependencies or expand their redistribution rights. See [Epic's source-sharing guidance](https://www.unrealengine.com/ue-on-github?lang=en-US).

Unreal Engine is a trademark of Epic Games. This independent project is not endorsed by Epic Games.
