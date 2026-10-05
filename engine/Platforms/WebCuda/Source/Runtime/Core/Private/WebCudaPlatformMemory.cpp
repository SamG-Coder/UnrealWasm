#include "WebCuda/WebCudaPlatformMemory.h"
#include <emscripten/heap.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

const FPlatformMemoryConstants& FWebCudaPlatformMemory::GetConstants()
{
    static FPlatformMemoryConstants Constants;
    // These describe the Wasm address space, not the host machine's RAM.
    Constants.TotalPhysical = emscripten_get_heap_max();
    Constants.TotalVirtual = Constants.TotalPhysical;
    Constants.PageSize = 65536;
    Constants.BinnedPageSize = 65536;
    Constants.BinnedAllocationGranularity = 65536;
    Constants.TotalPhysicalGB = static_cast<uint32>((Constants.TotalPhysical + (1ull << 30) - 1) >> 30);
    return Constants;
}
FPlatformMemoryStats FWebCudaPlatformMemory::GetStats()
{
    FPlatformMemoryStats Stats;
    const auto Info = mallinfo();
    Stats.UsedPhysical = Info.uordblks;
    Stats.UsedVirtual = emscripten_get_heap_size();
    Stats.AvailablePhysical = Stats.TotalPhysical - Stats.UsedPhysical;
    Stats.AvailableVirtual = Stats.TotalVirtual - Stats.UsedVirtual;
    return Stats;
}
void* FWebCudaPlatformMemory::BinnedAllocFromOS(SIZE_T Size)
{
    void* Pointer = nullptr;
    return posix_memalign(&Pointer, 65536, Size) == 0 ? Pointer : nullptr;
}
void FWebCudaPlatformMemory::BinnedFreeToOS(void* Pointer, SIZE_T) { free(Pointer); }

FWebCudaPlatformMemory::FPlatformVirtualMemoryBlock
FWebCudaPlatformMemory::FPlatformVirtualMemoryBlock::AllocateVirtual(size_t Size, size_t Alignment)
{
    FPlatformVirtualMemoryBlock Block;
    constexpr size_t Page = GetVirtualSizeAlignment();
    if (!Size || Size > SIZE_MAX - (Page - 1) || Alignment < Page || (Alignment & (Alignment - 1))) return Block;
    const size_t Pages = (Size + Page - 1) / Page;
    if (Pages > UINT32_MAX || Pages * Page > emscripten_get_heap_max()) return Block;
    if (posix_memalign(&Block.Ptr, Alignment, Pages * Page)) return Block;
    Block.CommittedPages = static_cast<uint8*>(calloc(Pages, 1));
    if (!Block.CommittedPages) { free(Block.Ptr); Block.Ptr = nullptr; return Block; }
    Block.VMSizeDivVirtualSizeAlignment = static_cast<uint32>(Pages);
    return Block;
}
void FWebCudaPlatformMemory::FPlatformVirtualMemoryBlock::ValidateRange(size_t Offset, size_t Size) const
{
    if (!Ptr || Offset % GetCommitAlignment() || Size % GetCommitAlignment() ||
        Offset > GetActualSize() || Size > GetActualSize() - Offset) __builtin_trap();
}
void FWebCudaPlatformMemory::FPlatformVirtualMemoryBlock::Commit(size_t Offset, size_t Size)
{
    ValidateRange(Offset, Size);
    const size_t First = Offset / GetCommitAlignment(), End = (Offset + Size) / GetCommitAlignment();
    // Already committed pages retain their contents. Coalesce newly committed
    // runs into one memset. Recommitting a decommitted page returns zeroed bytes.
    for (size_t Page = First; Page < End;)
    {
        if (CommittedPages[Page]) { ++Page; continue; }
        const size_t Start = Page;
        while (Page < End && !CommittedPages[Page]) CommittedPages[Page++] = 1;
        memset(static_cast<uint8*>(Ptr) + Start * GetCommitAlignment(), 0, (Page - Start) * GetCommitAlignment());
    }
}
void FWebCudaPlatformMemory::FPlatformVirtualMemoryBlock::Decommit(size_t Offset, size_t Size)
{
    ValidateRange(Offset, Size);
    // Logical decommit only: Wasm cannot reclaim or protect subranges of memory.
    memset(CommittedPages + Offset / GetCommitAlignment(), 0, Size / GetCommitAlignment());
}
void FWebCudaPlatformMemory::FPlatformVirtualMemoryBlock::FreeVirtual()
{
    free(CommittedPages); free(Ptr);
    CommittedPages = nullptr; Ptr = nullptr; VMSizeDivVirtualSizeAlignment = 0;
}
