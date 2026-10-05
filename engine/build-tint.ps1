param([string]$ChromiumRoot='D:\ChromiumRTXCuda\src',[ValidateRange(1,4)][int]$MaxParallelActions=2)
$ErrorActionPreference='Stop'
$cmake='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$source=Join-Path $ChromiumRoot 'third_party\dawn'
$output=Join-Path $PSScriptRoot 'artifacts\tint-cli'
New-Item -ItemType Directory -Force $output | Out-Null
@{status='configuring';wrapperProcessId=$PID;startedUtc=[DateTime]::UtcNow.ToString('o');source=$source;sourceEdited=$false} | ConvertTo-Json | Set-Content "$output/status.json"
$args=@('-S',$source,'-B',$output,'-G','Visual Studio 18 2026','-A','x64',
    '-DDAWN_BUILD_MONOLITHIC_LIBRARY=OFF','-DDAWN_BUILD_PROTOBUF=OFF',
    '-DDAWN_BUILD_TESTS=OFF','-DDAWN_BUILD_SAMPLES=OFF','-DDAWN_USE_GLFW=OFF',
    '-DTINT_BUILD_TESTS=OFF','-DTINT_BUILD_CMD_TOOLS=ON','-DTINT_BUILD_SPV_READER=ON',
    '-DTINT_BUILD_SPV_WRITER=OFF','-DTINT_BUILD_WGSL_READER=ON','-DTINT_BUILD_WGSL_WRITER=ON',
    '-DTINT_BUILD_HLSL_WRITER=OFF','-DTINT_BUILD_GLSL_WRITER=OFF','-DTINT_BUILD_GLSL_VALIDATOR=OFF','-DTINT_BUILD_MSL_WRITER=OFF',
    '-DTINT_BUILD_IR_BINARY=OFF','-DTINT_ENABLE_IR_VALIDATION_ASSERTS=ON',
    "-DPython3_EXECUTABLE=$PSScriptRoot\tools\emsdk\python\3.13.3_64bit\python.exe",
    "-DDAWN_ABSEIL_DIR=$ChromiumRoot/third_party/abseil-cpp",
    "-DDAWN_SPIRV_TOOLS_DIR=$ChromiumRoot/third_party/spirv-tools/src",
    "-DDAWN_SPIRV_HEADERS_DIR=$ChromiumRoot/third_party/spirv-headers/src")
foreach($backend in @('D3D11','D3D12','METAL','NULL','DESKTOP_GL','OPENGLES','VULKAN')) { $args+="-DDAWN_ENABLE_$backend=OFF" }
& $cmake @args *> "$output/configure.log"
if($LASTEXITCODE){
    @{status='failed';phase='configure';exitCode=$LASTEXITCODE;completedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content "$output/status.json"
    Get-Content "$output/configure.log" -Tail 35; throw 'Tint configure failed'
}
@{status='building';wrapperProcessId=$PID;startedUtc=[DateTime]::UtcNow.ToString('o');source=$source;sourceEdited=$false} | ConvertTo-Json | Set-Content "$output/status.json"
& $cmake --build $output --config Release --target tint_cmd_tint_cmd --parallel $MaxParallelActions *> "$output/build.log"
$code=$LASTEXITCODE
@{status=$(if($code){'failed'}else{'succeeded'});buildExitCode=$code;completedUtc=[DateTime]::UtcNow.ToString('o');source=$source;sourceEdited=$false;compiler=(Join-Path $output 'Release\tint.exe')} | ConvertTo-Json | Set-Content "$output/status.json"
if($code){Get-Content "$output/build.log" -Tail 30; throw "Tint build failed: $code"}
Write-Output "Built isolated Tint CLI: $output\Release\tint.exe"
