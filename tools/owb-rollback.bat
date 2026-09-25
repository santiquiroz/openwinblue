@echo off
:: OpenWinBlue Emergency Rollback
:: Uninstalls owb_a2dp.sys and re-enables the Windows default A2DP driver.
:: Run as Administrator.
::
:: owb-rollback.bat --list-from <file>
::   Prints the published names (oemNN.inf) of owb_a2dp.inf found in a saved
::   "pnputil /enum-drivers" output and exits. Needs no admin and changes nothing.

setlocal EnableDelayedExpansion
set "OWB_INF=owb_a2dp.inf"

if /i "%~1"=="--list-from" goto :list_from
if not "%~1"=="" (
    echo Usage: %~nx0 [--list-from ^<pnputil-enum-drivers-output.txt^>]
    exit /b 2
)

echo OpenWinBlue — Emergency Rollback
echo ===================================
echo.

:: Check for admin rights
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click and select "Run as administrator".
    pause
    exit /b 1
)

echo Step 1: Stopping OpenWinBlue service...
sc stop owb-service 2>nul
sc delete owb-service 2>nul

echo Step 2: Removing OpenWinBlue driver...
:: pnputil /delete-driver only accepts the published name (oemNN.inf), never owb_a2dp.inf.
set "OWB_SOURCE=pnputil /enum-drivers"
call :find_published
if not defined PUBLISHED (
    echo No %OWB_INF% package found in the driver store. Nothing to remove.
)
for %%p in (!PUBLISHED!) do (
    echo Deleting %%p ^(%OWB_INF%^)...
    pnputil /delete-driver %%p /uninstall /force
)

echo Step 3: Re-enabling Windows default A2DP driver (btavchdt.sys)...
:: The inbox driver re-activates automatically once our driver is removed.
:: A reboot finalizes the switch.

echo.
echo Done. Please REBOOT your PC to complete the rollback.
echo After reboot, your Bluetooth headphones will use the Windows default driver.
echo.
pause
exit /b 0

:list_from
if "%~2"=="" (
    echo Usage: %~nx0 --list-from ^<pnputil-enum-drivers-output.txt^>
    exit /b 2
)
if not exist "%~2" (
    echo File not found: %~2
    exit /b 2
)
set "OWB_SOURCE=type "%~2""
call :find_published
for %%p in (!PUBLISHED!) do echo %%p
exit /b 0

:: Sets PUBLISHED to the oemNN.inf of every block of %OWB_SOURCE% output whose original name is %OWB_INF%.
:: Labels are localized ("Published Name" / "Nombre publicado"), so only values are read:
:: the published name is the first line of each block, before the original name.
:find_published
set "PUBLISHED="
set "LAST_OEM="
for /f "usebackq tokens=1,* delims=:" %%a in (`%OWB_SOURCE%`) do (
    for /f "tokens=1" %%v in ("%%b") do (
        set "VALUE=%%v"
        if /i "!VALUE:~0,3!"=="oem" if /i "!VALUE:~-4!"==".inf" set "LAST_OEM=!VALUE!"
        if /i "!VALUE!"=="%OWB_INF%" if defined LAST_OEM set "PUBLISHED=!PUBLISHED! !LAST_OEM!"
    )
)
exit /b 0
