#pragma once
#include "GenericPlatform/GenericPlatformTLS.h"
#include <pthread.h>
struct FWebCudaPlatformTLS : FGenericPlatformTLS
{
    // This initial target runs UE on one thread; enabling workers requires
    // explicit thread-ID allocation instead of truncating a Wasm64 pthread_t.
    static uint32 GetCurrentThreadId() { return 1; }
    static void ClearThreadIdTLS() {}
    static uint32 AllocTlsSlot() { pthread_key_t Key; return pthread_key_create(&Key, nullptr) == 0 ? Key : InvalidTlsSlot; }
    static void FreeTlsSlot(uint32 Key) { pthread_key_delete(Key); }
    static void SetTlsValue(uint32 Key, void* Value) { pthread_setspecific(Key, Value); }
    static void* GetTlsValue(uint32 Key) { return pthread_getspecific(Key); }
};
using FPlatformTLS = FWebCudaPlatformTLS;
