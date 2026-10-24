@echo off
rem Builds the plain C++ core and its test runner with MSVC, outside Unreal (no UBT, no editor).
rem Links the engine's own zlib, so the core inflates with the bytes a packaged game links.
rem   build.cmd            -> build\ow_tests.exe
setlocal
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "ZLIB=%UE_ROOT%\Engine\Source\ThirdParty\zlib\1.3"
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>nul || exit /b 2
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /O2 /MD /utf-8 /W4 /permissive- ^
   /I Core\Public /I "%ZLIB%\include" ^
   Core\Private\*.cpp tests\*.cpp ^
   /Fo"build\\" /Fe"build\ow_tests.exe" ^
   /link "%ZLIB%\lib\Win64\Release\zlibstatic.lib" || exit /b 1
endlocal
