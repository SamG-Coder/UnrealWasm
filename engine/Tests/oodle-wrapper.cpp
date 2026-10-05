// C ABI and SDK allocator hooks only. Codec algorithms remain in the SDK.
#include "oodle2.h"
#include <stdint.h>
#include <stdlib.h>
static void* OodleAllocate(OO_SINTa bytes, OO_S32 alignment) {
    void* pointer = nullptr;
    return posix_memalign(&pointer, static_cast<size_t>(alignment), static_cast<size_t>(bytes)) == 0 ? pointer : nullptr;
}
extern "C" void oodle_init() {
    // The SDK intentionally leaves its Wasm default allocator unimplemented.
    // Use its supported hook, as Unreal installs its own allocator at startup.
    OodleCore_Plugins_SetAllocators(OodleAllocate, free);
}
extern "C" int32_t oodle_decompress(const void* input, uint32_t bytes, void* output, uint32_t rawBytes) {
    return static_cast<int32_t>(OodleLZ_Decompress(input, bytes, output, rawBytes,
        OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_Yes, OodleLZ_Verbosity_None));
}
extern "C" int32_t oodle_bound(uint32_t rawBytes) {
    return static_cast<int32_t>(OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken, rawBytes));
}
extern "C" int32_t oodle_compress(const void* input, uint32_t bytes, void* output) {
    return static_cast<int32_t>(OodleLZ_Compress(OodleLZ_Compressor_Kraken, input, bytes, output, OodleLZ_CompressionLevel_Normal));
}
