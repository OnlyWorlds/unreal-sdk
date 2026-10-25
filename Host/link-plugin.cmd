@echo off
rem Links the repository's root, which is the plugin, into the host as Plugins\OnlyWorlds: a directory junction (no
rem administrator needed, not stored in git). Run once after a clone. UBT finds a project's plugins only in the
rem subfolders of Plugins\ (and of each AdditionalPluginDirectories entry), never in the folder named itself, so the
rem host cannot reach the root it sits in by "../".
setlocal
cd /d "%~dp0"
if not exist Plugins mkdir Plugins
if exist Plugins\OnlyWorlds\OnlyWorlds.uplugin (
  echo Plugins\OnlyWorlds already links the plugin.
  exit /b 0
)
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
mklink /J Plugins\OnlyWorlds "%ROOT%" || exit /b 1
endlocal
