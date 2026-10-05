@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
set "SDK=D:\UnrealEngine\UE_5.8\Engine\Source\Runtime\OodleDataCompression\Sdks\2.9.16"
pushd "%~dp0..\artifacts\oodle"
cl /nologo /std:c++17 /O2 /EHsc /MD "%~dp0oodle-native.cpp" /I"%SDK%\include" "%SDK%\lib\Win64\oo2core_win64.lib" /Fe:oodle-native.exe
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%
