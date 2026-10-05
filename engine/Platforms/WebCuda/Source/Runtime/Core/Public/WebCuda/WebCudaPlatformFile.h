#pragma once
#include "Unix/UnixPlatformFile.h"

// Ordinary POSIX IO targets WasmFS. Native file mappings are deliberately
// disabled: they would copy file data into the Wasm heap, not map OS pages.
class FWebCudaPlatformFile : public FUnixPlatformFile
{
public:
    CORE_API FOpenMappedResult OpenMappedEx(const TCHAR* Filename, EOpenReadFlags Options = EOpenReadFlags::None, int64 MaximumSize = 0) override;
    CORE_API FOpenMappedResult OpenMappedEx2(const TCHAR* Filename, EOpenMappedFlags Options = EOpenMappedFlags::None, int64 MaximumSize = 0) override;
};
