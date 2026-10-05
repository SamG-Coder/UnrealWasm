#pragma once
#include "GenericPlatform/GenericPlatformApplicationMisc.h"

struct FWebCudaPlatformApplicationMisc : FGenericPlatformApplicationMisc
{
    static APPLICATIONCORE_API GenericApplication* CreateApplication();
    static APPLICATIONCORE_API bool IsThisApplicationForeground();
    static bool IsHighDPIAwarenessEnabled() { return false; } // logical canvas pixels
};
using FPlatformApplicationMisc=FWebCudaPlatformApplicationMisc;
