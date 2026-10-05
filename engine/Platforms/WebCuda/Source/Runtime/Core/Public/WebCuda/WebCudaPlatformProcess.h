#pragma once
#include "GenericPlatform/GenericPlatformProcess.h"
#include <sched.h>
// Browsers do not expose native child processes. An empty handle is explicitly
// invalid, rather than pretending a process was launched.
struct FProcHandle
{
    constexpr bool IsValid() const { return false; }
    constexpr void Reset() {}
    constexpr void* Get() const { return nullptr; }
};
struct FWebCudaPlatformProcess : FGenericPlatformProcess
{
    static bool SupportsMultithreading() { return false; }
    static void Yield() { sched_yield(); }
};
using FPlatformProcess = FWebCudaPlatformProcess;
