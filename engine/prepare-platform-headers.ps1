param([string]$Engine='D:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
function Write-ChangedText([string]$Path,[string]$Text) {
    if (!(Test-Path -LiteralPath $Path) -or [IO.File]::ReadAllText($Path) -cne $Text) {
        [IO.File]::WriteAllText($Path,$Text)
    }
}
$source=Join-Path $Engine 'Engine\Source\Runtime\Core\Public\GenericPlatform\GenericPlatformProcess.h'
$out=Join-Path $PSScriptRoot 'artifacts\platform-headers'
New-Item -ItemType Directory -Force $out | Out-Null
$original=[IO.File]::ReadAllText($source)
$needle='#' + "`t`t`t" + 'error "Not implemented on this platform"'
if(([regex]::Matches($original,[regex]::Escape($needle))).Count -ne 1){throw 'GenericPlatformProcess yield source changed; review the adapter'}
$replacement=@'
#if PLATFORM_WEBCUDA
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
#else
#error "Not implemented on this platform"
#endif
'@
New-Item -ItemType Directory -Force (Join-Path $out 'GenericPlatform') | Out-Null
Write-ChangedText (Join-Path $out 'GenericPlatform\GenericPlatformProcess.h') $original.Replace($needle,$replacement)
@{source=$source;sourceSHA256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;change='Allow compiler-fence spin hint in generic YieldCycles for Wasm';originalSourceEdited=$false} | ConvertTo-Json | Set-Content (Join-Path $out 'manifest.json')
$queueSource=Join-Path $Engine 'Engine\Source\ThirdParty\AtomicQueue\AtomicQueue.h'
$queue=[IO.File]::ReadAllText($queueSource)
$queueNeedle='#' + "`t" + 'error Implement this for your platform/architecture'
if(([regex]::Matches($queue,[regex]::Escape($queueNeedle))).Count -ne 1){throw 'AtomicQueue spin hint source changed; review the adapter'}
$queueReplacement=@'
#if PLATFORM_WEBCUDA
        // No Wasm CPU pause instruction. This is a compiler spin hint only;
        // it neither yields to the browser event loop nor provides workers.
        std::atomic_signal_fence(std::memory_order_seq_cst);
#else
#error Implement this for your platform/architecture
#endif
'@
$queue=$queue.Replace($queueNeedle,$queueReplacement)
# Recent Clang requires a template argument list after this disambiguator.
# Empty <> preserves deduction and the existing overloads and queue algorithm.
foreach($method in @('do_pop_any','do_push_any')) {
    $call="Base::template $method("
    if(([regex]::Matches($queue,[regex]::Escape($call))).Count -ne 2){throw "AtomicQueue $method call sites changed; review Clang adapter"}
    $queue=$queue.Replace($call,"Base::template $method<>(")
}
Write-ChangedText (Join-Path $out 'AtomicQueue.h') $queue
@{source=$queueSource;sourceSHA256=(Get-FileHash -LiteralPath $queueSource -Algorithm SHA256).Hash;change='Wasm compiler-fence spin hint and four Clang template-call syntax fixes; queue algorithms unchanged';originalSourceEdited=$false} | ConvertTo-Json | Set-Content (Join-Path $out 'atomic-queue-manifest.json')

# UE declares these allocator instantiations noinline, but marks their member
# definition always_inline. Clang 23 rejects the contradictory attributes.
# Preserve the explicit noinline contract for this out-of-line allocation path.
$memorySource=Join-Path $Engine 'Engine\Source\Runtime\Core\Private\HAL\LLM\MemoryUtils.h'
$memory=[IO.File]::ReadAllText($memorySource)
$memoryNeedle='FORCEINLINE void ResizeAllocation(SizeType CurrentNum, SizeType NewMax, SIZE_T NumBytesPerElement)'
if(([regex]::Matches($memory,[regex]::Escape($memoryNeedle))).Count -ne 1){throw 'LLM allocator source changed; review attribute adapter'}
New-Item -ItemType Directory -Force (Join-Path $out 'HAL\LLM') | Out-Null
Write-ChangedText (Join-Path $out 'HAL\LLM\MemoryUtils.h') $memory.Replace($memoryNeedle,$memoryNeedle.Replace('FORCEINLINE','FORCENOINLINE'))
@{source=$memorySource;sourceSHA256=(Get-FileHash -LiteralPath $memorySource -Algorithm SHA256).Hash;change='Honor explicit noinline allocator instantiations without conflicting always_inline';originalSourceEdited=$false} | ConvertTo-Json | Set-Content (Join-Path $out 'llm-allocator-manifest.json')

# Wasm64 is LP64 like Unix; enable UE's existing size_t overload, retaining
# its exact uint64 conversion and avoiding ambiguous numeric overloads.
$writerSource=Join-Path $Engine 'Engine\Source\Runtime\Core\Public\Serialization\CompactBinaryWriter.h'
$writer=[IO.File]::ReadAllText($writerSource)
$writerNeedle='#if PLATFORM_LINUX || PLATFORM_APPLE'
if(([regex]::Matches($writer,[regex]::Escape($writerNeedle))).Count -ne 1){throw 'CompactBinaryWriter size_t guard changed; review adapter'}
New-Item -ItemType Directory -Force (Join-Path $out 'Serialization') | Out-Null
Write-ChangedText (Join-Path $out 'Serialization\CompactBinaryWriter.h') $writer.Replace($writerNeedle,$writerNeedle+' || PLATFORM_WEBCUDA')
@{source=$writerSource;sourceSHA256=(Get-FileHash -LiteralPath $writerSource -Algorithm SHA256).Hash;change='Enable existing LP64 size_t serialization overload for Wasm64';originalSourceEdited=$false} | ConvertTo-Json | Set-Content (Join-Path $out 'compact-binary-manifest.json')
