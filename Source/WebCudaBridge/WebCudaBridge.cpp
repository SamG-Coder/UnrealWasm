// The native Unreal RHI will submit this same packet ABI. This standalone UBT
// target validates Wasm64 compilation and the browser queue without requiring
// the unfinished Unreal Core/HAL port. It implements no shader or lighting.
#include <stdint.h>
#include <stddef.h>

static_assert(sizeof(void*) == 8, "WebCuda requires a Wasm64 target");
struct alignas(8) FWebCudaDispatch {
    uint32_t Version;
    uint32_t Shader;
    uint32_t Groups[3];
    uint32_t ConstantBytes;
    uint64_t Constants;
    uint32_t ResourceCount;
    uint32_t Reserved;
    uint64_t Resources;
};
struct FWebCudaResource { uint32_t Handle; uint32_t Elements; };
static_assert(sizeof(FWebCudaDispatch) == 48);
static_assert(offsetof(FWebCudaDispatch, Constants) == 24);
static_assert(offsetof(FWebCudaDispatch, Resources) == 40);
static FWebCudaDispatch Dispatch{};
alignas(16) static unsigned char Constants[4096]{};
static FWebCudaResource Resources[32]{};

#define WEB_EXPORT(name) extern "C" __attribute__((used, visibility("default"), export_name(name)))
WEB_EXPORT("webcuda_constants") uint64_t GetConstants() { return reinterpret_cast<uint64_t>(Constants); }
WEB_EXPORT("webcuda_resources") uint64_t GetResources() { return reinterpret_cast<uint64_t>(Resources); }
WEB_EXPORT("webcuda_dispatch") uint64_t PrepareDispatch(uint32_t shader, uint32_t x, uint32_t y, uint32_t z, uint32_t bytes, uint32_t resources) {
    if (!x || !y || !z || bytes > sizeof(Constants) || resources > 32) return 0;
    Dispatch = {1, shader, {x,y,z}, bytes, reinterpret_cast<uint64_t>(Constants), resources, 0, reinterpret_cast<uint64_t>(Resources)};
    return reinterpret_cast<uint64_t>(&Dispatch);
}
