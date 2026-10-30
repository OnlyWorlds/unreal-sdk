@echo off
rem Builds the plain C++ core (Plugin\OnlyWorlds\Core) and its test runner with MSVC, outside Unreal (no UBT, no editor).
rem Links the engine's own zlib, so the core inflates with the bytes a packaged game links.
rem   build.cmd            -> build\ow_tests.exe
rem   build.cmd twin       -> build\twin_run.exe (tools\twin_run.cpp: the lens and graph readers over a cases file)
setlocal
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "ZLIB=%UE_ROOT%\Engine\Source\ThirdParty\zlib\1.3"
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>nul || exit /b 2
cd /d "%~dp0"
if not exist build mkdir build
if /i "%~1"=="twin" (
  if not exist build\twin mkdir build\twin
  cl /nologo /std:c++17 /EHsc /O2 /MD /utf-8 /W4 /permissive- ^
     /I Plugin\OnlyWorlds\Core\Public /I "%ZLIB%\include" ^
     Plugin\OnlyWorlds\Core\Private\*.cpp tools\twin_run.cpp ^
     /Fo"build\twin\\" /Fe"build\twin_run.exe" ^
     /link "%ZLIB%\lib\Win64\Release\zlibstatic.lib" || exit /b 1
  exit /b 0
)
cl /nologo /std:c++17 /EHsc /O2 /MD /utf-8 /W4 /permissive- ^
   /I Plugin\OnlyWorlds\Core\Public /I "%ZLIB%\include" ^
   Plugin\OnlyWorlds\Core\Private\*.cpp tests\*.cpp ^
   /Fo"build\\" /Fe"build\ow_tests.exe" ^
   /link "%ZLIB%\lib\Win64\Release\zlibstatic.lib" || exit /b 1
endlocal
