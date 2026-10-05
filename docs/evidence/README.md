# Historical raster evidence

These reports contain results and hashes only. No generated Unreal shader source or binary is distributed.

- `raster-edge.json`: latest Edge backend run, 60 cases and 184,320 exact readback bytes.
- `raster-custom-chromium.json`: earlier custom Chromium run, 45 cases and 138,240 exact readback bytes.

The backend hashes differ. This is not two-browser validation of the current tree. Both reports exercise original Unreal screen shaders through the shared C++ backend; neither executes a registered Unreal RHI or renders the full scene. Recreating shader inputs requires your own Unreal installation.
