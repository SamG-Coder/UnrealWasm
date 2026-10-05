param([string]$Engine='D:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
$source=Join-Path $Engine 'Engine\Source\Programs\UnrealBuildTool\System\FileHashCache.cs'
$destination=Join-Path $PSScriptRoot 'artifacts\ubt-source\FileHashCache.cs'
$content=Get-Content -LiteralPath $source -Raw
# UE 5.8 can save while asynchronous generated-file hashing adds entries. The
# ICollection List constructor races Count against CopyTo. Snapshot atomically,
# then persist that snapshot's count so the cache also remains well formed.
$changes=@{
 'List<KeyValuePair<FileItem, FileEntry>> files = new(partition.Files);'='List<KeyValuePair<FileItem, FileEntry>> files = new(partition.Files.ToArray());'
 'writer.Write(partition.Files.Count);'='writer.Write(files.Count);'
}
foreach($old in $changes.Keys){
 if(([regex]::Matches($content,[regex]::Escape($old))).Count -ne 1){throw "UBT source changed; review FileHashCache override: $old"}
 $content=$content.Replace($old,$changes[$old])
}
New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
if(!(Test-Path -LiteralPath $destination) -or (Get-Content -LiteralPath $destination -Raw) -ne $content){
 [System.IO.File]::WriteAllText($destination,$content)
}
