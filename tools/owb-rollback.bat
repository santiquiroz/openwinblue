@echo off
:: OpenWinBlue Emergency Rollback
:: Uninstalls owb_a2dp.sys and re-enables the Windows default A2DP driver.
:: Run as Administrator.

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
for /f "tokens=*" %%i in ('pnputil /enum-drivers /class Bluetooth ^| findstr "owb_a2dp"') do (
    pnputil /delete-driver %%i /uninstall /force
)

echo Step 3: Re-enabling Windows default A2DP driver (btavchdt.sys)...
:: The inbox driver re-activates automatically once our driver is removed.
:: A reboot finalizes the switch.

echo.
echo Done. Please REBOOT your PC to complete the rollback.
echo After reboot, your Bluetooth headphones will use the Windows default driver.
echo.
pause
