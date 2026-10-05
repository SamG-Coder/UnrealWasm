#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/PlatformString.h"
#include "HAL/PlatformMath.h"
#include "HAL/UESemaphore.h"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h>
#include "Chaos/Transform.h"

extern "C" int platform_run()
{
    static_assert(sizeof(void*) == 8 && sizeof(TCHAR) == 2 && sizeof(wchar_t) == 4);
    volatile int64 Value = 11;
    if (FPlatformAtomics::InterlockedCompareExchange(&Value, int64(37), int64(11)) != 11 || Value != 37) return 1;
    if (FPlatformAtomics::InterlockedAdd(&Value, int64(5)) != 37 || Value != 42) return 2;
    void* Memory = FPlatformMemory::BinnedAllocFromOS(8192);
    if (!Memory || reinterpret_cast<UPTRINT>(Memory) % 65536) return 3;
    std::memset(Memory, 0xa5, 8192);
    uint32 Slot = FPlatformTLS::AllocTlsSlot();
    if (!FPlatformTLS::IsValidTlsSlot(Slot)) return 4;
    FPlatformTLS::SetTlsValue(Slot, Memory);
    if (FPlatformTLS::GetTlsValue(Slot) != Memory) return 5;
    for (int i=0;i<8192;++i) if(static_cast<unsigned char*>(Memory)[i]!=0xa5)return 6;
    FPlatformTLS::FreeTlsSlot(Slot);
    FPlatformMemory::BinnedFreeToOS(Memory, 8192);
    const double First = FPlatformTime::Seconds(), Second = FPlatformTime::Seconds();
    if (First < 0 || Second < First) return 7;
    if (FPlatformString::Strlen(TEXT("WebCuda")) != 7 || FPlatformMath::Sqrt(16.0f) != 4.0f) return 8;
    using Block = FPlatformMemory::FPlatformVirtualMemoryBlock;
    if (FPlatformMemory::CanOverallocateVirtualMemory()) return 9;
    auto Region = Block::AllocateVirtual(3 * 65536 - 1, 131072);
    if (!Region.GetVirtualPointer() || reinterpret_cast<UPTRINT>(Region.GetVirtualPointer()) % 131072 || Region.GetActualSize() != 3 * 65536) return 10;
    auto* Bytes = static_cast<uint8*>(Region.GetVirtualPointer());
    Region.Commit(0, 2 * 65536);
    for (int i = 0; i < 2 * 65536; ++i) if (Bytes[i]) return 11;
    memset(Bytes, 0x7b, 2 * 65536);
    Region.Commit(0, 2 * 65536);
    Region.DecommitByPtr(Bytes + 65536, 65536);
    Region.CommitByPtr(Bytes + 65536, 65536);
    for (int i = 0; i < 65536; ++i) if (Bytes[i] != 0x7b || Bytes[i + 65536] != 0) return 12;
    Region.Commit(2 * 65536, 65536);
    for (int i = 2 * 65536; i < 3 * 65536; ++i) if (Bytes[i]) return 13;
    Region.FreeVirtual();
    if (Region.GetVirtualPointer() || Region.GetActualSize()) return 14;
    if (Block::AllocateVirtual(0).GetVirtualPointer() || Block::AllocateVirtual(SIZE_MAX).GetVirtualPointer() || Block::AllocateVirtual(65536, 65537).GetVirtualPointer()) return 15;
    FSemaphore Semaphore(1, 3);
    if (!Semaphore.TryAcquire() || Semaphore.TryAcquire()) return 16;
    Semaphore.Release(3);
    Semaphore.Acquire();
    if (!Semaphore.TryAcquire(FTimespan(10000)) || !Semaphore.TryAcquire() || Semaphore.TryAcquire()) return 17;
    if (Semaphore.TryAcquire(FTimespan(10000))) return 18;
    Semaphore.Release();
    if (!Semaphore.TryAcquire()) return 19;
    return 0;
}

