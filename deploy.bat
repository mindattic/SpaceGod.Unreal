@echo off
rem Asks first: 1) Deploy & Run packages DebugGame and Shipping into R:\MindAttic\Apps\SpaceGod.Unreal\{Debug,Release}
rem and starts the Release copy (run.ps1 -Redeploy -RunRelease); 2) Run starts the Release copy without deploying
rem (deploying first if there is none). "deploy.bat --deploy" skips the menu and only deploys (the /deploy hook).
rem run.bat beside it opens run.ps1's full menu (1 Redeploy, 2 Run Debug, 3 Run Release).
setlocal
title SpaceGod
set "EXE=R:\MindAttic\Apps\SpaceGod.Unreal\Release\SpaceGod.exe"
set "STEPS=-Redeploy -RunRelease"
if /i "%~1"=="--deploy" (
    set "STEPS=-Redeploy"
    goto deploy
)

echo.
echo   SpaceGod
echo     1^) Deploy ^& Run
echo     2^) Run
echo.
choice /c 12 /n /m "  Choose 1 or 2: "
if errorlevel 2 goto run

:deploy
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %STEPS%
if errorlevel 1 (
    echo.
    echo   Deploy failed. See the output above.
    pause
    exit /b 1
)
timeout /t 5
exit /b 0

:run
if not exist "%EXE%" (
    echo.
    echo   SpaceGod is not deployed yet ^(%EXE% is missing^) - deploying first.
    goto deploy
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" -RunRelease
if errorlevel 1 (
    echo.
    echo   Run failed. See the output above.
    pause
    exit /b 1
)
exit /b 0
