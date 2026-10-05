#include "unicode/uclean.h"
#include "unicode/udata.h"
#include "unicode/putil.h"
#include "unicode/ures.h"
#include "unicode/unum.h"
#include "unicode/ubrk.h"
#include "unicode/ucol.h"
#include "unicode/ustring.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>

namespace {
int Opened=0,Closed=0,Rejected=0;
int Allocations=0;
void* U_CALLCONV Allocate(const void*,size_t Size){void* P=std::malloc(Size);if(P)++Allocations;return P;}
void* U_CALLCONV Reallocate(const void*,void* Old,size_t Size){
    if(!Old)return Allocate(nullptr,Size);
    if(!Size){std::free(Old);--Allocations;return nullptr;}
    return std::realloc(Old,Size);
}
void U_CALLCONV Free(const void*,void* P){if(P){--Allocations;std::free(P);}}
UBool U_CALLCONV Open(const void* Context,void** FileContext,void** Contents,const char* Path)
{
    *FileContext=nullptr;*Contents=nullptr;
    // Same path boundary as Unreal's callback. The host mounts these loose files.
    constexpr char Prefix[]="/Content/Internationalization/icudt64l/";
    if(Context!=&Opened || std::strncmp(Path,Prefix,sizeof(Prefix)-1)!=0){++Rejected;return false;}
    FILE* File=std::fopen(Path,"rb");if(!File)return false;
    std::fseek(File,0,SEEK_END);long Size=std::ftell(File);std::rewind(File);
    if(Size<32){std::fclose(File);return false;}
    void* Data=std::malloc(Size);
    if(!Data || std::fread(Data,1,Size,File)!=static_cast<size_t>(Size)){std::free(Data);std::fclose(File);return false;}
    std::fclose(File);*FileContext=Data;*Contents=Data;++Opened;return true;
}
void U_CALLCONV Close(const void* Context,void* FileContext,void* Contents)
{
    if(Context!=&Opened || FileContext!=Contents)std::abort();
    std::free(Contents);++Closed;
}
}
extern "C" EMSCRIPTEN_KEEPALIVE int icu_test()
{
    Opened=Closed=Rejected=0;
    for(int Pass=0;Pass<2;++Pass)
    {
        UErrorCode Status=U_ZERO_ERROR;
        u_setMemoryFunctions(nullptr,Allocate,Reallocate,Free,&Status);
        u_setDataDirectory("/Content/Internationalization");
        udata_setFileAccess(UDATA_FILES_FIRST,&Status);
        u_setDataFileFunctions(&Opened,Open,Close,&Status);
        u_init(&Status);
        if(U_FAILURE(Status)){std::printf("init: %s\n",u_errorName(Status));return 1;}
        UErrorCode ActiveStatus=U_ZERO_ERROR;
        u_setDataFileFunctions(&Opened,Open,Close,&ActiveStatus);
        if(ActiveStatus!=U_INVALID_STATE_ERROR)return 2;
        UResourceBundle* Resource=ures_open(nullptr,"en_US_POSIX",&Status);
        if(U_FAILURE(Status)||!Resource)return 3;
        ures_close(Resource);
        Status=U_ZERO_ERROR;
        UNumberFormat* Number=unum_open(UNUM_DECIMAL,nullptr,0,"en_US",nullptr,&Status);
        UChar Text[64];int Length=unum_formatDouble(Number,12345.5,Text,64,nullptr,&Status);
        char UTF8[128];u_strToUTF8(UTF8,128,nullptr,Text,Length,&Status);
        unum_close(Number);
        if(U_FAILURE(Status)||std::strcmp(UTF8,"12,345.5"))return 4;
        Status=U_ZERO_ERROR;
        const UChar Words[]={ 'H','e','l','l','o',' ','w','o','r','l','d','!',0};
        UBreakIterator* Break=ubrk_open(UBRK_WORD,"en_US",Words,12,&Status);
        if(U_FAILURE(Status)||!Break)return 5;
        int Count=0;for(int P=ubrk_first(Break);P!=UBRK_DONE;P=ubrk_next(Break))++Count;
        ubrk_close(Break);if(Count!=5)return 6;
        Status=U_ZERO_ERROR;
        UCollator* Collator=ucol_open("en_US",&Status);
        const UChar A[]={'a',0},B[]={'b',0};
        if(U_FAILURE(Status)||!Collator)return 7;
        bool Sorted=ucol_strcoll(Collator,A,1,B,1)==UCOL_LESS;
        ucol_close(Collator);if(!Sorted)return 8;
        u_cleanup();
        if(Opened!=Closed)return 9;
        if(Allocations!=0)return 12;
    }
    UErrorCode Status=U_ZERO_ERROR;
    u_setDataFileFunctions(nullptr,nullptr,Close,&Status);
    if(Status!=U_ILLEGAL_ARGUMENT_ERROR)return 10;
    std::printf("ICU %s: %d callback opens, %d closes, two init/cleanup cycles\n",U_ICU_VERSION,Opened,Closed);
    return Opened>0?0:11;
}
extern "C" EMSCRIPTEN_KEEPALIVE int icu_test_missing()
{
    UErrorCode Status=U_ZERO_ERROR;
    u_setDataDirectory("/missing");
    udata_setFileAccess(UDATA_FILES_FIRST,&Status);
    u_setDataFileFunctions(&Opened,Open,Close,&Status);
    u_init(&Status);
    const bool Failed=U_FAILURE(Status);
    u_cleanup();
    return Failed && Opened==Closed ? 0:1;
}
extern "C" EMSCRIPTEN_KEEPALIVE int icu_opened(){return Opened;}
extern "C" EMSCRIPTEN_KEEPALIVE int icu_closed(){return Closed;}
