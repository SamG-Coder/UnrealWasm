@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++20 /O2 "%~dp0native-reference.cpp" /Fe:"%~dp0..\artifacts\native-reference.exe" /Fo:"%~dp0..\artifacts\native-reference.obj" /link d3d11.lib d3dcompiler.lib
