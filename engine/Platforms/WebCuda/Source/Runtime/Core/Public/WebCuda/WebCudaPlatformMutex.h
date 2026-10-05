#pragma once
#include "HAL/PThreadsRecursiveMutex.h"
#include "HAL/PThreadsSharedMutex.h"
#include "GenericPlatform/GenericPlatformMutex.h"
namespace UE {
using FPlatformRecursiveMutex = FPThreadsRecursiveMutex;
using FPlatformSharedMutex = FPThreadsSharedMutex;
using FPlatformSystemWideMutex = FPlatformSystemWideMutexNotImplemented;
}
