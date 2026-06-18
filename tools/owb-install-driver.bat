@echo off
set LOG=C:\owb-debug.log
set DIR=%~dp0

echo === owb-install-driver.bat START === >> %LOG% 2>&1
echo ScriptDir: %DIR% >> %LOG% 2>&1
echo. >> %LOG% 2>&1

echo [1] certutil -addstore root >> %LOG% 2>&1
certutil.exe -addstore root "%DIR%owb_test.cer" >> %LOG% 2>&1
echo certutil root exit: %ERRORLEVEL% >> %LOG% 2>&1
echo. >> %LOG% 2>&1

echo [2] certutil -addstore TrustedPublisher >> %LOG% 2>&1
certutil.exe -addstore TrustedPublisher "%DIR%owb_test.cer" >> %LOG% 2>&1
echo certutil TrustedPublisher exit: %ERRORLEVEL% >> %LOG% 2>&1
echo. >> %LOG% 2>&1

echo [3] pnputil /add-driver >> %LOG% 2>&1
pnputil.exe /add-driver "%DIR%owb_a2dp.inf" /install >> %LOG% 2>&1
set PNPUTIL_EXIT=%ERRORLEVEL%
echo pnputil exit: %PNPUTIL_EXIT% >> %LOG% 2>&1
echo. >> %LOG% 2>&1

echo === owb-install-driver.bat END === >> %LOG% 2>&1
exit /b %PNPUTIL_EXIT%
