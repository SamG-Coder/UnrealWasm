#include "CoreMinimal.h"
#include "WebCuda/WebCudaBrowserHost.h"

extern int32 GuardedMain(const TCHAR* CommandLine);
extern void EngineTick();
extern void EngineExit();

namespace {
bool TickBrowserFrame(double, void*)
{
    FTaskTagScope Scope(ETaskTag::EGameThread);
    if (!IsEngineExitRequested()) EngineTick();
    if (!IsEngineExitRequested()) return true;
    EngineExit();
    WebCudaBrowser::Shutdown();
    return false;
}
}

// The JS host mounts cooked files before calling main. UE's existing embedded
// launch path initializes the original engine without blocking the browser in
// its native while-loop; requestAnimationFrame then drives the original Tick.
int main(int Argc, char** Argv)
{
    FString CommandLine;
    for (int Index=1; Index<Argc; ++Index)
    {
        FString Argument=UTF8_TO_TCHAR(Argv[Index]);
        Argument.ReplaceInline(TEXT("\""), TEXT("\\\""));
        CommandLine += TEXT("\"") + Argument + TEXT("\" ");
    }
    // Some engine services (including the legacy HTTP timer thread) consult
    // FGenericPlatformProcess directly. Keep those on UE's existing fake-thread
    // path, consistently with this platform's SupportsMultithreading()==false.
    CommandLine += TEXT(" -nothreading");
    if (!WebCudaBrowser::Initialize()) return 1;
    GUELibraryOverrideSettings.bIsEmbedded=true;
    const int32 Result=GuardedMain(*CommandLine);
    if (Result != 0 || IsEngineExitRequested() || !WebCudaBrowser::StartFrames(TickBrowserFrame,nullptr))
    {
        FTaskTagScope Scope(ETaskTag::EGameThread);
        EngineExit();
        WebCudaBrowser::Shutdown();
        return Result != 0 ? Result : 1;
    }
    return 0;
}
