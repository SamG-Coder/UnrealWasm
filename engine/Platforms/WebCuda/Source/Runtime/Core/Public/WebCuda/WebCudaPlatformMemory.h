#pragma once
#include "GenericPlatform/GenericPlatformMemory.h"
struct FPlatformMemoryStats : FGenericPlatformMemoryStats {};
struct FWebCudaPlatformMemory : FGenericPlatformMemory
{
    // Wasm addresses are linear-memory offsets, not host pointers. Bit 63 is
    // unavailable to this target's bounded heap and can tag cooked asset values.
    // Keep the linker's MAXIMUM_MEMORY below that bit if the heap cap changes.
    static constexpr uint32 KernelAddressBit = 63;
    static CORE_API const FPlatformMemoryConstants& GetConstants();
    static CORE_API FPlatformMemoryStats GetStats();
    static CORE_API void* BinnedAllocFromOS(SIZE_T Size);
    static CORE_API void BinnedFreeToOS(void* Pointer, SIZE_T Size);
    static constexpr bool CanOverallocateVirtualMemory() { return false; }

    // Wasm linear memory has no sparse reservation or page protection. Allocate
    // backing storage once; track logical commits without clearing unused pages.
    // Explicit FreeVirtual follows the engine's existing non-owning block API.
    class FPlatformVirtualMemoryBlock : public FBasicVirtualMemoryBlock
    {
        uint8* CommittedPages = nullptr;
        CORE_API void ValidateRange(size_t Offset, size_t Size) const;
    public:
        static constexpr size_t GetCommitAlignment() { return 65536; }
        static constexpr size_t GetVirtualSizeAlignment() { return 65536; }
        size_t GetActualSize() const { return size_t(VMSizeDivVirtualSizeAlignment) * GetVirtualSizeAlignment(); }
        static CORE_API FPlatformVirtualMemoryBlock AllocateVirtual(size_t Size, size_t Alignment = GetVirtualSizeAlignment());
        CORE_API void Commit(size_t Offset, size_t Size);
        CORE_API void Decommit(size_t Offset, size_t Size);
        CORE_API void FreeVirtual();
        void Commit() { Commit(0, GetActualSize()); }
        void Decommit() { Decommit(0, GetActualSize()); }
        void CommitByPtr(void* Address, size_t Size) { Commit(size_t(reinterpret_cast<UPTRINT>(Address) - reinterpret_cast<UPTRINT>(Ptr)), Size); }
        void DecommitByPtr(void* Address, size_t Size) { Decommit(size_t(reinterpret_cast<UPTRINT>(Address) - reinterpret_cast<UPTRINT>(Ptr)), Size); }
    };
};
using FPlatformMemory = FWebCudaPlatformMemory;
