#pragma once
#include "GenericPlatform/GenericPlatformHttp.h"

struct FWebCudaPlatformHttp : FGenericPlatformHttp
{
    // URL/header utilities remain Unreal's implementation; requests use Fetch.
    static HTTP_API IHttpRequest* ConstructRequest();
    static HTTP_API FHttpManager* CreatePlatformHttpManager();
};
using FPlatformHttp = FWebCudaPlatformHttp;
