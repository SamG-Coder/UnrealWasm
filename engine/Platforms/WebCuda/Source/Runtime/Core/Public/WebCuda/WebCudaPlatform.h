#pragma once
#include "Clang/ClangPlatform.h"

struct FWebCudaPlatformTypes : FGenericPlatformTypes
{
    using DWORD = unsigned int;
    using SIZE_T = __SIZE_TYPE__;
    // Match Unreal's Unix convention: SSIZE_T/PTRINT remain int64, while
    // SIZE_T follows the C ABI's unsigned long for allocator interfaces.
    using WIDECHAR = char16_t;
    using TCHAR = WIDECHAR;
};
using FPlatformTypes = FWebCudaPlatformTypes;

// Shared ICursor ABI, matching Unreal's Unix/Android rectangle. The browser
// cursor adapter and the engine's generic software cursor use the same type.
struct tagRECT { int left; int top; int right; int bottom; };
static_assert(sizeof(int) == 4);

#define PLATFORM_DESKTOP 0
#define UE_ARCH_NAME wasm64
#define PLATFORM_USE_GENERIC_LAUNCH_IMPLEMENTATION 1
#define PLATFORM_LITTLE_ENDIAN 1
#define PLATFORM_SEH_EXCEPTIONS_DISABLED 1
#define PLATFORM_SUPPORTS_UNALIGNED_LOADS 1
#define PLATFORM_COMPILER_DISTINGUISHES_INT_AND_LONG 1
#define PLATFORM_SUPPORTS_PRAGMA_PACK 1
#define PLATFORM_WCHAR_IS_4_BYTES 1
#define PLATFORM_TCHAR_IS_CHAR16 1
#define PLATFORM_USE_ANSI_POSIX_MALLOC 1
#define PLATFORM_IS_ANSI_MALLOC_THREADSAFE 1
#define PLATFORM_SUPPORTS_MIMALLOC 0
#define PLATFORM_SUPPORTS_MULTITHREADED_GC 0
#define PLATFORM_HAS_FENV_H 0
// Emscripten lowers existing SSE4.1 intrinsics to Wasm SIMD128. This does not
// claim an x86 CPU, AVX, FMA, or hardware reciprocal-estimate semantics.
#define PLATFORM_ENABLE_VECTORINTRINSICS 1
#define PLATFORM_ALWAYS_HAS_SSE4_1 1
#define PLATFORM_ALWAYS_HAS_AVX 0
#define PLATFORM_ALWAYS_HAS_AVX_2 0
#define PLATFORM_ALWAYS_HAS_FMA3 0
#define PLATFORM_SUPPORTS_VECTOR_CONTROL_REGISTERS 0
#define PLATFORM_SUPPORTS_BINDLESS_RENDERING 0
#define PLATFORM_SUPPORTS_MESH_SHADERS 0
#define PLATFORM_SUPPORTS_GEOMETRY_SHADERS 0
#define PLATFORM_CAN_SUPPORT_EDITORONLY_DATA 0
#define PLATFORM_USE_PTHREADS 1
#define PLATFORM_BREAK() __builtin_trap()
#define UE_DEBUG_BREAK_IMPL() __builtin_trap()
#define VARARGS
#define CDECL
#define STDCALL
#define FORCEINLINE inline __attribute__((always_inline))
#define FORCENOINLINE __attribute__((noinline))
#define FUNCTION_CHECK_RETURN_END __attribute__((warn_unused_result))
#define FUNCTION_NO_RETURN_END __attribute__((noreturn))
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT
#define GCC_PACK(n) __attribute__((packed,aligned(n)))
#define GCC_ALIGN(n) __attribute__((aligned(n)))
#define OPERATOR_NEW_THROW_SPEC
#define OPERATOR_DELETE_THROW_SPEC noexcept
#define OPERATOR_NEW_NOTHROW_SPEC noexcept
#define OPERATOR_DELETE_NOTHROW_SPEC noexcept
#define PRAGMA_DISABLE_OPTIMIZATION_ACTUAL _Pragma("clang optimize off")
#define PRAGMA_ENABLE_OPTIMIZATION_ACTUAL _Pragma("clang optimize on")
