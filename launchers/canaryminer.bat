@echo off
rem Argument pass-through only. Everything else (config, devices, fee, shutdown, stats) is the native binary's job.
rem Example: canaryminer.bat --pool f2pool --wallet YOUR_PRL_ADDRESS.rig1
"%~dp0canary-miner.exe" %*
exit /b %ERRORLEVEL%
