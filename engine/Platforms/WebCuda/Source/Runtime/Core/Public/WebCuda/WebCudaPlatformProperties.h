#pragma once
#include "GenericPlatform/GenericPlatformProperties.h"
struct FWebCudaPlatformProperties : FGenericPlatformProperties
{
    static const char* PlatformName() { return "WebCuda"; }
    static const char* IniPlatformName() { return "WebCuda"; }
    // AudioMixer reads this as an INI section; no settings UObject is needed.
    static constexpr const TCHAR* GetRuntimeSettingsClassName() { return TEXT("/Script/WebCudaTargetPlatform.WebCudaRuntimeSettings"); }
    static constexpr bool RequiresCookedData() { return true; }
};
using FPlatformProperties = FWebCudaPlatformProperties;
