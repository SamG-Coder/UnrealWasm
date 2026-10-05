#pragma once
#include "GenericPlatform/GenericPlatformProperties.h"
struct FWebCudaCookProperties : FGenericPlatformProperties {
    static const char* PlatformName() { return "WebCuda"; }
    static const char* IniPlatformName() { return "WebCuda"; }
    static bool HasEditorOnlyData() { return false; }
    static bool RequiresCookedData() { return true; }
    static bool SupportsMemoryMappedFiles() { return false; }
    static bool SupportsAutoSDK() { return false; }
    static bool SupportsLumenGI() { return true; }
};
