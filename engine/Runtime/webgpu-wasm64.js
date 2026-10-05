// Emdawn's generated signature list omits this by-value WGPUStringView API.
// Clang lowers the struct argument to a pointer. This typed forwarding function
// gets Emscripten's checked Wasm64 BigInt-to-Number wrapper. A separate name is
// necessary because the port deletes pre-existing wgpu* signature metadata.
// The original Emdawn implementation still performs the WebGPU operation.
addToLibrary({
  webcudaCommandEncoderPushDebugGroup__sig: 'vpp',
  webcudaCommandEncoderPushDebugGroup__deps: ['wgpuCommandEncoderPushDebugGroup'],
  webcudaCommandEncoderPushDebugGroup: (encoder, label) => {
    _wgpuCommandEncoderPushDebugGroup(encoder, label);
  },
});
