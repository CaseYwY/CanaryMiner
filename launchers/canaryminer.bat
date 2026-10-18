@echo off
rem Argument pass-through only. Everything else (config, devices, fee, shutdown, stats) is the native binary's job.
"%~dp0canary-miner.exe" %*
exit /b %ERRORLEVEL%
