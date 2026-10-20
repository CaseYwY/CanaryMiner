@echo off
rem CanaryMiner, editable launcher. Edit the settings below, then double-click this file.
rem The miner restarts itself 5 seconds after it stops. To quit, press Ctrl+C and then Y.
rem GPU clocks, memory, power limit and fan: set them with MSI Afterburner (or a similar tool) before starting.
rem CanaryMiner does not change GPU clocks on Windows; it mines with whatever your tuning tool has applied.
title CanaryMiner

rem ---- settings: edit these ----
rem Your public PRL payout address. The launcher refuses to start until you change it.
set "WALLET=PUT_YOUR_PRL_ADDRESS_HERE"
rem Rig name shown by the pool.
set "WORKER=rig1"
rem Pool: kryptex, herominers, f2pool, or your own HOST:PORT / stratum+tcp://HOST:PORT.
rem For f2pool, set WALLET to your F2Pool account name (your PRL address is set in your F2Pool account).
set "POOL=kryptex"
rem Developer donation: on (1%) or off.
set "DONATION=on"
rem Any extra options, for example --gpu-id 0 or --api-enable. Leave empty for none.
set "EXTRA="
rem ---- end of settings ----

rem Turn on colours in this console window (the HKCU setting is per user and harmless if already set).
reg add HKCU\Console /v VirtualTerminalLevel /t REG_DWORD /d 1 /f >nul 2>&1

if not exist "%~dp0canary-miner.exe" (
    echo canary-miner.exe was not found next to this launcher. Extract the whole zip and try again.
    pause
    exit /b 1
)
if "%WALLET%"=="PUT_YOUR_PRL_ADDRESS_HERE" (
    echo Edit WALLET in start-advanced.bat to your PRL address, then run it again.
    pause
    exit /b 1
)

set "DONATION_FLAG="
if /i "%DONATION%"=="off" set "DONATION_FLAG=--no-donation"
echo CanaryMiner: pool %POOL%, worker %WORKER%, donation %DONATION%.

:run
"%~dp0canary-miner.exe" --pool %POOL% --wallet %WALLET% --worker %WORKER% %DONATION_FLAG% %EXTRA%
echo.
echo The miner stopped (exit code %ERRORLEVEL%). It restarts in 5 seconds. Press Ctrl+C and then Y to quit.
timeout /t 5 >nul
goto run
