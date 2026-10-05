param([ValidateSet('Build','Assets','Run','Benchmark','Nanite','Lumen')][string]$Action='Run',[string]$Engine='D:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
$sampleRoot=Split-Path $PSScriptRoot -Parent
$project=Join-Path $sampleRoot 'UnrealBrowserSample.uproject'
switch($Action){
 'Build' { & "$Engine\Engine\Build\BatchFiles\Build.bat" UnrealBrowserSampleEditor Win64 Development "-Project=$project" -WaitMutex; if($LASTEXITCODE){throw 'Unreal build failed'} }
 'Assets' { & "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project "-ExecutePythonScript=$PSScriptRoot\create-assets.py" -unattended -nosplash -NullRHI -stdout -FullStdOutLogOutput; if($LASTEXITCODE){throw 'Asset creation failed'} }
 'Run' { & "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" $project /Game/Sample -game -windowed -ResX=1280 -ResY=720 -NoVSync -nosplash }
 'Benchmark' { $process=Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList @("`"$project`"",'/Game/Sample','-game','-windowed','-ResX=1280','-ResY=720','-NoVSync','-nosplash','-unattended','-SampleBenchmark') -WindowStyle Hidden -PassThru -Wait; if($process.ExitCode){throw "Native sample failed: $($process.ExitCode)"} }
 {$_ -in 'Nanite','Lumen'} {
  $visualization=if($Action -eq 'Nanite'){'r.Nanite.Visualize clusters'}else{'r.Lumen.Visualize 1'}
  $capture=Join-Path $sampleRoot "artifacts\native-$($Action.ToLower())"
  $process=Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList @("`"$project`"",'/Game/Sample','-game','-windowed','-ResX=1280','-ResY=720','-NoVSync','-nosplash','-unattended','-SampleBenchmark',"-SampleOutput=`"$capture`"","-ExecCmds=`"$visualization`"") -WindowStyle Hidden -PassThru -Wait
  if($process.ExitCode){throw "Visualization capture failed: $($process.ExitCode)"}
 }
}