extern "C" int platform_files_run()
{
    // HTML/JS writes the fixture into the same WasmFS tree before this call.
    unsigned char Data[4096];
    FILE* Input = fopen("/Content/fixture.bin", "rb");
    if (!Input) return 1;
    const size_t Read = fread(Data, 1, sizeof(Data), Input);
    fclose(Input);
    if (Read != sizeof(Data)) return 2;
    for (size_t I = 0; I < sizeof(Data); ++I) if (Data[I] != (I * 37 + 11) % 256) return 3;
    if (mkdir("/Saved", 0777)) return 4;
    FILE* Output = fopen("/Saved/result.tmp", "wb+");
    if (!Output) return 5;
    if (fwrite(Data, 1, sizeof(Data), Output) != sizeof(Data) || fflush(Output)) { fclose(Output); return 6; }
    if (fseek(Output, 1024, SEEK_SET) || fgetc(Output) != 11) { fclose(Output); return 7; }
    if (ftruncate(fileno(Output), 2048)) { fclose(Output); return 8; }
    fclose(Output);
    if (rename("/Saved/result.tmp", "/Saved/result.bin")) return 9;
    struct stat Info;
    if (stat("/Saved/result.bin", &Info) || Info.st_size != 2048) return 10;
    DIR* Directory = opendir("/Saved");
    if (!Directory) return 11;
    bool Found = false;
    while (dirent* Entry = readdir(Directory)) if (!strcmp(Entry->d_name, "result.bin")) Found = true;
    closedir(Directory);
    if (!Found || fopen("/Content/missing.bin", "rb")) return 12;
    return 0;
}

extern "C" void platform_semaphore_violation(int Case)
{
    FSemaphore Semaphore(Case == 2 ? -1 : 0, 1);
    if (Case == 0) Semaphore.Acquire();
    else Semaphore.Release(2);
}

extern "C" double platform_memory_benchmark(int Baseline)
{
    constexpr size_t Size = 16 * 1024 * 1024, Page = 65536;
    const double Start = FPlatformTime::Seconds();
    for (int I = 0; I < 64; ++I)
    {
        if (Baseline)
        {
            void* P = mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (P == MAP_FAILED) return -1;
            static_cast<volatile uint8*>(P)[0] = static_cast<uint8>(I);
            if (munmap(P, Size)) return -1;
        }
        else
        {
            auto Region = FPlatformMemory::FPlatformVirtualMemoryBlock::AllocateVirtual(Size);
            if (!Region.GetVirtualPointer()) return -1;
            Region.Commit(0, Page);
            static_cast<volatile uint8*>(Region.GetVirtualPointer())[0] = static_cast<uint8>(I);
            Region.FreeVirtual();
        }
    }
    return (FPlatformTime::Seconds() - Start) * 1000;
}

extern "C" const double* platform_chaos_transform(double X, double Y, double Z)
{
    static double Result[12];
    // These are the exact two vector-register constructors which failed with
    // the scalar platform configuration. Use the original Chaos implementation.
    Chaos::TRigidTransform<float, 3> F(MakeVectorRegisterFloat(0.f, 0.f, 1.f, 0.f), MakeVectorRegisterFloat(5.f, -2.f, 9.f, 0.f));
    F.SetScale3D(FVector3f(2, 3, 4));
    const FVector3f FP = F.TransformPosition(FVector3f(X, Y, Z));
    const FVector3f FI = F.InverseTransformPosition(FP);
    Chaos::TRigidTransform<double, 3> D(MakeVectorRegisterDouble(0., 0., 1., 0.), MakeVectorRegisterDouble(5., -2., 9., 0.));
    D.SetScale3D(FVector3d(2, 3, 4));
    const FVector3d DP = D.TransformPosition(FVector3d(X, Y, Z));
    const FVector3d DI = D.InverseTransformPosition(DP);
    Result[0] = FP.X; Result[1] = FP.Y; Result[2] = FP.Z;
    Result[3] = FI.X; Result[4] = FI.Y; Result[5] = FI.Z;
    Result[6] = DP.X; Result[7] = DP.Y; Result[8] = DP.Z;
    Result[9] = DI.X; Result[10] = DI.Y; Result[11] = DI.Z;
    return Result;
}
