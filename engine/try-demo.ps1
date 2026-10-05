# Attempt the actual game target; do not launch the unrelated standalone fixture.
param([ValidateRange(1,8)][int]$MaxParallelActions=4)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$out=Join-Path $PSScriptRoot 'artifacts\demo-attempt'
New-Item -ItemType Directory -Force $out | Out-Null
$config=Join-Path $root 'Saved\UnrealBuildTool\BuildConfiguration.xml'
$existed=Test-Path -LiteralPath $config
$previous=if($existed){[IO.File]::ReadAllBytes($config)}else{$null}
$started=[DateTime]::UtcNow
@{startedUtc=$started.ToString('o');status='running';wrapperProcessId=$PID;map='/Game/Sample';target='UnrealBrowserSample WebCuda Development';freshGameWasm=$false;browserLaunched=$false;sceneRendered=$false;log="$out\build.log"} | ConvertTo-Json | Set-Content "$out\result.json"
try {
    # Bound diagnosis to several distinct failing translation units instead of
    # spending hours printing the same platform error across thousands of files.
    [xml]$xml=if($existed){[IO.File]::ReadAllText($config)}else{'<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration" />'}
    $ns=$xml.DocumentElement.NamespaceURI
    $parallel=$xml.DocumentElement.SelectSingleNode("*[local-name()='ParallelExecutor']")
    if(!$parallel){$parallel=$xml.CreateElement('ParallelExecutor',$ns);$null=$xml.DocumentElement.AppendChild($parallel)}
    $limit=$parallel.SelectSingleNode("*[local-name()='StopCompilationAfterNumErrors']")
    if(!$limit){$limit=$xml.CreateElement('StopCompilationAfterNumErrors',$ns);$null=$parallel.AppendChild($limit)}
    $limit.InnerText='8'
    $xml.Save($config)
    & "$PSScriptRoot\ubt.ps1" UnrealBrowserSample WebCuda Development "-Project=$root\UnrealBrowserSample.uproject" -WaitMutex -NoUBA -gather "-MaxParallelActions=$MaxParallelActions" *> "$out\build.log"
    $buildExit=$LASTEXITCODE
    $binary=Join-Path $root 'Binaries\WebCuda\UnrealBrowserSample.wasm'
    $freshBinary=(Test-Path -LiteralPath $binary) -and ((Get-Item -LiteralPath $binary).LastWriteTimeUtc -ge $started)
    @{startedUtc=$started.ToString('o');finishedUtc=[DateTime]::UtcNow.ToString('o');status=$(if($buildExit -eq 0){'succeeded'}else{'failed'});map='/Game/Sample';target='UnrealBrowserSample WebCuda Development';buildExitCode=$buildExit;freshGameWasm=$freshBinary;browserLaunched=$false;sceneRendered=$false;log="$out\build.log"} | ConvertTo-Json | Set-Content "$out\result.json"
    Get-Content "$out\build.log" -Tail 30
    exit $buildExit
} finally {
    if($existed){[IO.File]::WriteAllBytes($config,$previous)}else{Remove-Item -LiteralPath $config -Force}
}
