#pragma once
#include "GenericPlatform/GenericPlatformTime.h"
#include <emscripten/emscripten.h>
struct FWebCudaPlatformTime : FGenericPlatformTime
{
    static double InitTiming() { SecondsPerCycle = SecondsPerCycle64 = 0.000001; return Seconds(); }
    static double Seconds() { return emscripten_get_now() * 0.001; }
    static uint64 Cycles64() { return static_cast<uint64>(emscripten_get_now() * 1000.0); }
    static uint32 Cycles() { return static_cast<uint32>(Cycles64()); }
};
using FPlatformTime = FWebCudaPlatformTime;
