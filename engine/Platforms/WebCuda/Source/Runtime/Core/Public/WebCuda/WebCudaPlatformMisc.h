#pragma once
#include "GenericPlatform/GenericPlatformMisc.h"
struct FWebCudaPlatformMisc : FGenericPlatformMisc
{
    static void MemoryBarrier() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
    static int32 NumberOfCores() { return 1; }
    static int32 NumberOfCoresIncludingHyperthreads() { return 1; }
};
using FPlatformMisc = FWebCudaPlatformMisc;
