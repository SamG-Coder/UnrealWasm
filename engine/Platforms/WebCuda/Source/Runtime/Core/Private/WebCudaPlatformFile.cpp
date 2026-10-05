#include "WebCuda/WebCudaPlatformFile.h"
#include "Async/MappedFileHandle.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <utime.h>
// The existing Unix source spells one timestamp cast with glibc's internal
// alias. Emscripten uses musl; keep this compatibility spelling local to this TU.
using __time_t = time_t;

// Compile the engine's existing Unix implementation against Emscripten libc.
#include "Unix/UnixPlatformFile.cpp"

IPlatformFile& IPlatformFile::GetPlatformPhysical()
{
    static FWebCudaPlatformFile Singleton;
    return Singleton;
}
FOpenMappedResult FWebCudaPlatformFile::OpenMappedEx(const TCHAR* Filename, EOpenReadFlags Options, int64 MaximumSize)
{
    return IPlatformFile::OpenMappedEx(Filename, Options, MaximumSize);
}
FOpenMappedResult FWebCudaPlatformFile::OpenMappedEx2(const TCHAR* Filename, EOpenMappedFlags Options, int64 MaximumSize)
{
    return IPlatformFile::OpenMappedEx2(Filename, Options, MaximumSize);
}
