@echo off
rem ewdk-env.cmd — entorno EWDK (amd64) para compilar el driver KMDF.
rem Uso: tools\ewdk-env.cmd <comando...>
rem   tools\ewdk-env.cmd msbuild driver\owb_a2dp.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off
rem
rem Requiere el ISO del EWDK build 26100. Si no esta montado lo monta desde
rem %EWDK_ISO% (default: %USERPROFILE%\Downloads\EWDK_26100.iso).
rem El BuildTools instalado (VS 18) NO trae el toolset WindowsKernelModeDriver10.0
rem (el WDK no integra su VSIX en BuildTools) — por eso el driver se compila con EWDK.

set "EWDK_DRIVE="
for %%d in (D E F G H I J K) do if exist "%%d:\BuildEnv\SetupBuildEnv.cmd" set "EWDK_DRIVE=%%d:"
if defined EWDK_DRIVE goto havedrive

if not defined EWDK_ISO set "EWDK_ISO=%USERPROFILE%\Downloads\EWDK_26100.iso"
if exist "%EWDK_ISO%" goto mount
echo ERROR: EWDK no montado y no existe "%EWDK_ISO%". Define EWDK_ISO o monta el ISO.
exit /b 1

:mount
echo Montando EWDK: %EWDK_ISO%
for /f %%l in ('powershell -NoProfile -Command "(Mount-DiskImage -ImagePath $env:EWDK_ISO -PassThru | Get-Volume).DriveLetter"') do set "EWDK_DRIVE=%%l:"
if not defined EWDK_DRIVE echo ERROR: no se pudo montar el EWDK. && exit /b 1

:havedrive
call %EWDK_DRIVE%\BuildEnv\SetupBuildEnv.cmd amd64 >nul

rem SetupBuildEnv usa vsdevcmd -winsdk=none: agregar INCLUDE/LIB del SDK del EWDK
rem (sin esto, user-mode linkea sin kernel32.lib -> LNK1104).
set "SDKINC=%EWDK_DRIVE%\Program Files\Windows Kits\10\Include\10.0.26100.0"
set "SDKLIB=%EWDK_DRIVE%\Program Files\Windows Kits\10\Lib\10.0.26100.0"
set "INCLUDE=%INCLUDE%;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared;%SDKINC%\winrt"
set "LIB=%LIB%;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64"
%*
