#pragma once
#include "CoreTypes.h"
#include "Misc/Timespan.h"
#include <cstdio>
#include <cstdlib>

// Emscripten's non-pthread sem_wait/sem_trywait/sem_post are no-op stubs.
// Keep actual permit accounting for the current -nothreading export instead.
// There is no other executing worker that could release an unavailable permit:
// TryAcquire reports unavailable immediately (including finite timeouts), while
// an exhausted blocking Acquire explicitly fails instead of freezing the tab.
#ifdef __EMSCRIPTEN_PTHREADS__
#error Replace the single-thread WebCuda semaphore with a tested worker semaphore before enabling pthreads.
#endif
class FWebCudaSemaphore
{
public:
    FWebCudaSemaphore(int32 InitialCount, int32 MaxCount) : Available(InitialCount), Maximum(MaxCount)
    {
        if (InitialCount < 0 || MaxCount <= 0 || InitialCount > MaxCount) Fail("invalid semaphore count");
    }
    FWebCudaSemaphore(const FWebCudaSemaphore&) = delete;
    FWebCudaSemaphore& operator=(const FWebCudaSemaphore&) = delete;

    void Acquire()
    {
        if (!TryAcquire()) Fail("blocking semaphore wait requires WebCuda worker support");
    }
    bool TryAcquire(FTimespan Timeout = FTimespan(0))
    {
        (void)Timeout;
        if (!Available) return false;
        --Available;
        return true;
    }
    void Release(int32 Count = 1)
    {
        if (Count <= 0 || Count > Maximum - Available) Fail("invalid semaphore release count");
        Available += Count;
    }
private:
    [[noreturn]] static void Fail(const char* Operation) { std::fprintf(stderr,"WebCuda: %s\n",Operation); std::abort(); }
    int32 Available, Maximum;
};
using FSemaphore = FWebCudaSemaphore;
