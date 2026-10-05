// Wasm data-file backend for ICU 64.1. The callback signatures match UE's
// installed ICU headers. ICU's internationalization/data algorithms are intact.
#include "unicode/uclean.h"
#include "udatamem.h"
#include "umapfile.h"
#include "cmemory.h"
#include <mutex>

namespace {
std::mutex Mutex;
const void* Context=nullptr;
UDataFileOpenFn* Open=nullptr;
UDataFileCloseFn* Close=nullptr;
size_t Active=0;
struct Mapping {const void* Context; UDataFileCloseFn* Close; void* File; void* Contents;};
}

U_CAPI void U_EXPORT2 u_setDataFileFunctions(const void* ContextIn,UDataFileOpenFn* OpenIn,UDataFileCloseFn* CloseIn,UErrorCode* Status)
{
    if(!Status || U_FAILURE(*Status))return;
    if(!OpenIn || !CloseIn){*Status=U_ILLEGAL_ARGUMENT_ERROR;return;}
    std::lock_guard<std::mutex> Guard(Mutex);
    if(Active){*Status=U_INVALID_STATE_ERROR;return;}
    Context=ContextIn;Open=OpenIn;Close=CloseIn;
}

U_CFUNC UBool uprv_mapFile(UDataMemory* Data,const char* Path,UErrorCode* Status)
{
    if(!Status || U_FAILURE(*Status))return FALSE;
    UDataMemory_init(Data);
    Mapping Snapshot{}; UDataFileOpenFn* OpenSnapshot;
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        // No fallback outside Unreal's file manager: it owns path validation,
        // cached buffers and loose/pak file resolution.
        if(!Open)return FALSE;
        Snapshot.Context=Context;Snapshot.Close=Close;OpenSnapshot=Open;++Active;
    }
    Mapping* Map=static_cast<Mapping*>(uprv_malloc(sizeof(Mapping)));
    if(!Map){std::lock_guard<std::mutex> Guard(Mutex);--Active;*Status=U_MEMORY_ALLOCATION_ERROR;return FALSE;}
    *Map=Snapshot;
    const UBool Loaded=OpenSnapshot(Map->Context,&Map->File,&Map->Contents,Path);
    if(!Loaded || !Map->Contents)
    {
        if(Map->File || Map->Contents)Map->Close(Map->Context,Map->File,Map->Contents);
        uprv_free(Map);std::lock_guard<std::mutex> Guard(Mutex);--Active;return FALSE;
    }
    Data->map=Map;Data->mapAddr=Map->Contents;
    UDataMemory_setData(Data,Map->Contents);
    return TRUE;
}

U_CFUNC void uprv_unmapFile(UDataMemory* Data)
{
    if(!Data || !Data->map)return;
    Mapping* Map=static_cast<Mapping*>(Data->map);
    Map->Close(Map->Context,Map->File,Map->Contents);
    uprv_free(Map);
    Data->map=nullptr;Data->mapAddr=nullptr;Data->pHeader=nullptr;
    std::lock_guard<std::mutex> Guard(Mutex);--Active;
}
